#include "overlay/InternalHud.h"

#include "core/Config.h"
#include "core/Log.h"
#include "game/Game.h"
#include "game/LethalState.h"
#include "game/PlayerActions.h"
#include "game/RuntimeCatalog.h"
#include "mono/Runtime.h"
#include "overlay/InternalHudRenderer.h"
#include "overlay/Menu.h"
#include "overlay/Theme.h"
#include "platform/Environment.h"
#include "platform/FileSystem.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace kue::internalhud {
namespace {

#define MSABI __attribute__((ms_abi))

LethalState* gSource = nullptr;
Config* gConfig = nullptr;
ImGuiContext* gContext = nullptr;
std::optional<ui::Interface> gInterface;
PlayerActionQueue gManagedActions;
constexpr int kMaximumScreenDimension = 16384;
constexpr std::uint64_t kMaximumFontFileBytes = std::uint64_t{64} * 1024 * 1024;
constexpr float kBodyFontPixels = 14.f;
constexpr float kTitleFontPixels = 18.f;
constexpr float kCaptionFontPixels = 12.f;

ImFont* loadFontFile(ImGuiIO& io, const char* path, float pixels) {
    const platform::DescriptorResult opened = platform::openForRead(path);
    if (opened.descriptor < 0) {
        KUE_WARN("internal HUD: cannot open font %s (errno=%d)", path, opened.errorCode);
        return nullptr;
    }
    const platform::FileInspection inspection = platform::inspectFile(opened.descriptor);
    if (!inspection.succeeded || inspection.kind != platform::FileKind::Regular ||
        inspection.bytes == 0 || inspection.bytes > kMaximumFontFileBytes) {
        KUE_WARN("internal HUD: font %s is not a regular file within %llu bytes", path,
                 static_cast<unsigned long long>(kMaximumFontFileBytes));
        static_cast<void>(platform::closeDescriptor(opened.descriptor));
        return nullptr;
    }
    const std::size_t size = static_cast<std::size_t>(inspection.bytes);
    void* const data = ImGui::MemAlloc(size);
    if (!data) {
        static_cast<void>(platform::closeDescriptor(opened.descriptor));
        return nullptr;
    }
    std::size_t loaded = 0;
    bool readFailed = false;
    while (loaded < size) {
        const platform::ReadResult read = platform::readSome(
            opened.descriptor, std::span<char>(static_cast<char*>(data) + loaded, size - loaded));
        if (read.errorCode != 0 || read.bytes == 0) {
            readFailed = true;
            break;
        }
        loaded += read.bytes;
    }
    static_cast<void>(platform::closeDescriptor(opened.descriptor));
    if (readFailed) {
        KUE_WARN("internal HUD: cannot read font %s", path);
        ImGui::MemFree(data);
        return nullptr;
    }
    ImFontConfig pixelAligned;
    pixelAligned.OversampleH = 1;
    pixelAligned.OversampleV = 1;
    pixelAligned.PixelSnapH = true;
    ImFont* const font =
        io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(size), pixels, &pixelAligned);
    if (!font)
        ImGui::MemFree(data);
    return font;
}

ImFont* loadSystemFont(ImGuiIO& io, platform::SystemFontRole role, float pixels) {
    platform::EnvironmentStorage storage;
    const platform::SystemFontPath systemFont = platform::systemFontPath(role, storage);
    if (!systemFont.available)
        return nullptr;
    return loadFontFile(io, storage.data(), pixels);
}

ImFont* loadBodyFont(ImGuiIO& io, float pixels) {
    ImFont* font = nullptr;
    if (!gConfig->fontPath.empty()) {
        font = loadFontFile(io, gConfig->fontPath.c_str(), pixels);
    }
    if (!font) {
        font = loadSystemFont(io, platform::SystemFontRole::Body, pixels);
    }
    return font;
}

RuntimeCatalogs gRuntimeCatalogs;
InternalHudRenderer gRenderer;
double gLastFrameTime = 0.0;
double gNextFpsLabelTime = 0.0;
float gDisplayedFps = 60.f;
ConfigSaveSchedule gConfigSaveSchedule;
int gConfigRevision = 1;
SessionState gSession;
std::optional<ui::MenuPreview> gPreview;

bool readCatalogName(mono::MonoObject* stringObject,
                     std::array<char, mono::kManagedStringMaxUtf8Bytes>& storage,
                     std::string_view& name) {
    const mono::ManagedStringUtf8Result result =
        mono::readManagedStringUtf8(stringObject, {storage.data(), storage.size()});
    if (result.status == mono::ManagedStringUtf8Status::Success) {
        name = result.text;
        return true;
    }
    KUE_ERR("internal HUD: catalog name conversion failed status=%s code-units=%d "
            "validated-code-units=%zu utf8-bytes=%zu",
            mono::managedStringUtf8StatusName(result.status), result.codeUnitCount,
            result.validatedCodeUnits, result.utf8Bytes);
    return false;
}

void processConfigSave(ConfigSaveSchedule::Clock::time_point now) {
    if (!gConfig || !gConfigSaveSchedule.due(now))
        return;
    std::string configError;
    const std::string configPath = gConfig->filePath.empty() ? "kuelethal.json" : gConfig->filePath;
    const ConfigSaveResult result =
        configSave(*gConfig, {.path = configPath, .error = configError});
    gConfigSaveSchedule.record(result, now);
    if (result != ConfigSaveResult::Durable)
        KUE_ERR("internal HUD: config save failed: %s", configError.c_str());
}

void advanceConfigRevision() {
    gConfigRevision = gConfigRevision == std::numeric_limits<int>::max() ? 1 : gConfigRevision + 1;
}

enum class ManagedPayloadStatus : std::uint8_t {
    Encoded,
    InvalidRequest,
    ClientIdOutOfRange,
    NoCatalog,
    StaleGeneration,
    IndexOutOfRange,
};

struct ManagedActionArguments {
    int clientId = -1;
    int catalogIndex = -1;
    int count = 0;
    int amount = -1;
};

struct ManagedPayloadEncoding {
    ManagedPayloadStatus status = ManagedPayloadStatus::InvalidRequest;
    ManagedActionArguments arguments;
};

ManagedPayloadStatus managedPayloadStatus(PlayerActionCatalogResolutionResult result) noexcept {
    switch (result) {
    case PlayerActionCatalogResolutionResult::NotRequired:
    case PlayerActionCatalogResolutionResult::Resolved:
        return ManagedPayloadStatus::Encoded;
    case PlayerActionCatalogResolutionResult::InvalidRequest:
        return ManagedPayloadStatus::InvalidRequest;
    case PlayerActionCatalogResolutionResult::NoCatalog:
        return ManagedPayloadStatus::NoCatalog;
    case PlayerActionCatalogResolutionResult::StaleGeneration:
        return ManagedPayloadStatus::StaleGeneration;
    case PlayerActionCatalogResolutionResult::IndexOutOfRange:
        return ManagedPayloadStatus::IndexOutOfRange;
    }
    return ManagedPayloadStatus::InvalidRequest;
}

std::optional<int> managedClientId(std::uint64_t clientId) noexcept {
    if (clientId > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(clientId);
}

ManagedPayloadEncoding encodeManagedPayload(const PlayerActionRequest& request,
                                            const RuntimeCatalogs& catalogs) {
    const PlayerActionCatalogResolution resolution = resolvePlayerActionCatalog(request, catalogs);
    const ManagedPayloadStatus resolutionStatus = managedPayloadStatus(resolution.result);
    if (resolutionStatus != ManagedPayloadStatus::Encoded) {
        return {resolutionStatus, {}};
    }
    const int catalogIndex = static_cast<int>(resolution.index);
    if (std::holds_alternative<NoPlayerActionPayload>(request.payload)) {
        return {ManagedPayloadStatus::Encoded, {}};
    }
    if (const auto* payload = std::get_if<PlayerTargetPayload>(&request.payload)) {
        const std::optional<int> clientId = managedClientId(payload->clientId);
        if (!clientId) {
            return {ManagedPayloadStatus::ClientIdOutOfRange, {}};
        }
        return {ManagedPayloadStatus::Encoded, {.clientId = *clientId}};
    }
    if (const auto* payload = std::get_if<EnemySpawnPayload>(&request.payload)) {
        return {ManagedPayloadStatus::Encoded,
                {.catalogIndex = catalogIndex, .count = static_cast<int>(payload->count)}};
    }
    if (const auto* payload = std::get_if<EnemyAtPlayerPayload>(&request.payload)) {
        const std::optional<int> clientId = managedClientId(payload->clientId);
        if (!clientId) {
            return {ManagedPayloadStatus::ClientIdOutOfRange, {}};
        }
        return {ManagedPayloadStatus::Encoded,
                {.clientId = *clientId,
                 .catalogIndex = catalogIndex,
                 .count = static_cast<int>(payload->count),
                 .amount = static_cast<int>(payload->distanceMeters)}};
    }
    if (const auto* payload = std::get_if<ItemAtPlayerPayload>(&request.payload)) {
        const std::optional<int> clientId = managedClientId(payload->clientId);
        if (!clientId) {
            return {ManagedPayloadStatus::ClientIdOutOfRange, {}};
        }
        return {ManagedPayloadStatus::Encoded,
                {.clientId = *clientId,
                 .catalogIndex = catalogIndex,
                 .count = static_cast<int>(payload->count),
                 .amount = payload->fixedScrapValue.value_or(-1)}};
    }
    if (const auto* payload = std::get_if<HazardAtPlayerPayload>(&request.payload)) {
        const std::optional<int> clientId = managedClientId(payload->clientId);
        if (!clientId) {
            return {ManagedPayloadStatus::ClientIdOutOfRange, {}};
        }
        return {ManagedPayloadStatus::Encoded,
                {.clientId = *clientId,
                 .catalogIndex = catalogIndex,
                 .count = static_cast<int>(payload->count),
                 .amount = static_cast<int>(payload->distanceMeters)}};
    }
    if (const auto* payload = std::get_if<TerminalCreditsPayload>(&request.payload)) {
        return {ManagedPayloadStatus::Encoded, {.amount = payload->amount}};
    }
    if (std::holds_alternative<MoonTravelPayload>(request.payload)) {
        return {ManagedPayloadStatus::Encoded, {.catalogIndex = catalogIndex}};
    }
    const auto* payload = std::get_if<PlushieIntervalPayload>(&request.payload);
    if (!payload || payload->interval.count() > std::numeric_limits<int>::max()) {
        return {};
    }
    return {ManagedPayloadStatus::Encoded, {.amount = static_cast<int>(payload->interval.count())}};
}

void discardImGuiContext() noexcept {
    if (gContext) {
        ImGui::SetCurrentContext(gContext);
        ui::setFonts({.body = nullptr, .title = nullptr, .caption = nullptr});
        gRenderer.clearFontAtlas();
        ImGui::DestroyContext(gContext);
        gContext = nullptr;
    }
    gLastFrameTime = 0.0;
}

void ensureContext() {
    if (gContext || !gSource || !gConfig)
        return;
    IMGUI_CHECKVERSION();
    gContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(gContext);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFont* body = loadBodyFont(io, kBodyFontPixels);
    if (!body) {
        body = io.Fonts->AddFontDefault();
    }
    ImFont* caption = loadBodyFont(io, kCaptionFontPixels);
    if (!caption) {
        caption = body;
    }
    ImFont* title = loadSystemFont(io, platform::SystemFontRole::Title, kTitleFontPixels);
    if (!title) {
        title = body;
    }
    io.FontDefault = body;
    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    if (!fontPixels || fontWidth <= 0 || fontHeight <= 0) {
        discardImGuiContext();
        return;
    }
    const std::size_t fontBytes =
        static_cast<std::size_t>(fontWidth) * static_cast<std::size_t>(fontHeight) * std::size_t{4};
    const MenuRasterStatus fontStatus = gRenderer.setFontAtlas(
        {std::span<const std::uint8_t>{fontPixels, fontBytes}, fontWidth, fontHeight});
    if (fontStatus != MenuRasterStatus::Success) {
        KUE_ERR("internal HUD: font atlas rejected: %s", menuRasterStatusName(fontStatus));
        discardImGuiContext();
        return;
    }
    ui::setFonts({.body = body, .title = title, .caption = caption});
    ui::applyTheme();
    if (!gInterface) {
        gInterface.emplace();
        gInterface->setVisible(ui::MenuVisibility::Visible);
    }
    KUE_INFO("internal HUD: original ImGui interface initialized");
}

void* MSABI renderMenu(int screenW, int screenH, float mouseX, float mouseY, int mouseDown,
                       float wheel, int pressedKey, int* outX, int* outY, int* outW, int* outH,
                       int* outBytes, std::uint64_t* outRevision) {
    if (outX)
        *outX = 0;
    if (outY)
        *outY = 0;
    if (outW)
        *outW = 0;
    if (outH)
        *outH = 0;
    if (outBytes)
        *outBytes = 0;
    if (outRevision)
        *outRevision = 0;
    ensureContext();
    if (!gContext || !gInterface || !gSource || !gConfig || screenW <= 0 || screenH <= 0 ||
        screenW > kMaximumScreenDimension || screenH > kMaximumScreenDimension ||
        !std::isfinite(mouseX) || !std::isfinite(mouseY) || !std::isfinite(wheel)) {
        return nullptr;
    }
    ImGui::SetCurrentContext(gContext);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(screenW), static_cast<float>(screenH));
    const ConfigSaveSchedule::Clock::time_point now = ConfigSaveSchedule::Clock::now();
    const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
    io.DeltaTime = gLastFrameTime > 0.0 ? static_cast<float>(seconds - gLastFrameTime) : 1.f / 60.f;
    gLastFrameTime = seconds;

    if (seconds >= gNextFpsLabelTime) {
        gDisplayedFps = io.Framerate;
        gNextFpsLabelTime = seconds + 1.0;
    }
    io.AddMousePosEvent(mouseX, mouseY);
    io.AddMouseButtonEvent(0, mouseDown != 0);
    if (std::fabs(wheel) > std::numeric_limits<float>::epsilon())
        io.AddMouseWheelEvent(0.f, wheel);
    ImGui::NewFrame();

    static MenuSnapshot snapshot;
    snapshot.playerFrame = gSource->readPlayerFrame();
    snapshot.session = gSession;

    InternalHudFrameTransaction transaction(*gInterface, *gConfig);
    transaction.interface().render(
        snapshot, gRuntimeCatalogs,
        ui::MenuFrame{screenW, screenH, gDisplayedFps, parseMenuKey(pressedKey)}, gSource->status(),
        transaction.configuration());
    ImGui::Render();

    MenuRasterFrame rasterFrame{};
    MenuFrameEffects effects{};
    const MenuRasterStatus rasterStatus = transaction.commit(
        gRenderer, ImGui::GetDrawData(), transaction.interface().menuRectangle(),
        MenuScreen{screenW, screenH}, *gInterface, *gConfig, rasterFrame, effects);
    static MenuRasterStatus lastRasterStatus = MenuRasterStatus::Success;
    if (rasterStatus != MenuRasterStatus::Success) {
        if (rasterStatus != lastRasterStatus)
            KUE_ERR("internal HUD: raster input rejected: %s", menuRasterStatusName(rasterStatus));
        lastRasterStatus = rasterStatus;
        discardImGuiContext();
        return nullptr;
    }
    lastRasterStatus = MenuRasterStatus::Success;
    gPreview = gInterface->preview();

    PlayerActionRequest action;
    if (gInterface->readAction(action)) {
        const ManagedPayloadEncoding encoding = encodeManagedPayload(action, gRuntimeCatalogs);
        const PlayerActionEnqueueResult result = encoding.status == ManagedPayloadStatus::Encoded
                                                     ? gManagedActions.push(action)
                                                     : PlayerActionEnqueueResult::InvalidRequest;
        const int actionValue = static_cast<int>(action.action);
        switch (result) {
        case PlayerActionEnqueueResult::Queued:
            KUE_INFO("internal HUD: queued player action=%d", actionValue);
            break;
        case PlayerActionEnqueueResult::InvalidRequest:
            KUE_ERR("internal HUD: player action handoff invariant failed action=%d", actionValue);
            break;
        case PlayerActionEnqueueResult::CapacityExceeded:
            break;
        }
        gInterface->applyActionEnqueueResult(result);
    }
    if (effects.configurationChanged) {
        advanceConfigRevision();
        gConfigSaveSchedule.requestAutosave(now);
        gSource->apply(*gConfig);
    }
    if (effects.explicitSaveRequested)
        gConfigSaveSchedule.requestExplicit();
    processConfigSave(now);

    if (outX)
        *outX = rasterFrame.x;
    if (outY)
        *outY = rasterFrame.y;
    if (outW)
        *outW = rasterFrame.width;
    if (outH)
        *outH = rasterFrame.height;
    const int pixelBytes = static_cast<int>(rasterFrame.pixels.size());
    if (outBytes) {
        *outBytes = rasterFrame.change == MenuPixelChange::Changed ? pixelBytes : -pixelBytes;
    }
    if (outRevision)
        *outRevision = rasterFrame.revision;
    return rasterFrame.pixels.empty() ? nullptr
                                      : const_cast<std::uint8_t*>(rasterFrame.pixels.data());
}

void MSABI getHudConfig(int* flags, float* maxDistance, int* menuKey, int* tickIntervalMs,
                        int* revision) {
    if (!gConfig)
        return;
    processConfigSave(ConfigSaveSchedule::Clock::now());
    int f = 0;
    if (gConfig->esp.players)
        f |= 1 << 0;
    if (gConfig->esp.items)
        f |= 1 << 1;
    if (gConfig->esp.monsters)
        f |= 1 << 2;
    if (gConfig->esp.exits)
        f |= 1 << 3;
    if (gConfig->esp.fireExits)
        f |= 1 << 4;
    if (gConfig->esp.ships)
        f |= 1 << 5;
    if (gConfig->esp.names)
        f |= 1 << 6;
    if (gConfig->esp.values)
        f |= 1 << 7;
    if (gConfig->esp.distance)
        f |= 1 << 8;
    if (gConfig->esp.outlines)
        f |= 1 << 9;
    if (gConfig->esp.lines)
        f |= 1 << 10;
    if (gConfig->esp.useScrapTiers)
        f |= 1 << 11;
    if (gConfig->esp.deathNotifications)
        f |= 1 << 12;
    if (gConfig->infiniteBattery)
        f |= 1 << 13;
    if (gConfig->extendedInventory)
        f |= 1 << 14;
    if (gConfig->infiniteStamina)
        f |= 1 << 15;
    if (gConfig->noWeight)
        f |= 1 << 16;
    if (flags)
        *flags = f;
    if (maxDistance)
        *maxDistance = gConfig->esp.maxDistance;
    if (menuKey)
        *menuKey = static_cast<int>(gConfig->menuKey);
    if (tickIntervalMs)
        *tickIntervalMs = gConfig->tickIntervalMs;
    if (revision)
        *revision = gConfigRevision;
}

int MSABI pollAction(int* clientId, int* catalogIndex, int* count, int* amount) {
    PlayerActionRequest request;
    if (!gManagedActions.pop(request)) {
        return static_cast<int>(PlayerAction::None);
    }
    const ManagedPayloadEncoding encoding = encodeManagedPayload(request, gRuntimeCatalogs);
    if (encoding.status != ManagedPayloadStatus::Encoded) {
        KUE_ERR("internal HUD: queued action rejected action=%d payload-status=%u",
                static_cast<int>(request.action), static_cast<unsigned int>(encoding.status));
        return static_cast<int>(PlayerAction::None);
    }
    if (clientId) {
        *clientId = encoding.arguments.clientId;
    }
    if (catalogIndex) {
        *catalogIndex = encoding.arguments.catalogIndex;
    }
    if (count) {
        *count = encoding.arguments.count;
    }
    if (amount) {
        *amount = encoding.arguments.amount;
    }
    return static_cast<int>(request.action);
}

void MSABI reportSessionState(int switches, int world, std::int64_t playersInFacility,
                              int lureClientId, int lureSecondsLeft) {
    SessionState session;
    session.switches = FlagSet<SessionSwitch>::fromBits(static_cast<std::uint32_t>(switches));
    session.world = FlagSet<WorldFeature>::fromBits(static_cast<std::uint32_t>(world));
    session.playersInFacility = static_cast<std::uint64_t>(playersInFacility);
    if (lureClientId >= 0) {
        EnemyLure lure{.clientId = static_cast<std::uint64_t>(lureClientId), .secondsLeft = {}};
        if (lureSecondsLeft >= 0) {
            lure.secondsLeft = lureSecondsLeft;
        }
        session.lure = lure;
    }
    gSession = session;
}

int MSABI getPreviewRequest(int* kind, int* catalogIndex, int* x, int* y, int* width, int* height) {
    if (!gPreview || !kind || !catalogIndex || !x || !y || !width || !height) {
        return 0;
    }
    *kind = static_cast<int>(gPreview->kind);
    *catalogIndex = static_cast<int>(gPreview->catalogIndex);
    *x = static_cast<int>(std::ceil(gPreview->rectangle.x));
    *y = static_cast<int>(std::ceil(gPreview->rectangle.y));
    *width = static_cast<int>(std::floor(gPreview->rectangle.width));
    *height = static_cast<int>(std::floor(gPreview->rectangle.height));
    return 1;
}

int MSABI reportEnemySpawnTraits(int index, int outside, int supported) {
    if (index < 0 || static_cast<std::size_t>(index) >= kRuntimeCatalogCapacity) {
        return static_cast<int>(EnemyTraitsResult::IndexOutOfRange);
    }
    const EnemyTypeId id{gRuntimeCatalogs.enemyGeneration(), static_cast<std::uint16_t>(index)};
    const EnemySpawnTraits traits{
        .habitat = outside != 0 ? EnemyHabitat::Outside : EnemyHabitat::Facility,
        .loadedMap = supported != 0 ? EnemyMapSupport::Supported : EnemyMapSupport::Unsupported};
    return static_cast<int>(gRuntimeCatalogs.setEnemySpawnTraits(id, traits));
}

void MSABI resetRuntimeCatalogs() {
    gRuntimeCatalogs.resetForSessionEnd();
}

int MSABI beginEnemyCatalog() {
    return static_cast<int>(gRuntimeCatalogs.beginEnemyCatalog());
}

int MSABI reportEnemyType(int instanceId, mono::MonoObject* name) {
    const RuntimeAssetIdentity identity = runtimeAssetIdentityFromSigned32(instanceId);
    std::array<char, mono::kManagedStringMaxUtf8Bytes> decodedBytes{};
    std::string_view decoded;
    if (!name || !readCatalogName(name, decodedBytes, decoded)) {
        return -1;
    }
    const CatalogReportOutcome outcome =
        gRuntimeCatalogs.reportEnemy(identity, OrderedCatalogName{decoded});
    if (outcome.result != CatalogReportResult::Recorded) {
        KUE_ERR("internal HUD: enemy catalog report failed result=%u entry=%u actual=%llu "
                "limit=%llu",
                static_cast<unsigned int>(outcome.result),
                static_cast<unsigned int>(outcome.entryIndex),
                static_cast<unsigned long long>(outcome.actual),
                static_cast<unsigned long long>(outcome.limit));
    }
    return static_cast<int>(outcome.result);
}

int MSABI commitEnemyCatalog() {
    const CatalogCommitOutcome outcome = gRuntimeCatalogs.commitEnemyCatalog();
    if (outcome.result != CatalogCommitResult::Committed &&
        outcome.result != CatalogCommitResult::Unchanged) {
        KUE_ERR("internal HUD: enemy catalog commit failed result=%u report=%u entry=%u",
                static_cast<unsigned int>(outcome.result),
                static_cast<unsigned int>(outcome.failure.result),
                static_cast<unsigned int>(outcome.failure.entryIndex));
    }
    return static_cast<int>(outcome.result);
}

void MSABI abortEnemyCatalog() {
    static_cast<void>(gRuntimeCatalogs.abortEnemyCatalog());
}

int MSABI beginActiveEnemyTypes() {
    return static_cast<int>(
        gRuntimeCatalogs.beginEnemyActivity(gRuntimeCatalogs.enemyGeneration()));
}

int MSABI reportActiveEnemyType(int index, int count) {
    if (index < 0 || static_cast<std::size_t>(index) >= kRuntimeCatalogCapacity)
        return static_cast<int>(EnemyActivityReportResult::IndexOutOfRange);
    if (count <= 0)
        return static_cast<int>(EnemyActivityReportResult::ZeroCount);
    const EnemyTypeId id{gRuntimeCatalogs.enemyGeneration(), static_cast<std::uint16_t>(index)};
    const EnemyActivityReportOutcome outcome =
        gRuntimeCatalogs.reportEnemyActivity(id, static_cast<std::uint32_t>(count));
    return static_cast<int>(outcome.result);
}

int MSABI commitActiveEnemyTypes() {
    return static_cast<int>(gRuntimeCatalogs.commitEnemyActivity().result);
}

void MSABI abortActiveEnemyTypes() {
    static_cast<void>(gRuntimeCatalogs.abortEnemyActivity());
}

int MSABI beginItemCatalog() {
    return static_cast<int>(gRuntimeCatalogs.beginItemCatalog());
}

int MSABI reportItemType(int instanceId, mono::MonoObject* name) {
    const RuntimeAssetIdentity identity = runtimeAssetIdentityFromSigned32(instanceId);
    std::array<char, mono::kManagedStringMaxUtf8Bytes> decodedBytes{};
    std::string_view decoded;
    if (!name || !readCatalogName(name, decodedBytes, decoded)) {
        return -1;
    }
    const CatalogReportOutcome outcome =
        gRuntimeCatalogs.reportItem(identity, OrderedCatalogName{decoded});
    if (outcome.result != CatalogReportResult::Recorded) {
        KUE_ERR(
            "internal HUD: item catalog report failed result=%u entry=%u actual=%llu limit=%llu",
            static_cast<unsigned int>(outcome.result),
            static_cast<unsigned int>(outcome.entryIndex),
            static_cast<unsigned long long>(outcome.actual),
            static_cast<unsigned long long>(outcome.limit));
    }
    return static_cast<int>(outcome.result);
}

int MSABI commitItemCatalog() {
    const CatalogCommitOutcome outcome = gRuntimeCatalogs.commitItemCatalog();
    if (outcome.result != CatalogCommitResult::Committed &&
        outcome.result != CatalogCommitResult::Unchanged) {
        KUE_ERR("internal HUD: item catalog commit failed result=%u report=%u entry=%u",
                static_cast<unsigned int>(outcome.result),
                static_cast<unsigned int>(outcome.failure.result),
                static_cast<unsigned int>(outcome.failure.entryIndex));
    }
    return static_cast<int>(outcome.result);
}

void MSABI abortItemCatalog() {
    static_cast<void>(gRuntimeCatalogs.abortItemCatalog());
}

int MSABI beginMoonCatalog() {
    return static_cast<int>(gRuntimeCatalogs.beginMoonCatalog());
}

int MSABI reportMoonType(int instanceId, mono::MonoObject* name) {
    const RuntimeAssetIdentity identity = runtimeAssetIdentityFromSigned32(instanceId);
    std::array<char, mono::kManagedStringMaxUtf8Bytes> decodedBytes{};
    std::string_view decoded;
    if (!name || !readCatalogName(name, decodedBytes, decoded)) {
        return -1;
    }
    const CatalogReportOutcome outcome =
        gRuntimeCatalogs.reportMoon(identity, OrderedCatalogName{decoded});
    if (outcome.result != CatalogReportResult::Recorded) {
        KUE_ERR(
            "internal HUD: moon catalog report failed result=%u entry=%u actual=%llu limit=%llu",
            static_cast<unsigned int>(outcome.result),
            static_cast<unsigned int>(outcome.entryIndex),
            static_cast<unsigned long long>(outcome.actual),
            static_cast<unsigned long long>(outcome.limit));
    }
    return static_cast<int>(outcome.result);
}

int MSABI commitMoonCatalog() {
    const CatalogCommitOutcome outcome = gRuntimeCatalogs.commitMoonCatalog();
    if (outcome.result != CatalogCommitResult::Committed &&
        outcome.result != CatalogCommitResult::Unchanged) {
        KUE_ERR("internal HUD: moon catalog commit failed result=%u report=%u entry=%u",
                static_cast<unsigned int>(outcome.result),
                static_cast<unsigned int>(outcome.failure.result),
                static_cast<unsigned int>(outcome.failure.entryIndex));
    }
    return static_cast<int>(outcome.result);
}

void MSABI abortMoonCatalog() {
    static_cast<void>(gRuntimeCatalogs.abortMoonCatalog());
}

int MSABI beginHazardCatalog() {
    return static_cast<int>(gRuntimeCatalogs.beginHazardCatalog());
}

int MSABI reportHazardType(int instanceId, mono::MonoObject* name) {
    const RuntimeAssetIdentity identity = runtimeAssetIdentityFromSigned32(instanceId);
    std::array<char, mono::kManagedStringMaxUtf8Bytes> decodedBytes{};
    std::string_view decoded;
    if (!name || !readCatalogName(name, decodedBytes, decoded)) {
        return -1;
    }
    const CatalogReportOutcome outcome =
        gRuntimeCatalogs.reportHazard(identity, OrderedCatalogName{decoded});
    if (outcome.result != CatalogReportResult::Recorded) {
        KUE_ERR(
            "internal HUD: hazard catalog report failed result=%u entry=%u actual=%llu limit=%llu",
            static_cast<unsigned int>(outcome.result),
            static_cast<unsigned int>(outcome.entryIndex),
            static_cast<unsigned long long>(outcome.actual),
            static_cast<unsigned long long>(outcome.limit));
    }
    return static_cast<int>(outcome.result);
}

int MSABI commitHazardCatalog() {
    const CatalogCommitOutcome outcome = gRuntimeCatalogs.commitHazardCatalog();
    if (outcome.result != CatalogCommitResult::Committed &&
        outcome.result != CatalogCommitResult::Unchanged) {
        KUE_ERR("internal HUD: hazard catalog commit failed result=%u report=%u entry=%u",
                static_cast<unsigned int>(outcome.result),
                static_cast<unsigned int>(outcome.failure.result),
                static_cast<unsigned int>(outcome.failure.entryIndex));
    }
    return static_cast<int>(outcome.result);
}

void MSABI abortHazardCatalog() {
    static_cast<void>(gRuntimeCatalogs.abortHazardCatalog());
}

void MSABI getEspColor(int category, float* r, float* g, float* b, float* a) {
    if (!gConfig)
        return;
    const std::array<float, 4>* color = &gConfig->esp.colorPlayers;
    switch (category) {
    case 1:
        color = &gConfig->esp.colorItems;
        break;
    case 2:
        color = &gConfig->esp.colorMonsters;
        break;
    case 3:
        color = &gConfig->esp.colorExits;
        break;
    case 4:
        color = &gConfig->esp.colorFireExits;
        break;
    case 5:
        color = &gConfig->esp.colorShips;
        break;
    default:
        break;
    }
    if (r)
        *r = (*color)[0];
    if (g)
        *g = (*color)[1];
    if (b)
        *b = (*color)[2];
    if (a)
        *a = (*color)[3];
}

}

void initialize(LethalState& source, Config& config) {
    gSource = &source;
    gConfig = &config;
}

bool registerManagedBridge() {
    const bool render = mono::addInternalCall("Kue.Internal.NativeBridge::RenderMenu",
                                              reinterpret_cast<const void*>(&renderMenu));
    const bool hudConfig = mono::addInternalCall("Kue.Internal.NativeBridge::GetHudConfig",
                                                 reinterpret_cast<const void*>(&getHudConfig));
    const bool color = mono::addInternalCall("Kue.Internal.NativeBridge::GetEspColor",
                                             reinterpret_cast<const void*>(&getEspColor));
    const bool actions = mono::addInternalCall("Kue.Internal.NativeBridge::PollAction",
                                               reinterpret_cast<const void*>(&pollAction));
    const bool catalogReset =
        mono::addInternalCall("Kue.Internal.NativeBridge::ResetRuntimeCatalogs",
                              reinterpret_cast<const void*>(&resetRuntimeCatalogs));
    const bool sessionState =
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportSessionState",
                              reinterpret_cast<const void*>(&reportSessionState)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportEnemySpawnTraits",
                              reinterpret_cast<const void*>(&reportEnemySpawnTraits)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::GetPreviewRequest",
                              reinterpret_cast<const void*>(&getPreviewRequest));
    const bool catalogBegin =
        mono::addInternalCall("Kue.Internal.NativeBridge::BeginEnemyCatalog",
                              reinterpret_cast<const void*>(&beginEnemyCatalog));
    const bool catalogEntry =
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportEnemyType",
                              reinterpret_cast<const void*>(&reportEnemyType));
    const bool catalogCommit =
        mono::addInternalCall("Kue.Internal.NativeBridge::CommitEnemyCatalog",
                              reinterpret_cast<const void*>(&commitEnemyCatalog));
    const bool catalogAbort =
        mono::addInternalCall("Kue.Internal.NativeBridge::AbortEnemyCatalog",
                              reinterpret_cast<const void*>(&abortEnemyCatalog));
    const bool activeCatalogBegin =
        mono::addInternalCall("Kue.Internal.NativeBridge::BeginActiveEnemyTypes",
                              reinterpret_cast<const void*>(&beginActiveEnemyTypes));
    const bool activeCatalogEntry =
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportActiveEnemyType",
                              reinterpret_cast<const void*>(&reportActiveEnemyType));
    const bool activeCatalogCommit =
        mono::addInternalCall("Kue.Internal.NativeBridge::CommitActiveEnemyTypes",
                              reinterpret_cast<const void*>(&commitActiveEnemyTypes));
    const bool activeCatalogAbort =
        mono::addInternalCall("Kue.Internal.NativeBridge::AbortActiveEnemyTypes",
                              reinterpret_cast<const void*>(&abortActiveEnemyTypes));
    const bool itemCatalogBegin =
        mono::addInternalCall("Kue.Internal.NativeBridge::BeginItemCatalog",
                              reinterpret_cast<const void*>(&beginItemCatalog));
    const bool itemCatalogEntry =
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportItemType",
                              reinterpret_cast<const void*>(&reportItemType));
    const bool itemCatalogCommit =
        mono::addInternalCall("Kue.Internal.NativeBridge::CommitItemCatalog",
                              reinterpret_cast<const void*>(&commitItemCatalog));
    const bool itemCatalogAbort =
        mono::addInternalCall("Kue.Internal.NativeBridge::AbortItemCatalog",
                              reinterpret_cast<const void*>(&abortItemCatalog));
    const bool moonCatalog =
        mono::addInternalCall("Kue.Internal.NativeBridge::BeginMoonCatalog",
                              reinterpret_cast<const void*>(&beginMoonCatalog)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportMoonType",
                              reinterpret_cast<const void*>(&reportMoonType)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::CommitMoonCatalog",
                              reinterpret_cast<const void*>(&commitMoonCatalog)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::AbortMoonCatalog",
                              reinterpret_cast<const void*>(&abortMoonCatalog));
    const bool hazardCatalog =
        mono::addInternalCall("Kue.Internal.NativeBridge::BeginHazardCatalog",
                              reinterpret_cast<const void*>(&beginHazardCatalog)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::ReportHazardType",
                              reinterpret_cast<const void*>(&reportHazardType)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::CommitHazardCatalog",
                              reinterpret_cast<const void*>(&commitHazardCatalog)) &&
        mono::addInternalCall("Kue.Internal.NativeBridge::AbortHazardCatalog",
                              reinterpret_cast<const void*>(&abortHazardCatalog));
    const bool registered =
        render && hudConfig && color && actions && catalogReset && sessionState && catalogBegin &&
        catalogEntry && catalogCommit && catalogAbort && activeCatalogBegin && activeCatalogEntry &&
        activeCatalogCommit && activeCatalogAbort && itemCatalogBegin && itemCatalogEntry &&
        itemCatalogCommit && itemCatalogAbort && moonCatalog && hazardCatalog;
    if (registered)
        KUE_INFO("internal HUD: Mono bridge registered");
    return registered;
}

}
