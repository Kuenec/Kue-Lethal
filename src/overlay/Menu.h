#ifndef KUE_OVERLAY_MENU_H
#define KUE_OVERLAY_MENU_H

#include "core/Config.h"
#include "game/Game.h"
#include "game/PlayerActions.h"
#include "game/RuntimeCatalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace kue::ui {

enum class MenuVisibility : std::uint8_t { Hidden, Visible };

struct MenuFrame {
    int width;
    int height;
    float framesPerSecond;
};

struct MenuRectangle {
    float x;
    float y;
    float width;
    float height;
};

struct MenuConfiguration {
    EspConfig esp;
    MenuKey menuKey;
    float pollRate;
    int tickIntervalMs;
    bool infiniteStamina;
    bool noWeight;
    bool infiniteBattery;
    bool extendedInventory;
};

inline constexpr std::size_t kPlayerListLabelCapacity = kPlayerNameCapacity + sizeof(" (you)");

static_assert(std::is_trivially_copyable_v<MenuConfiguration>);

[[nodiscard]] MenuConfiguration menuConfiguration(const Config& config) noexcept;
void applyMenuConfiguration(Config& config, const MenuConfiguration& menu) noexcept;
[[nodiscard]] std::array<char, kPlayerListLabelCapacity>
playerListLabel(const PlayerSnapshotPlayer& player) noexcept;

class Interface {
  public:
    void render(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs,
                const MenuFrame& frame, std::string_view runtimeStatus,
                MenuConfiguration& configuration);
    [[nodiscard]] bool readAction(PlayerActionRequest& output) const;
    void applyActionEnqueueResult(PlayerActionEnqueueResult result) noexcept;
    void setVisible(MenuVisibility visibility);
    MenuRectangle menuRectangle() const;
    bool consumeConfigChange();
    bool consumeSaveRequest();

  private:
    enum class MenuTab : std::uint8_t { Self, Visuals, Players, Enemies, Items, Trolls, Settings };

    void drawTabs();
    void drawTabButton(const char* label, MenuTab tab);
    void drawSelfTab(const MenuSnapshot& snapshot, MenuConfiguration& configuration);
    void drawVisualsTab(MenuConfiguration& configuration);
    void drawPlayersTab(const MenuSnapshot& snapshot);
    void drawEnemiesTab(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawItemsTab(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawTrollsTab(const MenuSnapshot& snapshot);
    void drawSettingsTab(MenuConfiguration& configuration);

    PendingPlayerAction mPendingAction;
    MenuVisibility mVisibility = MenuVisibility::Hidden;
    bool mMenuPositionInitialized = false;
    void markConfigChanged();
    void requestConfigSave();
    void queueAction(PlayerActionRequest request);

    MenuTab mActiveTab = MenuTab::Self;
    std::size_t mSelectedPlayer = 0;
    bool mCapturingMenuKey = false;
    std::size_t mSelectedEnemyType = 0;
    std::size_t mSelectedSpawnPlayer = 0;
    std::size_t mSelectedItemType = 0;
    std::size_t mSelectedItemPlayer = 0;
    int mItemSpawnCount = 1;
    int mEnemySpawnCount = 1;
    bool mSpawnOutside = false;
    float mPjSpamSpeed = 0.5f;
    bool mConfigurationChanged = false;
    bool mConfigurationSaveRequested = false;
    float mMenuLeft = 80.f;
    float mMenuTop = 80.f;
    float mMenuWidth = 720.f;
    float mMenuHeight = 440.f;
    int mLastWindowWidth = 0, mLastWindowHeight = 0;
};

}

#endif
