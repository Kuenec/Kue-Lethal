#include "overlay/Menu.h"

#include "core/Log.h"
#include "overlay/Theme.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>

namespace kue::ui {

namespace {

constexpr float kMenuWidth = 720.f;
constexpr float kMenuHeight = 440.f;

struct RgbaColor {
    ImU32 red;
    ImU32 green;
    ImU32 blue;
    ImU32 alpha;
};

template <typename T> struct SliderRange {
    T minimum;
    T maximum;
};

constexpr ImU32 rgba(RgbaColor color) noexcept {
    return (color.red << IM_COL32_R_SHIFT) | (color.green << IM_COL32_G_SHIFT) |
           (color.blue << IM_COL32_B_SHIFT) | (color.alpha << IM_COL32_A_SHIFT);
}

constexpr ImU32 kAccent = rgba({.red = 204u, .green = 46u, .blue = 56u, .alpha = 255u});
constexpr ImU32 kText = rgba({.red = 230u, .green = 230u, .blue = 235u, .alpha = 255u});
constexpr ImU32 kRowBg = rgba({.red = 24u, .green = 24u, .blue = 27u, .alpha = 255u});
constexpr ImU32 kRowHover = rgba({.red = 32u, .green = 32u, .blue = 36u, .alpha = 255u});

struct ActionButtonDefinition {
    const char* label;
    PlayerAction action;
};

constexpr std::array<ActionButtonDefinition, 20> kClientTrollActions{{
    {"Toggle Ship Horn", PlayerAction::ToggleShipHorn},
    {"Toggle Cruiser Horn", PlayerAction::ToggleCarHorn},
    {"Toggle Terminal Sound Spam", PlayerAction::ToggleTerminalSound},
    {"Toggle Ship Lights", PlayerAction::ToggleShipLights},
    {"Force Small Bridge Fall", PlayerAction::ForceSmallBridgeFall},
    {"Blow Up All Landmines", PlayerAction::BlowUpAllMines},
    {"Toggle All Landmines", PlayerAction::ToggleAllMines},
    {"Toggle All Turrets", PlayerAction::ToggleAllTurrets},
    {"Toggle Berserk Turrets", PlayerAction::BerserkAllTurrets},
    {"Toggle Open Ship Door In Space", PlayerAction::OpenShipDoorSpace},
    {"Force Company Tentacle Attack", PlayerAction::ForceTentacleAttack},
    {"Spawn Masked From Masks", PlayerAction::SpawnMaskedEnemy},
    {"Toggle Mineshaft Elevator", PlayerAction::ToggleMineshaftElevator},
    {"Toggle Vehicle Magnet", PlayerAction::ToggleVehicleMagnet},
    {"Shoot All Shotguns", PlayerAction::ShootAllShotguns},
    {"Toggle Shotgun Spam", PlayerAction::ToggleShotgunSpam},
    {"Explode Cruiser", PlayerAction::ExplodeCruiser},
    {"Toggle Slide Taunt", PlayerAction::SlideTaunt},
    {"Explode All Jetpacks", PlayerAction::ExplodeAllJetpacks},
    {"Toggle Explode Jetpacks On Grab", PlayerAction::ToggleExplodeJetpacksOnGrab},
}};

constexpr std::array<ActionButtonDefinition, 5> kHostTrollActions{{
    {"Toggle Deposit Desk Sound Spam", PlayerAction::ToggleDepositDeskSound},
    {"Toggle Factory Lights", PlayerAction::ToggleFactoryLights},
    {"Force Main Bridge Fall", PlayerAction::ForceBridgeFall},
    {"Force Eject / Fire Everyone", PlayerAction::EjectEveryone},
    {"Spawn Hoarding Bug Infestation", PlayerAction::SpawnHoardingBugInfestation},
}};

constexpr bool trollActionsHaveNoPayload() noexcept {
    for (const ActionButtonDefinition& definition : kClientTrollActions)
        if (playerActionPayloadKind(definition.action) != PlayerActionPayloadKind::None)
            return false;
    for (const ActionButtonDefinition& definition : kHostTrollActions)
        if (playerActionPayloadKind(definition.action) != PlayerActionPayloadKind::None)
            return false;
    return true;
}

static_assert(trollActionsHaveNoPayload());

struct MenuKeyBinding {
    ImGuiKey input;
    MenuKey key;
    const char* name;
};

constexpr std::array<MenuKeyBinding, 52> kMenuKeyBindings = {{
    {ImGuiKey_Insert, MenuKey::Insert, "Insert"},
    {ImGuiKey_Tab, MenuKey::Tab, "Tab"},
    {ImGuiKey_Enter, MenuKey::Enter, "Enter"},
    {ImGuiKey_Space, MenuKey::Space, "Space"},
    {ImGuiKey_F1, MenuKey::F1, "F1"},
    {ImGuiKey_F2, MenuKey::F2, "F2"},
    {ImGuiKey_F3, MenuKey::F3, "F3"},
    {ImGuiKey_F4, MenuKey::F4, "F4"},
    {ImGuiKey_F5, MenuKey::F5, "F5"},
    {ImGuiKey_F6, MenuKey::F6, "F6"},
    {ImGuiKey_F7, MenuKey::F7, "F7"},
    {ImGuiKey_F8, MenuKey::F8, "F8"},
    {ImGuiKey_F9, MenuKey::F9, "F9"},
    {ImGuiKey_F10, MenuKey::F10, "F10"},
    {ImGuiKey_F11, MenuKey::F11, "F11"},
    {ImGuiKey_F12, MenuKey::F12, "F12"},
    {ImGuiKey_A, MenuKey::A, "A"},
    {ImGuiKey_B, MenuKey::B, "B"},
    {ImGuiKey_C, MenuKey::C, "C"},
    {ImGuiKey_D, MenuKey::D, "D"},
    {ImGuiKey_E, MenuKey::E, "E"},
    {ImGuiKey_F, MenuKey::F, "F"},
    {ImGuiKey_G, MenuKey::G, "G"},
    {ImGuiKey_H, MenuKey::H, "H"},
    {ImGuiKey_I, MenuKey::I, "I"},
    {ImGuiKey_J, MenuKey::J, "J"},
    {ImGuiKey_K, MenuKey::K, "K"},
    {ImGuiKey_L, MenuKey::L, "L"},
    {ImGuiKey_M, MenuKey::M, "M"},
    {ImGuiKey_N, MenuKey::N, "N"},
    {ImGuiKey_O, MenuKey::O, "O"},
    {ImGuiKey_P, MenuKey::P, "P"},
    {ImGuiKey_Q, MenuKey::Q, "Q"},
    {ImGuiKey_R, MenuKey::R, "R"},
    {ImGuiKey_S, MenuKey::S, "S"},
    {ImGuiKey_T, MenuKey::T, "T"},
    {ImGuiKey_U, MenuKey::U, "U"},
    {ImGuiKey_V, MenuKey::V, "V"},
    {ImGuiKey_W, MenuKey::W, "W"},
    {ImGuiKey_X, MenuKey::X, "X"},
    {ImGuiKey_Y, MenuKey::Y, "Y"},
    {ImGuiKey_Z, MenuKey::Z, "Z"},
    {ImGuiKey_0, MenuKey::Digit0, "0"},
    {ImGuiKey_1, MenuKey::Digit1, "1"},
    {ImGuiKey_2, MenuKey::Digit2, "2"},
    {ImGuiKey_3, MenuKey::Digit3, "3"},
    {ImGuiKey_4, MenuKey::Digit4, "4"},
    {ImGuiKey_5, MenuKey::Digit5, "5"},
    {ImGuiKey_6, MenuKey::Digit6, "6"},
    {ImGuiKey_7, MenuKey::Digit7, "7"},
    {ImGuiKey_8, MenuKey::Digit8, "8"},
    {ImGuiKey_9, MenuKey::Digit9, "9"},
}};

const char* menuKeyName(MenuKey key) {
    for (const MenuKeyBinding& binding : kMenuKeyBindings) {
        if (binding.key == key)
            return binding.name;
    }
    return "Key";
}

bool translateMenuKey(ImGuiKey input, MenuKey& output) {
    for (const MenuKeyBinding& binding : kMenuKeyBindings) {
        if (binding.input == input) {
            output = binding.key;
            return true;
        }
    }
    return false;
}

bool toggleRow(const char* label, bool& value) {
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    ImGui::InvisibleButton("##row", ImVec2(availableWidth, 28.f));
    const bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        value = !value;
        changed = true;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 rowMinimum = ImGui::GetItemRectMin();
    const ImVec2 rowMaximum = ImGui::GetItemRectMax();
    drawList->AddRectFilled(rowMinimum, rowMaximum, hovered ? kRowHover : kRowBg, 4.f);
    drawList->AddText(ImVec2(rowMinimum.x + 10.f, rowMinimum.y + 6.f), kText, label);
    const ImVec2 toggleSize(34.f, 18.f);
    const ImVec2 togglePosition(rowMaximum.x - 44.f, rowMinimum.y + 5.f);
    const float radius = toggleSize.y * 0.5f;
    drawList->AddRectFilled(
        togglePosition, ImVec2(togglePosition.x + toggleSize.x, togglePosition.y + toggleSize.y),
        value ? kAccent : rgba({.red = 48u, .green = 48u, .blue = 54u, .alpha = 255u}), radius);
    const float knobCenterX =
        value ? togglePosition.x + toggleSize.x - radius : togglePosition.x + radius;
    drawList->AddCircleFilled(ImVec2(knobCenterX, togglePosition.y + radius), radius - 2.5f,
                              rgba({.red = 240u, .green = 240u, .blue = 245u, .alpha = 255u}));
    ImGui::Dummy(ImVec2(0.f, 4.f));
    ImGui::PopID();
    return changed;
}

void sectionLabel(const char* label) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.18f, 0.22f, 1.f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

bool actionButton(const char* label, float width = -1.f) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.18f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.26f, 0.30f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.12f, 0.16f, 1.f));
    const bool pressed = ImGui::Button(label, ImVec2(width, 0.f));
    ImGui::PopStyleColor(3);
    return pressed;
}

bool boundedSlider(const char* label, int& value, SliderRange<int> range, const char* format) {
    return ImGui::SliderInt(label, &value, range.minimum, range.maximum, format,
                            ImGuiSliderFlags_NoInput);
}

bool boundedSlider(const char* label, float& value, SliderRange<float> range, const char* format) {
    return ImGui::SliderFloat(label, &value, range.minimum, range.maximum, format,
                              ImGuiSliderFlags_NoInput);
}

}

MenuConfiguration menuConfiguration(const Config& config) noexcept {
    return {config.esp,
            config.menuKey,
            config.pollRate,
            config.tickIntervalMs,
            config.infiniteStamina,
            config.noWeight,
            config.infiniteBattery,
            config.extendedInventory};
}

void applyMenuConfiguration(Config& config, const MenuConfiguration& menu) noexcept {
    config.esp = menu.esp;
    config.menuKey = menu.menuKey;
    config.pollRate = menu.pollRate;
    config.tickIntervalMs = menu.tickIntervalMs;
    config.infiniteStamina = menu.infiniteStamina;
    config.noWeight = menu.noWeight;
    config.infiniteBattery = menu.infiniteBattery;
    config.extendedInventory = menu.extendedInventory;
}

std::array<char, kPlayerListLabelCapacity>
playerListLabel(const PlayerSnapshotPlayer& player) noexcept {
    constexpr std::string_view localSuffix = " (you)";
    std::array<char, kPlayerListLabelCapacity> label{};
    const std::string_view name = player.name.view();
    std::memcpy(label.data(), name.data(), name.size());
    std::size_t labelBytes = name.size();
    if (player.local) {
        std::memcpy(label.data() + labelBytes, localSuffix.data(), localSuffix.size());
        labelBytes += localSuffix.size();
    }
    label[labelBytes] = '\0';
    return label;
}

bool Interface::readAction(PlayerActionRequest& output) const {
    return mPendingAction.read(output);
}

void Interface::applyActionEnqueueResult(PlayerActionEnqueueResult result) noexcept {
    mPendingAction.applyEnqueueResult(result);
}

void Interface::queueAction(PlayerActionRequest request) {
    const int action = static_cast<int>(request.action);
    switch (mPendingAction.capture(request)) {
    case PlayerActionCaptureResult::Captured:
        return;
    case PlayerActionCaptureResult::InvalidRequest:
        KUE_ERR("menu rejected invalid action %d", action);
        return;
    case PlayerActionCaptureResult::Occupied:
        KUE_ERR("menu rejected action %d because its one-entry output is occupied", action);
        return;
    }
}

void Interface::setVisible(MenuVisibility visibility) {
    mVisibility = visibility;
}

MenuRectangle Interface::menuRectangle() const {
    return {mMenuLeft, mMenuTop, mMenuWidth, mMenuHeight};
}

bool Interface::consumeConfigChange() {
    const bool changed = mConfigurationChanged;
    mConfigurationChanged = false;
    return changed;
}

bool Interface::consumeSaveRequest() {
    const bool requested = mConfigurationSaveRequested;
    mConfigurationSaveRequested = false;
    return requested;
}

void Interface::markConfigChanged() {
    mConfigurationChanged = true;
}

void Interface::requestConfigSave() {
    mConfigurationSaveRequested = true;
}

void Interface::render(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs,
                       const MenuFrame& frame, std::string_view runtimeStatus,
                       MenuConfiguration& configuration) {
    if (mVisibility == MenuVisibility::Hidden)
        return;

    ImFont* const bodyFont = fontBody();
    if (bodyFont)
        ImGui::PushFont(bodyFont);

    if (!mMenuPositionInitialized || frame.width != mLastWindowWidth ||
        frame.height != mLastWindowHeight) {
        mMenuLeft = std::max(40.f, (static_cast<float>(frame.width) - kMenuWidth) * 0.5f);
        mMenuTop = std::max(40.f, (static_cast<float>(frame.height) - kMenuHeight) * 0.5f);
        mMenuPositionInitialized = true;
        mLastWindowWidth = frame.width;
        mLastWindowHeight = frame.height;
    }

    mMenuLeft =
        std::clamp(mMenuLeft, 0.f, std::max(0.f, static_cast<float>(frame.width) - kMenuWidth));
    mMenuTop =
        std::clamp(mMenuTop, 0.f, std::max(0.f, static_cast<float>(frame.height) - kMenuHeight));

    ImGui::SetNextWindowPos(ImVec2(mMenuLeft, mMenuTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kMenuWidth, kMenuHeight), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.035f, 0.038f, 0.97f));
    const bool windowVisible = ImGui::Begin(
        "##kue_menu", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    {
        const ImVec2 windowPosition = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        mMenuLeft = windowPosition.x;
        mMenuTop = windowPosition.y;
        mMenuWidth = windowSize.x;
        mMenuHeight = windowSize.y;
        if (windowVisible) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRect(
                windowPosition,
                ImVec2(windowPosition.x + windowSize.x, windowPosition.y + windowSize.y),
                rgba({.red = 50u, .green = 50u, .blue = 56u, .alpha = 255u}), 8.f, 0, 1.5f);
        }
    }

    if (!windowVisible) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (bodyFont)
            ImGui::PopFont();
        return;
    }

    const ImVec2 headerPos = ImGui::GetCursorScreenPos();
    ImFont* const titleFont = fontTitle();
    if (titleFont)
        ImGui::PushFont(titleFont);
    ImGui::TextColored(ImVec4(0.91f, 0.91f, 0.93f, 1.f), "Kue");
    if (titleFont)
        ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
    ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f), "Lethal Menu");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.f);
    ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f), "%.0f fps",
                       static_cast<double>(frame.framesPerSecond));
    ImGui::SetCursorScreenPos(headerPos);
    ImGui::InvisibleButton("##drag_handle", ImVec2(ImGui::GetWindowWidth() - 24.f, 32.f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
        mMenuLeft += mouseDelta.x;
        mMenuTop += mouseDelta.y;
        mMenuLeft =
            std::clamp(mMenuLeft, 0.f, std::max(0.f, static_cast<float>(frame.width) - mMenuWidth));
        mMenuTop = std::clamp(mMenuTop, 0.f,
                              std::max(0.f, static_cast<float>(frame.height) - mMenuHeight));
        ImGui::SetWindowPos(ImVec2(mMenuLeft, mMenuTop));
    }
    ImGui::SetCursorScreenPos(ImVec2(headerPos.x, headerPos.y + 32.f));

    ImGui::Spacing();
    drawTabs();
    ImGui::Separator();
    ImGui::Spacing();

    const float footerHeight = 26.f;
    const float bodyHeight = ImGui::GetContentRegionAvail().y - footerHeight;
    const bool tabBodyVisible = ImGui::BeginChild(
        "##tab_body", ImVec2(0.f, std::max(80.f, bodyHeight)), ImGuiChildFlags_None);
    if (tabBodyVisible) {
        switch (mActiveTab) {
        case MenuTab::Self:
            drawSelfTab(snapshot, configuration);
            break;
        case MenuTab::Visuals:
            drawVisualsTab(configuration);
            break;
        case MenuTab::Players:
            drawPlayersTab(snapshot);
            break;
        case MenuTab::Enemies:
            drawEnemiesTab(snapshot, catalogs);
            break;
        case MenuTab::Items:
            drawItemsTab(snapshot, catalogs);
            break;
        case MenuTab::Trolls:
            drawTrollsTab(snapshot);
            break;
        case MenuTab::Settings:
            drawSettingsTab(configuration);
            break;
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    const std::size_t maximumStatusLength =
        static_cast<std::size_t>(std::numeric_limits<int>::max());
    const int statusLength = static_cast<int>(std::min(runtimeStatus.size(), maximumStatusLength));
    const char* statusData = runtimeStatus.empty() ? "" : runtimeStatus.data();
    ImGui::TextColored(ImVec4(0.40f, 0.40f, 0.44f, 1.f), "%.*s  |  %s toggle  Esc close",
                       statusLength, statusData, menuKeyName(configuration.menuKey));

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (bodyFont)
        ImGui::PopFont();
}

void Interface::drawTabs() {
    drawTabButton("Self", MenuTab::Self);
    ImGui::SameLine();
    drawTabButton("Visuals", MenuTab::Visuals);
    ImGui::SameLine();
    drawTabButton("Players", MenuTab::Players);
    ImGui::SameLine();
    drawTabButton("Enemies", MenuTab::Enemies);
    ImGui::SameLine();
    drawTabButton("Items", MenuTab::Items);
    ImGui::SameLine();
    drawTabButton("Trolls", MenuTab::Trolls);
    ImGui::SameLine();
    drawTabButton("Settings", MenuTab::Settings);
}

void Interface::drawTabButton(const char* label, MenuTab tab) {
    const bool selected = mActiveTab == tab;
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.80f, 0.18f, 0.22f, 1.f)
                                                    : ImVec4(0.10f, 0.10f, 0.12f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.90f, 0.26f, 0.30f, 1.f)
                                                           : ImVec4(0.16f, 0.16f, 0.18f, 1.f));
    if (ImGui::Button(label, ImVec2(0.f, 28.f)))
        mActiveTab = tab;
    ImGui::PopStyleColor(2);
}

void Interface::drawSelfTab(const MenuSnapshot& snapshot, MenuConfiguration& configuration) {
    sectionLabel("SELF CHEATS");
    if (toggleRow("Infinite Stamina", configuration.infiniteStamina))
        markConfigChanged();
    if (toggleRow("No Weight", configuration.noWeight))
        markConfigChanged();
    if (toggleRow("Infinite Flashlight / Battery", configuration.infiniteBattery))
        markConfigChanged();
    if (toggleRow("Infinite / Expanding Inventory", configuration.extendedInventory))
        markConfigChanged();

    sectionLabel("LIVE");
    ImGui::Text("Stamina  %.0f%%", static_cast<double>(snapshot.playerFrame.sprintMeter * 100.f));
    ImGui::ProgressBar(std::clamp(snapshot.playerFrame.sprintMeter, 0.f, 1.f), ImVec2(-1.f, 6.f),
                       "");
    const float extraWeight = std::max(0.f, snapshot.playerFrame.carryWeight - 1.f);
    ImGui::Text("Extra weight  %.2f", static_cast<double>(extraWeight));
    ImGui::ProgressBar(std::clamp(extraWeight / 2.f, 0.f, 1.f), ImVec2(-1.f, 6.f), "");

    sectionLabel("ACTIONS");
    if (actionButton(snapshot.flyEnabled ? "Disable Fly" : "Enable Fly", -1.f))
        queueAction({PlayerAction::ToggleFly, NoPlayerActionPayload{}});
    if (actionButton("Kill Yourself", -1.f)) {
        std::optional<std::uint64_t> selfClientId;
        for (const PlayerSnapshotPlayer& player : snapshot.playerFrame.players) {
            if (player.local) {
                selfClientId = player.clientId;
                break;
            }
        }
        if (selfClientId) {
            queueAction({PlayerAction::Kill, PlayerTargetPayload{*selfClientId}});
        } else {
            KUE_ERR("menu cannot queue self-kill because no local player is available");
        }
    }
}

void Interface::drawEnemiesTab(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    const float listWidth = 280.f;
    const std::size_t enemyCount = catalogs.enemyCount();
    if (mSelectedEnemyType >= enemyCount)
        mSelectedEnemyType = enemyCount == 0 ? 0 : enemyCount - 1;
    bool anyActive = false;
    for (std::size_t index = 0; index < enemyCount; ++index) {
        const EnemyCatalogLookup entry = catalogs.enemyAt(index);
        anyActive = anyActive ||
                    (entry.result == CatalogLookupResult::Found && entry.entry.activeCount > 0);
    }
    const EnemyCatalogLookup selectedEnemy = catalogs.enemyAt(mSelectedEnemyType);
    bool selectedAvailable =
        selectedEnemy.result == CatalogLookupResult::Found && selectedEnemy.entry.activeCount > 0;
    if (!snapshot.localIsHost && anyActive && !selectedAvailable) {
        for (std::size_t index = 0; index < enemyCount; ++index) {
            const EnemyCatalogLookup entry = catalogs.enemyAt(index);
            if (entry.result == CatalogLookupResult::Found && entry.entry.activeCount > 0) {
                mSelectedEnemyType = index;
                selectedAvailable = true;
                break;
            }
        }
    }

    const bool enemyTypesVisible =
        ImGui::BeginChild("##enemy_types", ImVec2(listWidth, 0.f), ImGuiChildFlags_Borders);
    if (enemyTypesVisible) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.f), "Available Now");
        ImGui::Separator();
        std::array<char, kRuntimeCatalogNameCapacity + 32> activeLabel{};
        std::array<char, kRuntimeCatalogNameCapacity + 1> catalogLabel{};
        for (std::size_t index = 0; index < enemyCount; ++index) {
            const EnemyCatalogLookup entry = catalogs.enemyAt(index);
            if (entry.result != CatalogLookupResult::Found || entry.entry.activeCount == 0)
                continue;
            std::snprintf(activeLabel.data(), activeLabel.size(), "%.*s  (x%u)",
                          static_cast<int>(entry.entry.name.size()), entry.entry.name.data(),
                          entry.entry.activeCount);
            ImGui::PushID(static_cast<int>(index) + 10000);
            if (ImGui::Selectable(activeLabel.data(), mSelectedEnemyType == index))
                mSelectedEnemyType = index;
            ImGui::PopID();
        }
        if (!anyActive)
            ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f),
                               "No living enemies currently loaded");
        if (snapshot.localIsHost) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.80f, 0.18f, 0.22f, 1.f), "Host Spawn Catalog");
            ImGui::Separator();
            if (enemyCount == 0)
                ImGui::TextWrapped("Enemy assets are discovered after reaching a lobby or moon.");
            for (std::size_t index = 0; index < enemyCount; ++index) {
                const EnemyCatalogLookup entry = catalogs.enemyAt(index);
                if (entry.result != CatalogLookupResult::Found)
                    continue;
                std::snprintf(catalogLabel.data(), catalogLabel.size(), "%.*s",
                              static_cast<int>(entry.entry.name.size()), entry.entry.name.data());
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(catalogLabel.data(), mSelectedEnemyType == index))
                    mSelectedEnemyType = index;
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    const bool enemyActionsVisible =
        ImGui::BeginChild("##enemy_actions", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
    if (!enemyActionsVisible) {
        ImGui::EndChild();
        return;
    }
    sectionLabel(snapshot.localIsHost ? "SESSION: HOST" : "SESSION: CLIENT");
    const EnemyCatalogLookup chosenEnemy = catalogs.enemyAt(mSelectedEnemyType);
    if (chosenEnemy.result == CatalogLookupResult::Found) {
        ImGui::TextWrapped("%.*s", static_cast<int>(chosenEnemy.entry.name.size()),
                           chosenEnemy.entry.name.data());
        ImGui::SetNextItemWidth(-1.f);
        boundedSlider("##spawn_count", mEnemySpawnCount, {.minimum = 1, .maximum = 20}, "Count %d");
        if (mSelectedSpawnPlayer >= snapshot.playerFrame.players.size())
            mSelectedSpawnPlayer =
                snapshot.playerFrame.players.empty() ? 0 : snapshot.playerFrame.players.size() - 1;
        if (!snapshot.playerFrame.players.empty()) {
            const PlayerSnapshotPlayer& selected =
                snapshot.playerFrame.players[mSelectedSpawnPlayer];
            const char* preview = selected.local ? "You" : selected.name.c_str();
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::BeginCombo("##spawn_target", preview)) {
                for (std::size_t index = 0; index < snapshot.playerFrame.players.size(); ++index) {
                    const PlayerSnapshotPlayer& player = snapshot.playerFrame.players[index];
                    const bool chosen = index == mSelectedSpawnPlayer;
                    const char* name = player.local ? "You" : player.name.c_str();
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(name, chosen))
                        mSelectedSpawnPlayer = index;
                    if (chosen)
                        ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            sectionLabel("WORKS AS CLIENT - EXISTING ENEMIES");
            ImGui::BeginDisabled(!selectedAvailable);
            if (actionButton("Summon Existing Type To Player", -1.f)) {
                queueAction({PlayerAction::SummonEnemyTypeAtPlayer,
                             EnemyAtPlayerPayload{
                                 chosenEnemy.entry.id, static_cast<std::uint8_t>(mEnemySpawnCount),
                                 mSpawnOutside ? EnemySpawnArea::Outside : EnemySpawnArea::Inside,
                                 selected.clientId}});
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Moves only living enemies shown under Available Now.");
            if (!selectedAvailable)
                ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.68f, 1.f),
                                   "No selected living enemy is available to summon");

            sectionLabel("HOST ONLY - CREATE NEW ENEMIES");
            if (!snapshot.localIsHost)
                ImGui::TextColored(ImVec4(0.85f, 0.30f, 0.30f, 1.f),
                                   "Disabled: the remote host owns enemy creation");
            ImGui::BeginDisabled(!snapshot.localIsHost);
            ImGui::Checkbox("Spawn outside", &mSpawnOutside);
            if (actionButton("Spawn Selected Enemy", -1.f)) {
                queueAction({PlayerAction::SpawnEnemy,
                             EnemySpawnPayload{chosenEnemy.entry.id,
                                               static_cast<std::uint8_t>(mEnemySpawnCount),
                                               mSpawnOutside ? EnemySpawnArea::Outside
                                                             : EnemySpawnArea::Inside}});
            }
            if (actionButton("Spawn At Selected Player", -1.f)) {
                queueAction({PlayerAction::SpawnEnemyAtPlayer,
                             EnemyAtPlayerPayload{
                                 chosenEnemy.entry.id, static_cast<std::uint8_t>(mEnemySpawnCount),
                                 mSpawnOutside ? EnemySpawnArea::Outside : EnemySpawnArea::Inside,
                                 selected.clientId}});
            }
            ImGui::EndDisabled();
        }
    } else {
        ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f), "No catalog yet");
    }
    sectionLabel("CLIENT/HOST - ALL ACTIVE ENEMIES");
    if (actionButton("Kill All Enemies", -1.f))
        queueAction({PlayerAction::KillAllEnemies, NoPlayerActionPayload{}});
    if (actionButton("Stun All Enemies", -1.f))
        queueAction({PlayerAction::StunAllEnemies, NoPlayerActionPayload{}});
    ImGui::EndChild();
}

void Interface::drawItemsTab(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    const float listWidth = 280.f;
    const std::size_t itemCount = catalogs.itemCount();
    if (mSelectedItemType >= itemCount)
        mSelectedItemType = itemCount == 0 ? 0 : itemCount - 1;
    const bool itemTypesVisible =
        ImGui::BeginChild("##item_types", ImVec2(listWidth, 0.f), ImGuiChildFlags_Borders);
    if (itemTypesVisible) {
        ImGui::TextColored(ImVec4(0.80f, 0.18f, 0.22f, 1.f), "Installed Item Types");
        ImGui::Separator();
        if (itemCount == 0)
            ImGui::TextWrapped("Waiting for the installed item catalog...");
        std::array<char, kRuntimeCatalogNameCapacity + 1> catalogLabel{};
        for (std::size_t index = 0; index < itemCount; ++index) {
            const ItemCatalogLookup entry = catalogs.itemAt(index);
            if (entry.result != CatalogLookupResult::Found)
                continue;
            std::snprintf(catalogLabel.data(), catalogLabel.size(), "%.*s",
                          static_cast<int>(entry.entry.name.size()), entry.entry.name.data());
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(catalogLabel.data(), mSelectedItemType == index))
                mSelectedItemType = index;
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    const bool itemActionsVisible =
        ImGui::BeginChild("##item_actions", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
    if (!itemActionsVisible) {
        ImGui::EndChild();
        return;
    }
    sectionLabel("TARGET PLAYER");
    if (mSelectedItemPlayer >= snapshot.playerFrame.players.size())
        mSelectedItemPlayer =
            snapshot.playerFrame.players.empty() ? 0 : snapshot.playerFrame.players.size() - 1;
    if (!snapshot.playerFrame.players.empty()) {
        const PlayerSnapshotPlayer& selected = snapshot.playerFrame.players[mSelectedItemPlayer];
        const char* preview = selected.local ? "You" : selected.name.c_str();
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::BeginCombo("##item_target", preview)) {
            for (std::size_t index = 0; index < snapshot.playerFrame.players.size(); ++index) {
                const PlayerSnapshotPlayer& player = snapshot.playerFrame.players[index];
                const bool chosen = index == mSelectedItemPlayer;
                const char* name = player.local ? "You" : player.name.c_str();
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(name, chosen))
                    mSelectedItemPlayer = index;
                if (chosen)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        sectionLabel("HOST ONLY - SPAWN NEW ITEM");
        if (!snapshot.localIsHost)
            ImGui::TextColored(ImVec4(0.85f, 0.30f, 0.30f, 1.f),
                               "Disabled: the remote host owns item creation");
        ImGui::BeginDisabled(!snapshot.localIsHost);
        ImGui::SetNextItemWidth(-1.f);
        boundedSlider("##item_count", mItemSpawnCount, {.minimum = 1, .maximum = 20}, "Count %d");
        const ItemCatalogLookup chosenItem = catalogs.itemAt(mSelectedItemType);
        if (chosenItem.result == CatalogLookupResult::Found &&
            actionButton("Spawn Selected Item At Player", -1.f)) {
            queueAction({PlayerAction::SpawnItemAtPlayer,
                         ItemAtPlayerPayload{chosenItem.entry.id,
                                             static_cast<std::uint8_t>(mItemSpawnCount),
                                             selected.clientId}});
        }
        ImGui::EndDisabled();
        sectionLabel("CLIENT/HOST - EXISTING LOADED ITEMS");
        if (actionButton("Teleport All Existing Items To Player", -1.f)) {
            queueAction(
                {PlayerAction::TeleportItemsToPlayer, PlayerTargetPayload{selected.clientId}});
        }
    } else {
        ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f), "No real player available");
    }
    ImGui::EndChild();
}

void Interface::drawTrollsTab(const MenuSnapshot& snapshot) {
    const bool trollActionsVisible =
        ImGui::BeginChild("##troll_scroll", ImVec2(0.f, 0.f), ImGuiChildFlags_None);
    if (!trollActionsVisible) {
        ImGui::EndChild();
        return;
    }
    sectionLabel("VERIFIED CLIENT-CAPABLE");
    const float buttonGap = ImGui::GetStyle().ItemSpacing.x;
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - buttonGap) * 0.5f;
    for (std::size_t index = 0; index < kClientTrollActions.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (actionButton(kClientTrollActions[index].label, buttonWidth)) {
            queueAction({kClientTrollActions[index].action, NoPlayerActionPayload{}});
        }
        ImGui::PopID();
        if ((index & 1U) == 0U && index + 1 < kClientTrollActions.size())
            ImGui::SameLine();
    }
    sectionLabel("HOST ONLY");
    if (!snapshot.localIsHost)
        ImGui::TextColored(ImVec4(0.85f, 0.30f, 0.30f, 1.f),
                           "Unavailable while connected as a client");
    ImGui::BeginDisabled(!snapshot.localIsHost);
    for (std::size_t index = 0; index < kHostTrollActions.size(); ++index) {
        ImGui::PushID(static_cast<int>(index) + 1000);
        if (actionButton(kHostTrollActions[index].label, buttonWidth)) {
            queueAction({kHostTrollActions[index].action, NoPlayerActionPayload{}});
        }
        ImGui::PopID();
        if ((index & 1U) == 0U && index + 1 < kHostTrollActions.size())
            ImGui::SameLine();
    }
    ImGui::EndDisabled();
    sectionLabel("SHIP PLUSHIE ANIMATION SPAM");
    ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f),
                       "PJ Man is the animated plushie decoration in the ship.");
    ImGui::SetNextItemWidth(-1.f);
    boundedSlider("##pj_speed", mPjSpamSpeed, {.minimum = 0.f, .maximum = 1.f}, "Interval %.2f s");
    if (actionButton("Toggle Ship Plushie Animation Spam", -1.f)) {
        queueAction({PlayerAction::TogglePjManSpam,
                     PlushieIntervalPayload{
                         std::chrono::milliseconds{static_cast<int>(mPjSpamSpeed * 1000.f)}}});
    }
    ImGui::EndChild();
}

void Interface::drawVisualsTab(MenuConfiguration& configuration) {
    sectionLabel("ESP");
    if (toggleRow("Player ESP", configuration.esp.players))
        markConfigChanged();
    if (toggleRow("Item ESP", configuration.esp.items))
        markConfigChanged();
    if (toggleRow("Monster ESP", configuration.esp.monsters))
        markConfigChanged();
    if (toggleRow("Entrance ESP", configuration.esp.exits))
        markConfigChanged();
    if (toggleRow("Fire Exit ESP", configuration.esp.fireExits))
        markConfigChanged();
    if (toggleRow("Ship ESP", configuration.esp.ships))
        markConfigChanged();

    sectionLabel("RENDER OPTIONS");
    const bool anyEspCategoryEnabled = configuration.esp.players || configuration.esp.items ||
                                       configuration.esp.monsters || configuration.esp.exits ||
                                       configuration.esp.fireExits || configuration.esp.ships;
    ImGui::BeginDisabled(!anyEspCategoryEnabled);
    if (ImGui::Checkbox("Outlines", &configuration.esp.outlines))
        markConfigChanged();
    if (ImGui::Checkbox("Names", &configuration.esp.names))
        markConfigChanged();
    if (ImGui::Checkbox("Values", &configuration.esp.values))
        markConfigChanged();
    if (ImGui::Checkbox("Distance", &configuration.esp.distance))
        markConfigChanged();
    if (ImGui::Checkbox("Lines", &configuration.esp.lines))
        markConfigChanged();
    if (ImGui::Checkbox("Use Scrap Tiers", &configuration.esp.useScrapTiers))
        markConfigChanged();
    if (ImGui::Checkbox("Death Notifications", &configuration.esp.deathNotifications))
        markConfigChanged();
    ImGui::SetNextItemWidth(-1.f);
    if (boundedSlider("##maxd", configuration.esp.maxDistance, {.minimum = 10.f, .maximum = 1000.f},
                      "Max %.0f m")) {
        markConfigChanged();
    }
    ImGui::EndDisabled();

    sectionLabel("COLORS");
    if (ImGui::ColorEdit4("Players", configuration.esp.colorPlayers.data(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        markConfigChanged();
    }
    if (ImGui::ColorEdit4("Items", configuration.esp.colorItems.data(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        markConfigChanged();
    }
    if (ImGui::ColorEdit4("Monsters", configuration.esp.colorMonsters.data(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        markConfigChanged();
    }
    if (ImGui::ColorEdit4("Entrances", configuration.esp.colorExits.data(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        markConfigChanged();
    }
    if (ImGui::ColorEdit4("Fire Exits", configuration.esp.colorFireExits.data(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        markConfigChanged();
    }
    if (ImGui::ColorEdit4("Ship", configuration.esp.colorShips.data(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        markConfigChanged();
    }
}

void Interface::drawPlayersTab(const MenuSnapshot& snapshot) {
    const float listWidth = 200.f;
    const bool playerListVisible =
        ImGui::BeginChild("##plist", ImVec2(listWidth, -36.f), ImGuiChildFlags_Borders);
    if (playerListVisible) {
        ImGui::TextColored(ImVec4(0.80f, 0.18f, 0.22f, 1.f), "Player List");
        ImGui::Separator();
        if (snapshot.playerFrame.players.empty()) {
            ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f), "No players yet");
        }
        for (std::size_t index = 0; index < snapshot.playerFrame.players.size(); ++index) {
            const PlayerSnapshotPlayer& player = snapshot.playerFrame.players[index];
            const bool selected = mSelectedPlayer == index;
            const std::array<char, kPlayerListLabelCapacity> label = playerListLabel(player);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  player.dead ? ImVec4(0.55f, 0.55f, 0.58f, 1.f)
                                              : (selected ? ImVec4(0.35f, 0.85f, 0.45f, 1.f)
                                                          : ImVec4(0.90f, 0.90f, 0.92f, 1.f)));
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.data(), selected))
                mSelectedPlayer = index;
            ImGui::PopID();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    const bool playerActionsVisible =
        ImGui::BeginChild("##pactions", ImVec2(0.f, -36.f), ImGuiChildFlags_Borders);
    if (!playerActionsVisible) {
        ImGui::EndChild();
        return;
    }
    if (mSelectedPlayer >= snapshot.playerFrame.players.size()) {
        ImGui::TextColored(ImVec4(0.48f, 0.48f, 0.52f, 1.f), "Select a player");
        ImGui::EndChild();
        return;
    }
    const PlayerSnapshotPlayer& player = snapshot.playerFrame.players[mSelectedPlayer];
    ImGui::TextColored(ImVec4(0.80f, 0.18f, 0.22f, 1.f), "%s", player.name.c_str());
    ImGui::Separator();

    sectionLabel("PLAYER INFORMATION");
    ImGui::Text("SteamID:  %llu", static_cast<unsigned long long>(player.steamId));
    ImGui::Text("PlayerID: %llu", static_cast<unsigned long long>(player.clientId));
    ImGui::Text("Status:   %s", player.dead ? "DEAD" : "ALIVE");
    ImGui::Text("Health:   %d", player.health);
    ImGui::Text("Insanity: %.0f", static_cast<double>(player.insanity));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fear/mental-state value. High insanity gives a large bonus in the Ghost "
                          "Girl's weighted target selection.");

    sectionLabel("GENERAL ACTIONS");
    auto drawPlayerActionRow = [&](const char* label, PlayerAction action) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.f);
        ImGui::PushID(label);
        if (actionButton("Execute", 90.f)) {
            queueAction({action, PlayerTargetPayload{player.clientId}});
        }
        ImGui::PopID();
    };
    drawPlayerActionRow("Teleport To Them", PlayerAction::TeleportTo);
    drawPlayerActionRow("Kill Player", PlayerAction::Kill);
    drawPlayerActionRow("Heal Player", PlayerAction::Heal);
    ImGui::TextUnformatted("Clear Insanity");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.f);
    ImGui::PushID("clear_insanity");
    if (actionButton("Execute", 90.f)) {
        queueAction({PlayerAction::ClearInsanity, PlayerTargetPayload{player.clientId}});
    }
    ImGui::PopID();
    ImGui::TextUnformatted("Max Insanity");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.f);
    ImGui::PushID("max_insanity");
    if (actionButton("Execute", 90.f)) {
        queueAction({PlayerAction::MaxInsanity, PlayerTargetPayload{player.clientId}});
    }
    ImGui::PopID();
    drawPlayerActionRow("Lure All Enemies", PlayerAction::LureAllEnemies);
    ImGui::TextUnformatted("Persistent Targeting");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.f);
    ImGui::PushID("persistent_lure");
    const bool persistent =
        snapshot.persistentLureClientId && *snapshot.persistentLureClientId == player.clientId;
    if (actionButton(persistent ? "Disable" : "Enable", 90.f)) {
        queueAction({PlayerAction::TogglePersistentLure, PlayerTargetPayload{player.clientId}});
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Continuously forces living enemies to track this player's live location while "
            "preserving their attack AI. Special enemies still use their normal attack phases.");
    ImGui::PopID();
    drawPlayerActionRow("Teleport All Enemies", PlayerAction::TeleportAllEnemies);

    ImGui::Spacing();
    if (actionButton("Kill Everyone", -1.f)) {
        queueAction({PlayerAction::KillAll, NoPlayerActionPayload{}});
    }
    if (actionButton("Kill Everyone Except You", -1.f)) {
        queueAction({PlayerAction::KillAllExceptLocal, NoPlayerActionPayload{}});
    }
    ImGui::EndChild();
}

void Interface::drawSettingsTab(MenuConfiguration& configuration) {
    sectionLabel("CONTROLS");
    ImGui::TextUnformatted("Menu key");
    ImGui::SameLine(140.f);
    if (mCapturingMenuKey) {
        if (actionButton("press key...", 120.f))
            mCapturingMenuKey = false;
    } else {
        ImGui::PushID("menukey");
        if (ImGui::Button(menuKeyName(configuration.menuKey), ImVec2(120.f, 0.f)))
            mCapturingMenuKey = true;
        ImGui::PopID();
    }
    if (mCapturingMenuKey && ImGui::IsKeyPressed(ImGuiKey_Escape))
        mCapturingMenuKey = false;
    if (mCapturingMenuKey) {
        for (int keyValue = ImGuiKey_NamedKey_BEGIN; keyValue < ImGuiKey_NamedKey_END; ++keyValue) {
            if (keyValue == ImGuiKey_Escape)
                continue;
            if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(keyValue)))
                continue;
            MenuKey menuKey;
            if (!translateMenuKey(static_cast<ImGuiKey>(keyValue), menuKey))
                continue;
            configuration.menuKey = menuKey;
            mCapturingMenuKey = false;
            markConfigChanged();
            break;
        }
    }

    sectionLabel("PERFORMANCE");
    ImGui::SetNextItemWidth(-1.f);
    if (boundedSlider("##poll", configuration.pollRate, {.minimum = 10.f, .maximum = 90.f},
                      "Camera %.0f Hz")) {
        markConfigChanged();
    }
    ImGui::SetNextItemWidth(-1.f);
    if (boundedSlider("##tick", configuration.tickIntervalMs, {.minimum = 33, .maximum = 500},
                      "Write every %d ms")) {
        markConfigChanged();
    }

    sectionLabel("CONFIG");
    if (actionButton("Save", 120.f))
        requestConfigSave();
}

}
