#ifndef KUE_OVERLAY_THEME_H
#define KUE_OVERLAY_THEME_H

#include "imgui.h"

namespace kue::ui {

namespace palette {

inline constexpr ImVec4 kWindow{0.020f, 0.020f, 0.024f, 0.98f};
inline constexpr ImVec4 kSidebar{0.000f, 0.000f, 0.000f, 1.00f};
inline constexpr ImVec4 kCard{0.047f, 0.047f, 0.055f, 1.00f};
inline constexpr ImVec4 kBorder{0.110f, 0.110f, 0.130f, 1.00f};
inline constexpr ImVec4 kControl{0.078f, 0.078f, 0.090f, 1.00f};
inline constexpr ImVec4 kControlHover{0.110f, 0.110f, 0.126f, 1.00f};
inline constexpr ImVec4 kControlActive{0.150f, 0.150f, 0.170f, 1.00f};
inline constexpr ImVec4 kSwitchOff{0.165f, 0.165f, 0.185f, 1.00f};
inline constexpr ImVec4 kText{0.910f, 0.910f, 0.925f, 1.00f};
inline constexpr ImVec4 kMuted{0.540f, 0.540f, 0.575f, 1.00f};
inline constexpr ImVec4 kFaint{0.340f, 0.340f, 0.370f, 1.00f};
inline constexpr ImVec4 kAccent{0.940f, 0.940f, 0.955f, 1.00f};
inline constexpr ImVec4 kOnAccent{0.020f, 0.020f, 0.024f, 1.00f};
inline constexpr ImVec4 kDanger{0.880f, 0.270f, 0.310f, 1.00f};
inline constexpr ImVec4 kDangerHover{0.300f, 0.075f, 0.090f, 1.00f};
inline constexpr ImVec4 kGood{0.360f, 0.800f, 0.480f, 1.00f};

}

struct MenuFonts {
    ImFont* body;
    ImFont* title;
    ImFont* caption;
};

void applyTheme();
void setFonts(MenuFonts fonts);
[[nodiscard]] MenuFonts fonts();
}

#endif
