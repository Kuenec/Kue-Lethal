#ifndef KUE_OVERLAY_THEME_H
#define KUE_OVERLAY_THEME_H

struct ImFont;

namespace kue::ui {

void applyTheme();

ImFont* fontBody();
ImFont* fontTitle();
struct MenuFonts {
    ImFont* body;
    ImFont* title;
};
void setFonts(MenuFonts fonts);

}

#endif
