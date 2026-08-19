#include "overlay/Theme.h"

#include "imgui.h"

namespace kue::ui {

namespace {
ImFont* gBody = nullptr;
ImFont* gTitle = nullptr;
}

void setFonts(MenuFonts fonts) {
    gBody = fonts.body;
    gTitle = fonts.title;
}

ImFont* fontBody() {
    return gBody;
}
ImFont* fontTitle() {
    return gTitle;
}

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    const ImVec4 bg(0.035f, 0.035f, 0.038f, 0.96f);
    const ImVec4 sidebar(0.055f, 0.055f, 0.060f, 1.00f);
    const ImVec4 panel(0.070f, 0.070f, 0.076f, 1.00f);
    const ImVec4 accent(0.80f, 0.18f, 0.22f, 1.00f);
    const ImVec4 accentHover(0.90f, 0.26f, 0.30f, 1.00f);
    const ImVec4 text(0.90f, 0.90f, 0.92f, 1.00f);
    const ImVec4 muted(0.48f, 0.48f, 0.52f, 1.00f);
    const ImVec4 frame(0.10f, 0.10f, 0.11f, 1.00f);
    const ImVec4 frameHover(0.14f, 0.14f, 0.16f, 1.00f);
    const ImVec4 border(0.14f, 0.14f, 0.16f, 1.00f);

    s.WindowPadding = ImVec2(0.f, 0.f);
    s.FramePadding = ImVec2(8.f, 4.f);
    s.ItemSpacing = ImVec2(8.f, 6.f);
    s.ItemInnerSpacing = ImVec2(6.f, 4.f);
    s.ScrollbarSize = 8.f;
    s.GrabMinSize = 8.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.PopupBorderSize = 1.f;
    s.WindowRounding = 8.f;
    s.ChildRounding = 6.f;
    s.FrameRounding = 4.f;
    s.GrabRounding = 3.f;
    s.PopupRounding = 6.f;
    s.ScrollbarRounding = 4.f;
    s.TabRounding = 4.f;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.AntiAliasedLines = true;
    s.AntiAliasedFill = true;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.28f);

    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.16f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.26f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.34f);

    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = frameHover;
    c[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 1.f);

    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.f);

    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accentHover;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_ScrollbarBg] = sidebar;
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.22f, 0.24f, 1.f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.33f, 1.f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;

    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_SeparatorHovered] = accentHover;
    c[ImGuiCol_SeparatorActive] = accent;

    c[ImGuiCol_Tab] = sidebar;
    c[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    c[ImGuiCol_TabSelected] = ImVec4(accent.x * 0.35f, accent.y * 0.12f, accent.z * 0.14f, 1.f);
    c[ImGuiCol_TabDimmed] = sidebar;

    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = accentHover;
}

}
