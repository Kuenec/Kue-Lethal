#ifndef KUE_OVERLAY_INTERNAL_HUD_RENDERER_H
#define KUE_OVERLAY_INTERNAL_HUD_RENDERER_H

#include "overlay/Menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

struct ImDrawData;

namespace kue::internalhud {

enum class MenuRasterStatus : std::uint8_t {
    Success,
    InvalidOutput,
    InvalidFont,
    InvalidFrame,
    CapacityExceeded,
    InvalidList,
    UnsupportedCallback,
    UnsupportedTexture,
    InvalidCommand,
    InvalidVertex,
    RevisionExhausted,
};

enum class MenuPixelChange : std::uint8_t { Changed, Unchanged };

struct MenuScreen {
    int width;
    int height;
};

struct MenuFontAtlas {
    std::span<const std::uint8_t> pixels;
    int width;
    int height;
};

struct MenuRasterFrame {
    std::span<const std::uint8_t> pixels;
    int x;
    int y;
    int width;
    int height;
    std::uint64_t revision;
    MenuPixelChange change;
};

struct MenuRasterBounds {
    int x;
    int y;
    int width;
    int height;
};

struct MenuFrameEffects {
    bool configurationChanged;
    bool explicitSaveRequested;
};

[[nodiscard]] const char* menuRasterStatusName(MenuRasterStatus status) noexcept;
[[nodiscard]] MenuRasterStatus checkedNextPixelRevision(std::uint64_t current,
                                                        std::uint64_t& output) noexcept;

class InternalHudRenderer final {
  public:
    static constexpr int kMaximumRasterWidth = 726;
    static constexpr int kMaximumRasterHeight = 446;
    static constexpr std::size_t kMaximumRasterBytes =
        static_cast<std::size_t>(kMaximumRasterWidth) *
        static_cast<std::size_t>(kMaximumRasterHeight) * std::size_t{4};
    static constexpr std::size_t kMaximumCachedDrawBytes = 65536;

    [[nodiscard]] MenuRasterStatus setFontAtlas(MenuFontAtlas atlas) noexcept;
    void clearFontAtlas() noexcept;
    [[nodiscard]] MenuRasterStatus render(const ImDrawData* drawData,
                                          const ui::MenuRectangle& rectangle, MenuScreen screen,
                                          MenuRasterFrame& output) noexcept;

  private:
    [[nodiscard]] bool drawIdentityMatches(const ImDrawData* drawData,
                                           const MenuRasterBounds& bounds) const noexcept;
    void storeDrawIdentity(const ImDrawData* drawData, const MenuRasterBounds& bounds) noexcept;
    void rasterValidatedDrawData(const ImDrawData* drawData,
                                 const MenuRasterBounds& bounds) noexcept;

    std::array<std::uint8_t, kMaximumRasterBytes> mPixels{};
    std::array<std::uint8_t, kMaximumCachedDrawBytes> mDrawIdentity{};
    std::span<const std::uint8_t> mFontPixels;
    std::size_t mPixelBytes = 0;
    std::size_t mDrawIdentityBytes = 0;
    std::uint64_t mPixelRevision = 0;
    int mFontWidth = 0;
    int mFontHeight = 0;
    bool mDrawIdentityValid = false;
};

class InternalHudFrameTransaction final {
  public:
    InternalHudFrameTransaction(const ui::Interface& interface, const Config& config) noexcept;

    [[nodiscard]] ui::Interface& interface() noexcept;
    [[nodiscard]] ui::MenuConfiguration& configuration() noexcept;
    [[nodiscard]] MenuRasterStatus commit(InternalHudRenderer& renderer, const ImDrawData* drawData,
                                          const ui::MenuRectangle& rectangle, MenuScreen screen,
                                          ui::Interface& liveInterface, Config& liveConfig,
                                          MenuRasterFrame& rasterFrame,
                                          MenuFrameEffects& effects) noexcept;

  private:
    ui::Interface mInterface;
    ui::MenuConfiguration mConfiguration;
};

}

#endif
