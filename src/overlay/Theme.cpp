#include "overlay/Theme.h"

namespace kue::ui {

namespace {
MenuFonts gFonts{.body = nullptr, .title = nullptr, .caption = nullptr};
}

void setFonts(MenuFonts fonts) {
    gFonts = fonts;
}

MenuFonts fonts() {
    return gFonts;
}

void applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(0.f, 0.f);
    style.FramePadding = ImVec2(8.f, 5.f);
    style.ItemSpacing = ImVec2(8.f, 2.f);
    style.ItemInnerSpacing = ImVec2(6.f, 4.f);
    style.ScrollbarSize = 6.f;
    style.GrabMinSize = 8.f;
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.PopupBorderSize = 1.f;
    style.WindowRounding = 6.f;
    style.ChildRounding = 4.f;
    style.FrameRounding = 3.f;
    style.GrabRounding = 3.f;
    style.PopupRounding = 4.f;
    style.ScrollbarRounding = 3.f;
    style.ButtonTextAlign = ImVec2(0.f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.f, 0.5f);
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = palette::kWindow;
    colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    colors[ImGuiCol_PopupBg] = palette::kCard;
    colors[ImGuiCol_Border] = palette::kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);

    colors[ImGuiCol_Text] = palette::kText;
    colors[ImGuiCol_TextDisabled] = palette::kMuted;
    colors[ImGuiCol_TextSelectedBg] = palette::kControlActive;

    colors[ImGuiCol_Header] = palette::kControlActive;
    colors[ImGuiCol_HeaderHovered] = palette::kControlHover;
    colors[ImGuiCol_HeaderActive] = palette::kControlActive;

    colors[ImGuiCol_Button] = palette::kControl;
    colors[ImGuiCol_ButtonHovered] = palette::kControlHover;
    colors[ImGuiCol_ButtonActive] = palette::kControlActive;

    colors[ImGuiCol_FrameBg] = palette::kControl;
    colors[ImGuiCol_FrameBgHovered] = palette::kControlHover;
    colors[ImGuiCol_FrameBgActive] = palette::kControlActive;

    colors[ImGuiCol_CheckMark] = palette::kAccent;
    colors[ImGuiCol_SliderGrab] = palette::kMuted;
    colors[ImGuiCol_SliderGrabActive] = palette::kAccent;
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
    colors[ImGuiCol_ScrollbarGrab] = palette::kSwitchOff;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette::kFaint;
    colors[ImGuiCol_ScrollbarGrabActive] = palette::kMuted;

    colors[ImGuiCol_Separator] = palette::kBorder;
    colors[ImGuiCol_SeparatorHovered] = palette::kBorder;
    colors[ImGuiCol_SeparatorActive] = palette::kBorder;

    colors[ImGuiCol_PlotHistogram] = palette::kAccent;
    colors[ImGuiCol_PlotHistogramHovered] = palette::kAccent;
}

}
