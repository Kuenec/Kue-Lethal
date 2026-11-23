#ifndef KUE_OVERLAY_MENU_H
#define KUE_OVERLAY_MENU_H

#include "core/Config.h"
#include "game/Game.h"
#include "game/PlayerActions.h"
#include "game/RuntimeCatalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace kue::ui {

enum class MenuVisibility : std::uint8_t { Hidden, Visible };

struct MenuFrame {
    int width;
    int height;
    float framesPerSecond;
    std::optional<MenuKey> pressedKey;
};

struct MenuRectangle {
    float x;
    float y;
    float width;
    float height;
};

enum class PreviewKind : std::uint8_t { Enemy, Item, Trap, Moon };

struct MenuPreview {
    PreviewKind kind;
    std::uint16_t catalogIndex;
    MenuRectangle rectangle;
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
    [[nodiscard]] std::optional<MenuPreview> preview() const;
    bool consumeConfigChange();
    bool consumeSaveRequest();

  private:
    enum class MenuPage : std::uint8_t {
        Self,
        Visuals,
        Players,
        Enemies,
        Items,
        Moons,
        Trolls,
        Settings
    };

    enum class EspCategory : std::uint8_t { Players, Scrap, Enemies, Entrance, FireExits, Ship };

    enum class SpawnKind : std::uint8_t { Enemies, Traps };

    struct PlayerPicker {
        const char* id;
        const char* habitatLabel;
        std::size_t visibleRows;
    };

    void drawSidebar();
    void drawHeader(const MenuSnapshot& snapshot, const MenuFrame& frame);
    void drawFooter(std::string_view runtimeStatus, const MenuConfiguration& configuration);
    void drawSelfPage(const MenuSnapshot& snapshot, MenuConfiguration& configuration);
    void drawVisualsPage(MenuConfiguration& configuration);
    void drawPlayersPage(const MenuSnapshot& snapshot);
    void drawEnemiesPage(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawEnemyTypes(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawTrapTypes(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawItemsPage(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawMoonsPage(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs);
    void drawTrollsPage(const MenuSnapshot& snapshot);
    void drawSettingsPage(MenuConfiguration& configuration, const MenuFrame& frame);
    void drawPlayerPicker(PlayerPicker picker, const MenuSnapshot& snapshot,
                          std::optional<std::uint64_t>& selected);
    void drawModelPanel(const MenuSnapshot& snapshot, PreviewKind kind,
                        std::optional<std::size_t> catalogIndex, std::string_view name);
    void configSwitch(const char* label, const char* hint, bool& value);
    void sessionSwitch(const char* label, const char* hint, bool on, PlayerActionRequest request,
                       const char* blocked = nullptr);
    void command(const char* label, const char* hint, PlayerActionRequest request,
                 const char* blocked = nullptr);
    void dangerCommand(const char* label, const char* hint, PlayerActionRequest request,
                       const char* blocked = nullptr);
    void blockedRow(const char* label, const char* hint, const char* reason);
    void markConfigChanged();
    void requestConfigSave();
    void queueAction(PlayerActionRequest request);

    PendingPlayerAction mPendingAction;
    MenuVisibility mVisibility = MenuVisibility::Hidden;
    MenuPage mPage = MenuPage::Self;
    EspCategory mColorCategory = EspCategory::Players;
    SpawnKind mSpawnKind = SpawnKind::Enemies;
    bool mMenuPositionInitialized = false;
    bool mCapturingMenuKey = false;
    bool mItemValueFixed = false;
    bool mConfigurationChanged = false;
    bool mConfigurationSaveRequested = false;
    const char* mHint = nullptr;
    std::optional<MenuPreview> mPreview;
    std::optional<std::uint64_t> mSelectedPlayer;
    std::optional<std::uint64_t> mEnemyTarget;
    std::optional<std::uint64_t> mTrapTarget;
    std::optional<std::uint64_t> mItemTarget;
    std::size_t mSelectedEnemyType = 0;
    std::size_t mSelectedTrapType = 0;
    std::size_t mSelectedItemType = 0;
    std::size_t mSelectedMoon = 0;
    int mEnemyCount = 1;
    int mEnemyDistanceMeters = 0;
    int mTrapCount = 1;
    int mTrapDistanceMeters = 3;
    int mItemCount = 1;
    int mItemValue = 100;
    int mCreditAmount = 1000;
    float mPlushieIntervalSeconds = 0.5f;
    float mMenuLeft = 80.f;
    float mMenuTop = 80.f;
    float mMenuWidth = 720.f;
    float mMenuHeight = 440.f;
    int mLastWindowWidth = 0;
    int mLastWindowHeight = 0;
};

}

#endif
