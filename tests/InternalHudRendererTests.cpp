#include "core/Config.h"

#include "AlignedAllocation.h"
#include "core/Log.h"
#include "core/Utf8.h"
#include "game/Game.h"
#include "game/RuntimeCatalog.h"
#include "overlay/InternalHudRenderer.h"
#include "overlay/Menu.h"
#include "overlay/Theme.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<std::size_t> gAllocations{0};
std::atomic<std::size_t> gAllocatedBytes{0};
kue::RuntimeCatalogs gCatalogs;

void* countedAllocation(std::size_t size) {
    const std::size_t allocatedSize = size == 0 ? 1 : size;
    if (void* allocation = std::malloc(allocatedSize)) {
        gAllocations.fetch_add(1, std::memory_order_relaxed);
        gAllocatedBytes.fetch_add(size, std::memory_order_relaxed);
        return allocation;
    }
    throw std::bad_alloc{};
}

#ifndef KUE_DISABLE_ALLOCATION_PROBE
void* countedAlignedAllocation(std::size_t size, std::align_val_t alignment) {
    const std::size_t allocatedSize = size == 0 ? 1 : size;
    if (void* allocation =
            kue::tests::allocateAligned(allocatedSize, static_cast<std::size_t>(alignment))) {
        gAllocations.fetch_add(1, std::memory_order_relaxed);
        gAllocatedBytes.fetch_add(size, std::memory_order_relaxed);
        return allocation;
    }
    throw std::bad_alloc{};
}
#endif

}

namespace kue {

void logFormat(LogLevel, const char*, ...) {}

}

#ifndef KUE_DISABLE_ALLOCATION_PROBE
void* operator new(std::size_t size) {
    return countedAllocation(size);
}

void* operator new[](std::size_t size) {
    return countedAllocation(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return countedAlignedAllocation(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return countedAlignedAllocation(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return countedAllocation(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return countedAllocation(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return countedAlignedAllocation(size, alignment);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return countedAlignedAllocation(size, alignment);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::align_val_t) noexcept {
    kue::tests::releaseAligned(allocation);
}

void operator delete[](void* allocation, std::align_val_t) noexcept {
    kue::tests::releaseAligned(allocation);
}

void operator delete(void* allocation, std::size_t, std::align_val_t) noexcept {
    kue::tests::releaseAligned(allocation);
}

void operator delete[](void* allocation, std::size_t, std::align_val_t) noexcept {
    kue::tests::releaseAligned(allocation);
}

void operator delete(void* allocation, const std::nothrow_t&) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, const std::nothrow_t&) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::align_val_t, const std::nothrow_t&) noexcept {
    kue::tests::releaseAligned(allocation);
}

void operator delete[](void* allocation, std::align_val_t, const std::nothrow_t&) noexcept {
    kue::tests::releaseAligned(allocation);
}
#endif

namespace {

void* allocateImGui(std::size_t size, void*) {
    return countedAllocation(size);
}

void releaseImGui(void* allocation, void*) {
    std::free(allocation);
}

class TestRun final {
  public:
    void expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
    }

    [[nodiscard]] int failures() const noexcept { return mFailures; }

    int result() const {
        if (mFailures == 0)
            std::cout << mAssertions << " assertions passed\n";
        return mFailures == 0 ? 0 : 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

struct PointerInput {
    float x = -1000.f;
    float y = -1000.f;
    bool down = false;
    float wheel = 0.f;
};

struct RenderResult {
    kue::internalhud::MenuRasterStatus status;
    kue::internalhud::MenuRasterFrame frame;
    kue::internalhud::MenuFrameEffects effects;
};

class HudHarness final {
  public:
    HudHarness() {
        mConfig.fontPath.assign(96, 'f');
        mConfig.logPath.assign(96, 'l');
        mConfig.filePath.assign(96, 'c');
        mInterface.setVisible(kue::ui::MenuVisibility::Visible);
        mSnapshot.playerFrame.sprintMeter = 0.73f;
        mSnapshot.playerFrame.carryWeight = 1.42f;
        mSnapshot.flyEnabled = true;
        kue::PlayerSnapshotTransaction players;
        if (players.report({"Kue", 76561198000000001ULL, 0, 100, 12.f, false, true, true}).result !=
                kue::PlayerSnapshotReportResult::Recorded ||
            players.report({"Player Two", 76561198000000002ULL, 1, 83, 40.f, false, false, true})
                    .result != kue::PlayerSnapshotReportResult::Recorded ||
            players.publish(mSnapshot.playerFrame.players).result !=
                kue::PlayerSnapshotPublishResult::Published) {
            std::abort();
        }
        createContext();
    }

    ~HudHarness() { destroyContext(); }

    HudHarness(const HudHarness&) = delete;
    HudHarness& operator=(const HudHarness&) = delete;

    template <typename MutateDrawData>
    RenderResult render(PointerInput input, MutateDrawData mutateDrawData) {
        ImGui::SetCurrentContext(mContext);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920.f, 1080.f);
        io.DeltaTime = 1.f / 60.f;
        io.AddMousePosEvent(input.x, input.y);
        io.AddMouseButtonEvent(0, input.down);
        if (std::fpclassify(input.wheel) != FP_ZERO)
            io.AddMouseWheelEvent(0.f, input.wheel);
        ImGui::NewFrame();

        kue::internalhud::InternalHudFrameTransaction transaction(mInterface, mConfig);
        transaction.interface().render(mSnapshot, gCatalogs, {1920, 1080, 0.f}, "ready",
                                       transaction.configuration());
        ImGui::Render();
        ImDrawData* const drawData = ImGui::GetDrawData();
        mutateDrawData(drawData);

        RenderResult result{};
        result.status =
            transaction.commit(mRenderer, drawData, transaction.interface().menuRectangle(),
                               {1920, 1080}, mInterface, mConfig, result.frame, result.effects);
        return result;
    }

    RenderResult render(PointerInput input = {}) {
        return render(input, [](ImDrawData*) {});
    }

    void recreateContext() {
        destroyContext();
        createContext();
    }

    [[nodiscard]] kue::Config& config() noexcept { return mConfig; }

    [[nodiscard]] kue::ui::Interface& interface() noexcept { return mInterface; }

    [[nodiscard]] kue::internalhud::InternalHudRenderer& renderer() noexcept { return mRenderer; }

  private:
    void createContext() {
        mContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(mContext);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImFont* body = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/TTF/DejaVuSans.ttf", 15.f);
        if (!body) {
            body = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                                15.f);
        }
        if (!body)
            body = io.Fonts->AddFontDefault();
        ImFont* title =
            io.Fonts->AddFontFromFileTTF("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf", 22.f);
        if (!title) {
            title = io.Fonts->AddFontFromFileTTF(
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22.f);
        }
        if (!title)
            title = body;
        io.FontDefault = body;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        const std::size_t bytes =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * std::size_t{4};
        if (mRenderer.setFontAtlas({std::span<const std::uint8_t>{pixels, bytes}, width, height}) !=
            kue::internalhud::MenuRasterStatus::Success) {
            std::abort();
        }
        kue::ui::setFonts({.body = body, .title = title});
        kue::ui::applyTheme();
    }

    void destroyContext() noexcept {
        if (!mContext)
            return;
        ImGui::SetCurrentContext(mContext);
        kue::ui::setFonts({.body = nullptr, .title = nullptr});
        mRenderer.clearFontAtlas();
        ImGui::DestroyContext(mContext);
        mContext = nullptr;
    }

    kue::Config mConfig;
    kue::ui::Interface mInterface;
    kue::internalhud::InternalHudRenderer mRenderer;
    kue::MenuSnapshot mSnapshot;
    ImGuiContext* mContext = nullptr;
};

RenderResult successfulRender(TestRun& run, HudHarness& harness, PointerInput input,
                              std::string_view contract) {
    const RenderResult result = harness.render(input);
    run.expect(result.status == kue::internalhud::MenuRasterStatus::Success, contract);
    return result;
}

std::uint64_t pixelHash(const kue::internalhud::MenuRasterFrame& frame) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t pixel : frame.pixels) {
        hash ^= pixel;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void testExactPixelsAndConsumerRecovery(TestRun& run) {
    HudHarness harness;
    const RenderResult first = harness.render();
    run.expect(first.status == kue::internalhud::MenuRasterStatus::Success,
               "the representative menu raster succeeds");
    run.expect(first.frame.width == 726 && first.frame.height == 446,
               "the representative menu preserves its exact crop");
    run.expect(first.frame.pixels.size() == 1295184,
               "the representative menu publishes every RGBA byte");
    run.expect(first.frame.revision != 0,
               "the first accepted pixels carry a nonzero consumer revision");
    const std::uint64_t hash = pixelHash(first.frame);
    if (hash != 1565097633067612323ULL)
        std::cerr << "actual pixel hash: " << hash << '\n';
    run.expect(hash == 1565097633067612323ULL,
               "the representative menu preserves its exact RGBA hash");

    RenderResult previous = first;
    std::size_t previousPixelBytes = first.frame.pixels.size();
    std::uint64_t previousPixelHash = hash;
    RenderResult unchanged{};
    for (int attempt = 0; attempt < 8; ++attempt) {
        unchanged = harness.render();
        if (unchanged.frame.change == kue::internalhud::MenuPixelChange::Unchanged)
            break;
        previous = unchanged;
        previousPixelBytes = unchanged.frame.pixels.size();
        previousPixelHash = pixelHash(unchanged.frame);
    }
    run.expect(unchanged.status == kue::internalhud::MenuRasterStatus::Success,
               "an unchanged menu remains renderable");
    run.expect(unchanged.frame.change == kue::internalhud::MenuPixelChange::Unchanged,
               "a settled menu reports unchanged cached pixels");
    run.expect(unchanged.frame.revision == previous.frame.revision,
               "unchanged pixels retain their exact revision");
    run.expect(unchanged.frame.pixels.size() == previousPixelBytes,
               "an unchanged frame retains every cached byte for a recreated consumer");
    run.expect(pixelHash(unchanged.frame) == previousPixelHash,
               "cached recovery bytes remain exactly equal to the accepted raster");
    std::uint64_t uploadedRevision = unchanged.frame.revision;
    run.expect(uploadedRevision == unchanged.frame.revision,
               "an existing consumer can skip an already uploaded revision");
    uploadedRevision = 0;
    run.expect(uploadedRevision != unchanged.frame.revision,
               "a recreated consumer requests cached bytes through the nonzero revision");

    harness.recreateContext();
    const RenderResult recreatedProducer = harness.render();
    run.expect(recreatedProducer.status == kue::internalhud::MenuRasterStatus::Success,
               "a recreated producer context emits a valid replacement frame");
    run.expect(recreatedProducer.frame.change == kue::internalhud::MenuPixelChange::Changed,
               "producer recreation invalidates the cached draw identity");
    run.expect(recreatedProducer.frame.revision != unchanged.frame.revision,
               "producer recreation publishes a distinct consumer revision");
    run.expect(pixelHash(recreatedProducer.frame) == hash,
               "producer recreation preserves the exact visible pixels");

    std::uint64_t nextRevision = 17;
    run.expect(kue::internalhud::checkedNextPixelRevision(std::numeric_limits<std::uint64_t>::max(),
                                                          nextRevision) ==
                   kue::internalhud::MenuRasterStatus::RevisionExhausted,
               "pixel revision overflow fails closed");
    run.expect(nextRevision == 17, "pixel revision overflow leaves its output untouched");
}

void testFontBoundaryValidation(TestRun& run) {
    kue::internalhud::InternalHudRenderer renderer;
    std::array<std::uint8_t, 4> pixel{};
    run.expect(renderer.setFontAtlas({pixel, 0, 1}) ==
                   kue::internalhud::MenuRasterStatus::InvalidFont,
               "a zero-width font atlas is rejected");
    run.expect(renderer.setFontAtlas({pixel, 1, 1}) == kue::internalhud::MenuRasterStatus::Success,
               "an exact one-pixel RGBA font atlas is accepted");
    run.expect(renderer.setFontAtlas({std::span<const std::uint8_t>{pixel.data(), 3}, 1, 1}) ==
                   kue::internalhud::MenuRasterStatus::InvalidFont,
               "a truncated font atlas is rejected");
    run.expect(renderer.setFontAtlas({pixel, 8193, 1}) ==
                   kue::internalhud::MenuRasterStatus::InvalidFont,
               "an oversized font dimension is rejected before byte multiplication");
}

void testPlayerListLabelBoundary(TestRun& run) {
    std::string maximumName;
    maximumName.reserve(kue::kPlayerNameCapacity);
    for (std::size_t index = 0; index < kue::kPlayerNameCapacity / 2U; ++index)
        maximumName += "\xC3\xA9";

    kue::PlayerSnapshotTransaction players;
    const kue::PlayerSnapshotReportOutcome report = players.report(kue::PlayerSnapshotInput{
        .name = maximumName,
        .steamId = 1,
        .clientId = 2,
        .health = 100,
        .insanity = 0.F,
        .dead = false,
        .local = true,
        .controlled = true,
    });
    kue::PlayerSnapshot snapshot;
    const kue::PlayerSnapshotPublishOutcome publish = players.publish(snapshot);
    run.expect(report.result == kue::PlayerSnapshotReportResult::Recorded,
               "the maximum valid UTF-8 player name enters the label fixture");
    run.expect(publish.result == kue::PlayerSnapshotPublishResult::Published,
               "the maximum valid UTF-8 player name is published");

    const std::size_t allocationBaseline = gAllocations.load(std::memory_order_relaxed);
    const auto label = kue::ui::playerListLabel(snapshot[0]);
    const std::size_t labelAllocations =
        gAllocations.load(std::memory_order_relaxed) - allocationBaseline;
    const std::string_view labelText{label.data()};
    run.expect(labelText.size() == kue::kPlayerNameCapacity + std::string_view{" (you)"}.size(),
               "the local marker follows the complete maximum-length name");
    run.expect(labelText.substr(0, kue::kPlayerNameCapacity) == maximumName,
               "player-list labeling preserves every validated name byte");
    run.expect(labelText.ends_with(" (you)"),
               "player-list labeling preserves the local-player marker");
    run.expect(kue::isValidUtf8(labelText), "player-list labeling cannot split a UTF-8 sequence");
    run.expect(labelAllocations == 0, "player-list labeling performs no heap allocation");
}

void testMalformedDrawData(TestRun& run) {
    HudHarness harness;
    const RenderResult baseline = harness.render();
    run.expect(baseline.status == kue::internalhud::MenuRasterStatus::Success,
               "the malformed-frame fixture starts valid");

    const RenderResult incoherentLists = harness.render(
        {}, [](ImDrawData* data) { data->CmdLists.Capacity = data->CmdLists.Size - 1; });
    run.expect(incoherentLists.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a draw-list size beyond capacity is rejected");
    harness.recreateContext();

    const RenderResult invalidVertex = harness.render({}, [](ImDrawData* data) {
        data->CmdLists[0]->VtxBuffer[0].pos.x = std::numeric_limits<float>::max();
    });
    run.expect(invalidVertex.status == kue::internalhud::MenuRasterStatus::InvalidVertex,
               "a coordinate outside safe raster intermediates is rejected");
    harness.recreateContext();

    const RenderResult invalidCommand = harness.render({}, [](ImDrawData* data) {
        data->CmdLists[0]->CmdBuffer[0].IdxOffset =
            static_cast<unsigned int>(data->CmdLists[0]->IdxBuffer.Size);
        data->CmdLists[0]->CmdBuffer[0].ElemCount = 3;
    });
    run.expect(invalidCommand.status == kue::internalhud::MenuRasterStatus::InvalidCommand,
               "a command beyond the index range is rejected before raster access");
}

void testViewportContract(TestRun& run) {
    HudHarness harness;
    const RenderResult xOrigin =
        harness.render({}, [](ImDrawData* data) { data->DisplayPos.x = 1.f; });
    run.expect(xOrigin.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a nonzero viewport x origin is rejected");
    harness.recreateContext();

    const RenderResult yOrigin =
        harness.render({}, [](ImDrawData* data) { data->DisplayPos.y = 1.f; });
    run.expect(yOrigin.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a nonzero viewport y origin is rejected");
    harness.recreateContext();

    const RenderResult width =
        harness.render({}, [](ImDrawData* data) { data->DisplaySize.x = 1919.f; });
    run.expect(width.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a mismatched viewport width is rejected");
    harness.recreateContext();

    const RenderResult height =
        harness.render({}, [](ImDrawData* data) { data->DisplaySize.y = 1079.f; });
    run.expect(height.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a mismatched viewport height is rejected");
    harness.recreateContext();

    const RenderResult scaleX =
        harness.render({}, [](ImDrawData* data) { data->FramebufferScale.x = 2.f; });
    run.expect(scaleX.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a scaled framebuffer width is rejected");
    harness.recreateContext();

    const RenderResult scaleY =
        harness.render({}, [](ImDrawData* data) { data->FramebufferScale.y = 2.f; });
    run.expect(scaleY.status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a scaled framebuffer height is rejected");
}

void testRejectedStateIsTransactional(TestRun& run) {
    HudHarness harness;
    const bool originalStamina = harness.config().infiniteStamina;
    kue::internalhud::InternalHudFrameTransaction transaction(harness.interface(),
                                                              harness.config());
    transaction.configuration().infiniteStamina = !originalStamina;
    transaction.interface().setVisible(kue::ui::MenuVisibility::Hidden);
    kue::internalhud::MenuRasterFrame frame{};
    kue::internalhud::MenuFrameEffects effects{true, true};
    ImDrawData invalidData;
    const kue::internalhud::MenuRasterStatus status =
        transaction.commit(harness.renderer(), &invalidData, {600.f, 320.f, 720.f, 440.f},
                           {1920, 1080}, harness.interface(), harness.config(), frame, effects);
    run.expect(status == kue::internalhud::MenuRasterStatus::InvalidFrame,
               "a malformed candidate frame is rejected");
    run.expect(harness.config().infiniteStamina == originalStamina,
               "a rejected candidate cannot leak configuration state");
    run.expect(effects.configurationChanged && effects.explicitSaveRequested,
               "a rejected candidate cannot publish menu effects");

    const RenderResult retry = harness.render();
    run.expect(retry.status == kue::internalhud::MenuRasterStatus::Success,
               "the untouched live interface renders after rejection");
    run.expect(!retry.frame.pixels.empty(),
               "a rejected hidden candidate cannot hide the live interface");
}

void testSaveScheduleSeparation(TestRun& run) {
    using namespace std::chrono_literals;
    using Clock = kue::ConfigSaveSchedule::Clock;
    const Clock::time_point start{};
    kue::ConfigSaveSchedule schedule;
    schedule.requestAutosave(start);
    run.expect(!schedule.due(start),
               "an ordinary accepted edit does not request an immediate explicit save");
    run.expect(!schedule.due(start + 349ms),
               "ordinary edit persistence remains debounced before 350 milliseconds");
    run.expect(schedule.due(start + 350ms),
               "ordinary edit persistence becomes due at 350 milliseconds");

    kue::ConfigSaveSchedule explicitSchedule;
    explicitSchedule.requestExplicit();
    run.expect(explicitSchedule.due(start), "an explicit save remains immediately due");
}

void invalidateTexture(ImDrawData* data) {
    data->CmdLists[0]->CmdBuffer[0].TextureId = static_cast<ImTextureID>(2);
}

void testRejectedConfigurationAndTabState(TestRun& run) {
    {
        HudHarness harness;
        static_cast<void>(
            successfulRender(run, harness, {}, "the configuration rejection fixture initializes"));
        const bool initialStamina = harness.config().infiniteStamina;
        const RenderResult rejected = harness.render({700.f, 449.f, true, 0.f}, invalidateTexture);
        run.expect(rejected.status == kue::internalhud::MenuRasterStatus::UnsupportedTexture,
                   "a config edit with an unsupported raster texture is rejected");
        run.expect(harness.config().infiniteStamina == initialStamina,
                   "a rejected edit leaves the live configuration unchanged");
        run.expect(!rejected.effects.configurationChanged &&
                       !rejected.effects.explicitSaveRequested,
                   "a rejected edit publishes no persistence effect");

        harness.recreateContext();
        static_cast<void>(
            successfulRender(run, harness, {}, "the recreated configuration fixture initializes"));
        const RenderResult accepted = successfulRender(run, harness, {700.f, 449.f, true, 0.f},
                                                       "the retried configuration press renders");
        const RenderResult release = successfulRender(run, harness, {700.f, 449.f, false, 0.f},
                                                      "the retried configuration release renders");
        run.expect(accepted.effects.configurationChanged,
                   "the retried ordinary edit commits exactly once");
        run.expect(!accepted.effects.explicitSaveRequested,
                   "an ordinary edit does not masquerade as an explicit save");
        run.expect(!release.effects.configurationChanged && !release.effects.explicitSaveRequested,
                   "releasing the retried edit produces no duplicate effect");
        run.expect(harness.config().infiniteStamina != initialStamina,
                   "the accepted retry applies the candidate configuration");
        const auto filledWith = [](const std::string& value, char expected) {
            return value.size() == 96U &&
                   std::all_of(value.begin(), value.end(),
                               [expected](char actual) { return actual == expected; });
        };
        run.expect(filledWith(harness.config().fontPath, 'f') &&
                       filledWith(harness.config().logPath, 'l') &&
                       filledWith(harness.config().filePath, 'c'),
                   "menu commits preserve every non-editable configuration string");
    }
    {
        HudHarness harness;
        const RenderResult self =
            successfulRender(run, harness, {}, "the tab rejection fixture initializes");
        const std::uint64_t selfHash = pixelHash(self.frame);
        const RenderResult rejected = harness.render({1035.f, 382.f, true, 0.f}, invalidateTexture);
        run.expect(rejected.status == kue::internalhud::MenuRasterStatus::UnsupportedTexture,
                   "a tab change with an unsupported raster texture is rejected");
        harness.recreateContext();
        const RenderResult afterRejection =
            successfulRender(run, harness, {}, "the recreated tab rejection fixture initializes");
        run.expect(pixelHash(afterRejection.frame) == selfHash,
                   "a rejected tab change leaves the live tab observable unchanged");

        const RenderResult accepted = successfulRender(
            run, harness, {1035.f, 382.f, true, 0.f}, "the tab change succeeds on its first retry");
        static_cast<void>(successfulRender(run, harness, {1035.f, 382.f, false, 0.f},
                                           "the tab retry release renders"));
        run.expect(pixelHash(accepted.frame) != selfHash,
                   "the accepted tab retry changes the visible menu exactly once");
    }
}

void testRejectedActionAndSaveState(TestRun& run) {
    {
        HudHarness harness;
        static_cast<void>(
            successfulRender(run, harness, {}, "the action rejection fixture initializes"));
        static_cast<void>(successfulRender(run, harness, {760.f, 382.f, true, 0.f},
                                           "the players-tab setup press renders"));
        static_cast<void>(successfulRender(run, harness, {760.f, 382.f, false, 0.f},
                                           "the players-tab setup release renders"));
        static_cast<void>(successfulRender(run, harness, {1200.f, 661.f, true, 0.f},
                                           "the rejected action setup press renders"));
        const RenderResult rejected =
            harness.render({1200.f, 661.f, false, 0.f}, invalidateTexture);
        run.expect(rejected.status == kue::internalhud::MenuRasterStatus::UnsupportedTexture,
                   "an action click with an unsupported raster texture is rejected");
        kue::PlayerActionRequest action;
        run.expect(!harness.interface().readAction(action),
                   "a rejected action click cannot leak into the live action output");

        harness.recreateContext();
        static_cast<void>(successfulRender(run, harness, {},
                                           "the recreated action rejection fixture initializes"));
        static_cast<void>(successfulRender(run, harness, {1200.f, 661.f, true, 0.f},
                                           "the retried action setup press renders"));
        static_cast<void>(successfulRender(run, harness, {1200.f, 661.f, false, 0.f},
                                           "the rejected action succeeds on its first retry"));
        run.expect(harness.interface().readAction(action) &&
                       action.action == kue::PlayerAction::Kill,
                   "the accepted retry publishes the selected action exactly once");
        harness.interface().applyActionEnqueueResult(kue::PlayerActionEnqueueResult::Queued);
        run.expect(!harness.interface().readAction(action),
                   "one successful enqueue consumes the retried action");
    }
    {
        HudHarness harness;
        static_cast<void>(
            successfulRender(run, harness, {}, "the save rejection fixture initializes"));
        static_cast<void>(successfulRender(run, harness, {1035.f, 382.f, true, 0.f},
                                           "the settings-tab setup press renders"));
        static_cast<void>(successfulRender(run, harness, {1035.f, 382.f, false, 0.f},
                                           "the settings-tab setup release renders"));
        static_cast<void>(successfulRender(run, harness, {670.f, 611.f, true, 0.f},
                                           "the rejected save setup press renders"));
        const RenderResult rejected = harness.render({670.f, 611.f, false, 0.f}, invalidateTexture);
        run.expect(rejected.status == kue::internalhud::MenuRasterStatus::UnsupportedTexture,
                   "an explicit-save click with an unsupported raster texture is rejected");
        run.expect(!rejected.effects.explicitSaveRequested,
                   "a rejected explicit-save click publishes no save request");
        run.expect(!harness.interface().consumeSaveRequest(),
                   "a rejected explicit-save click cannot leak into the live interface");

        harness.recreateContext();
        static_cast<void>(
            successfulRender(run, harness, {}, "the recreated save rejection fixture initializes"));
        static_cast<void>(successfulRender(run, harness, {670.f, 611.f, true, 0.f},
                                           "the retried save setup press renders"));
        const RenderResult accepted =
            successfulRender(run, harness, {670.f, 611.f, false, 0.f},
                             "the rejected save succeeds on its first retry");
        const RenderResult idle =
            successfulRender(run, harness, {}, "the accepted save idle frame renders");
        run.expect(accepted.effects.explicitSaveRequested,
                   "the explicit-save request succeeds on its first retry");
        run.expect(!accepted.effects.configurationChanged,
                   "explicit Save does not claim a configuration edit");
        run.expect(!idle.effects.explicitSaveRequested,
                   "the accepted explicit-save request is consumed exactly once");
    }
}

void testSteadyRenderPerformance(TestRun& run) {
    constexpr std::size_t measuredFrames = 10000;
    HudHarness harness;
    std::vector<double> microseconds;
    microseconds.reserve(measuredFrames);
    for (int warmup = 0; warmup < 16; ++warmup) {
        const RenderResult frame = harness.render();
        run.expect(frame.status == kue::internalhud::MenuRasterStatus::Success,
                   "every performance warmup frame remains renderable");
    }

    const std::size_t allocationBaseline = gAllocations.load(std::memory_order_relaxed);
    const std::size_t allocatedByteBaseline = gAllocatedBytes.load(std::memory_order_relaxed);
    for (std::size_t frameIndex = 0; frameIndex < measuredFrames; ++frameIndex) {
        const auto start = std::chrono::steady_clock::now();
        const RenderResult frame = harness.render();
        const auto finish = std::chrono::steady_clock::now();
        if (frame.status != kue::internalhud::MenuRasterStatus::Success) {
            run.expect(false, "every measured frame remains renderable");
            return;
        }
        microseconds.push_back(std::chrono::duration<double, std::micro>(finish - start).count());
    }
    const std::size_t allocations =
        gAllocations.load(std::memory_order_relaxed) - allocationBaseline;
    const std::size_t allocatedBytes =
        gAllocatedBytes.load(std::memory_order_relaxed) - allocatedByteBaseline;
    std::sort(microseconds.begin(), microseconds.end());
    const auto percentile = [&](std::size_t numerator) {
        const std::size_t index = measuredFrames * numerator / 100U;
        return microseconds[std::min(index, microseconds.size() - 1U)];
    };
    const double p95 = percentile(95U);
    const double p99 = percentile(99U);
    std::cout << "render p95_us=" << p95 << " p99_us=" << p99
              << " steady_allocations=" << allocations
              << " steady_allocated_bytes=" << allocatedBytes << '\n';
    run.expect(allocations == 0U, "steady unchanged rendering performs zero allocations");
    run.expect(allocatedBytes == 0U, "steady unchanged rendering allocates zero bytes");
    run.expect(p95 < 4000.0, "steady render p95 remains below four milliseconds");
    run.expect(p99 < 8000.0, "steady render p99 remains below eight milliseconds");
}

}

int main() {
    ImGui::SetAllocatorFunctions(&allocateImGui, &releaseImGui);
    TestRun run;
    testExactPixelsAndConsumerRecovery(run);
    testFontBoundaryValidation(run);
    testPlayerListLabelBoundary(run);
    testMalformedDrawData(run);
    testViewportContract(run);
    testRejectedStateIsTransactional(run);
    testSaveScheduleSeparation(run);
    testRejectedConfigurationAndTabState(run);
    testRejectedActionAndSaveState(run);
    if (run.failures() == 0)
        testSteadyRenderPerformance(run);
    return run.result();
}
