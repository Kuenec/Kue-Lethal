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
#include <span>

namespace kue::ui {

namespace {

constexpr float kMenuWidth = 900.f;
constexpr float kMenuHeight = 520.f;
constexpr float kSidebarWidth = 132.f;
constexpr float kHeaderHeight = 38.f;
constexpr float kFooterHeight = 26.f;
constexpr float kPagePadding = 12.f;
constexpr float kColumnGap = 10.f;
constexpr float kRowHeight = 24.f;
constexpr float kListRowHeight = 20.f;
constexpr float kCardPadding = 6.f;
constexpr float kListWidth = 200.f;
constexpr float kControlsWidth = 276.f;
constexpr float kLabelWidth = 104.f;
constexpr float kPoundsPerCarryUnit = 105.f;

struct MenuKeyName {
    MenuKey key;
    const char* name;
};

constexpr std::array<MenuKeyName, 52> kMenuKeyNames = {{
    {MenuKey::Insert, "Insert"}, {MenuKey::Tab, "Tab"},  {MenuKey::Enter, "Enter"},
    {MenuKey::Space, "Space"},   {MenuKey::F1, "F1"},    {MenuKey::F2, "F2"},
    {MenuKey::F3, "F3"},         {MenuKey::F4, "F4"},    {MenuKey::F5, "F5"},
    {MenuKey::F6, "F6"},         {MenuKey::F7, "F7"},    {MenuKey::F8, "F8"},
    {MenuKey::F9, "F9"},         {MenuKey::F10, "F10"},  {MenuKey::F11, "F11"},
    {MenuKey::F12, "F12"},       {MenuKey::A, "A"},      {MenuKey::B, "B"},
    {MenuKey::C, "C"},           {MenuKey::D, "D"},      {MenuKey::E, "E"},
    {MenuKey::F, "F"},           {MenuKey::G, "G"},      {MenuKey::H, "H"},
    {MenuKey::I, "I"},           {MenuKey::J, "J"},      {MenuKey::K, "K"},
    {MenuKey::L, "L"},           {MenuKey::M, "M"},      {MenuKey::N, "N"},
    {MenuKey::O, "O"},           {MenuKey::P, "P"},      {MenuKey::Q, "Q"},
    {MenuKey::R, "R"},           {MenuKey::S, "S"},      {MenuKey::T, "T"},
    {MenuKey::U, "U"},           {MenuKey::V, "V"},      {MenuKey::W, "W"},
    {MenuKey::X, "X"},           {MenuKey::Y, "Y"},      {MenuKey::Z, "Z"},
    {MenuKey::Digit0, "0"},      {MenuKey::Digit1, "1"}, {MenuKey::Digit2, "2"},
    {MenuKey::Digit3, "3"},      {MenuKey::Digit4, "4"}, {MenuKey::Digit5, "5"},
    {MenuKey::Digit6, "6"},      {MenuKey::Digit7, "7"}, {MenuKey::Digit8, "8"},
    {MenuKey::Digit9, "9"},
}};

const char* menuKeyName(MenuKey key) {
    for (const MenuKeyName& entry : kMenuKeyNames) {
        if (entry.key == key) {
            return entry.name;
        }
    }
    return "Key";
}

ImU32 color(const ImVec4& value) {
    return ImGui::ColorConvertFloat4ToU32(value);
}

enum class Icon : std::uint8_t {
    Self,
    Visuals,
    Players,
    Enemies,
    Items,
    Moons,
    Trolls,
    Settings,
    Chevron,
    Sun,
    Move,
    Battery,
    Sound,
    Pulse,
    Bolt,
    Palette,
    Bell,
    Info,
    Pin,
    List,
    Coin,
    Building,
    Ship,
    Car,
    Cog,
    Key,
    Gauge,
    Save,
    Keyboard,
    Route
};

constexpr float kPi = 3.14159265358979f;
constexpr float kIconStroke = 1.3f;
constexpr std::array<ImVec2, 8> kCompass = {{
    {1.f, 0.f},
    {0.70710678f, 0.70710678f},
    {0.f, 1.f},
    {-0.70710678f, 0.70710678f},
    {-1.f, 0.f},
    {-0.70710678f, -0.70710678f},
    {0.f, -1.f},
    {0.70710678f, -0.70710678f},
}};

struct IconPen {
    ImDrawList* drawList;
    ImVec2 center;
    float scale;
    ImU32 tint;

    [[nodiscard]] ImVec2 at(float x, float y) const {
        return {center.x + x * scale, center.y + y * scale};
    }

    void line(ImVec2 from, ImVec2 to) const {
        drawList->AddLine(at(from.x, from.y), at(to.x, to.y), tint, kIconStroke);
    }

    void ring(ImVec2 middle, float radius) const {
        drawList->AddCircle(at(middle.x, middle.y), radius * scale, tint, 16, kIconStroke);
    }

    void dot(ImVec2 middle, float radius) const {
        drawList->AddCircleFilled(at(middle.x, middle.y), radius * scale, tint, 10);
    }

    void box(ImVec2 minimum, ImVec2 maximum, float rounding) const {
        drawList->AddRect(at(minimum.x, minimum.y), at(maximum.x, maximum.y), tint,
                          rounding * scale, ImDrawFlags_None, kIconStroke);
    }

    void block(ImVec2 minimum, ImVec2 maximum) const {
        drawList->AddRectFilled(at(minimum.x, minimum.y), at(maximum.x, maximum.y), tint);
    }

    void arc(ImVec2 middle, float radius, float fromTurn, float toTurn) const {
        drawList->PathArcTo(at(middle.x, middle.y), radius * scale, kPi * fromTurn, kPi * toTurn,
                            10);
        drawList->PathStroke(tint, ImDrawFlags_None, kIconStroke);
    }

    void path(std::span<const ImVec2> points, ImDrawFlags flags) const {
        for (const ImVec2& point : points) {
            drawList->PathLineTo(at(point.x, point.y));
        }
        drawList->PathStroke(tint, flags, kIconStroke);
    }

    void person(ImVec2 middle, float size) const {
        ring({middle.x, middle.y - 3.2f * size}, 2.4f * size);
        arc({middle.x, middle.y + 6.2f * size}, 4.8f * size, 1.f, 2.f);
    }

    void rays(float inner, float outer) const {
        for (const ImVec2& heading : kCompass) {
            line({heading.x * inner, heading.y * inner}, {heading.x * outer, heading.y * outer});
        }
    }
};

void drawIcon(ImDrawList* drawList, Icon icon, ImVec2 center, ImU32 tint, float scale = 1.f) {
    const IconPen pen{drawList, center, scale, tint};
    switch (icon) {
    case Icon::Self:
        pen.person({0.f, 0.f}, 1.f);
        return;
    case Icon::Players:
        pen.person({-2.6f, 0.6f}, 0.82f);
        pen.person({3.2f, -0.4f}, 0.82f);
        return;
    case Icon::Visuals:
        pen.arc({0.f, 5.2f}, 8.2f, 1.22f, 1.78f);
        pen.arc({0.f, -5.2f}, 8.2f, 0.22f, 0.78f);
        pen.dot({0.f, 0.f}, 2.f);
        return;
    case Icon::Enemies:
        pen.ring({0.f, -1.2f}, 5.f);
        pen.box({-2.6f, 3.2f}, {2.6f, 6.2f}, 0.f);
        pen.dot({-2.f, -1.2f}, 1.3f);
        pen.dot({2.f, -1.2f}, 1.3f);
        return;
    case Icon::Items:
        pen.box({-5.5f, -5.f}, {5.5f, 5.5f}, 1.f);
        pen.line({-5.5f, -1.4f}, {5.5f, -1.4f});
        pen.line({0.f, -5.f}, {0.f, -1.4f});
        return;
    case Icon::Moons:
        pen.ring({0.f, 0.f}, 4.2f);
        pen.line({-7.f, 2.6f}, {-4.4f, 1.6f});
        pen.line({4.4f, -1.6f}, {7.f, -2.6f});
        return;
    case Icon::Trolls: {
        constexpr std::array<ImVec2, 3> leftHorn = {
            {{-4.7f, -1.6f}, {-5.6f, -6.4f}, {-2.f, -4.2f}}};
        constexpr std::array<ImVec2, 3> rightHorn = {{{4.7f, -1.6f}, {5.6f, -6.4f}, {2.f, -4.2f}}};
        pen.ring({0.f, 0.8f}, 5.2f);
        pen.path(leftHorn, ImDrawFlags_None);
        pen.path(rightHorn, ImDrawFlags_None);
        pen.dot({-2.f, -0.2f}, 1.f);
        pen.dot({2.f, -0.2f}, 1.f);
        pen.arc({0.f, 1.2f}, 2.8f, 0.15f, 0.85f);
        return;
    }
    case Icon::Settings:
        pen.line({-6.f, -4.f}, {6.f, -4.f});
        pen.line({-6.f, 0.f}, {6.f, 0.f});
        pen.line({-6.f, 4.f}, {6.f, 4.f});
        pen.dot({2.5f, -4.f}, 1.9f);
        pen.dot({-2.5f, 0.f}, 1.9f);
        pen.dot({3.5f, 4.f}, 1.9f);
        return;
    case Icon::Chevron:
        pen.line({-1.8f, -3.2f}, {1.6f, 0.f});
        pen.line({1.6f, 0.f}, {-1.8f, 3.2f});
        return;
    case Icon::Sun:
        pen.dot({0.f, 0.f}, 3.6f);
        pen.rays(5.4f, 7.6f);
        return;
    case Icon::Move:
        pen.line({-5.f, -4.f}, {-1.f, 0.f});
        pen.line({-1.f, 0.f}, {-5.f, 4.f});
        pen.line({0.5f, -4.f}, {4.5f, 0.f});
        pen.line({4.5f, 0.f}, {0.5f, 4.f});
        return;
    case Icon::Battery:
        pen.box({-6.f, -3.5f}, {4.5f, 3.5f}, 1.f);
        pen.block({4.5f, -1.5f}, {6.2f, 1.5f});
        pen.block({-4.2f, -1.8f}, {0.5f, 1.8f});
        return;
    case Icon::Sound: {
        constexpr std::array<ImVec2, 6> speaker = {
            {{-6.f, -2.f}, {-3.5f, -2.f}, {-0.5f, -5.f}, {-0.5f, 5.f}, {-3.5f, 2.f}, {-6.f, 2.f}}};
        pen.path(speaker, ImDrawFlags_Closed);
        pen.arc({0.5f, 0.f}, 3.f, -0.25f, 0.25f);
        pen.arc({0.5f, 0.f}, 5.6f, -0.25f, 0.25f);
        return;
    }
    case Icon::Pulse: {
        constexpr std::array<ImVec2, 6> beat = {
            {{-6.5f, 0.f}, {-3.f, 0.f}, {-1.4f, -4.5f}, {0.8f, 4.5f}, {2.4f, 0.f}, {6.5f, 0.f}}};
        pen.path(beat, ImDrawFlags_None);
        return;
    }
    case Icon::Bolt: {
        constexpr std::array<ImVec2, 6> bolt = {{{1.5f, -6.f},
                                                 {-3.5f, 0.8f},
                                                 {-0.3f, 0.8f},
                                                 {-1.5f, 6.f},
                                                 {3.5f, -0.8f},
                                                 {0.3f, -0.8f}}};
        pen.path(bolt, ImDrawFlags_Closed);
        return;
    }
    case Icon::Palette:
        pen.ring({0.f, 0.f}, 5.6f);
        pen.dot({-2.2f, -1.6f}, 1.2f);
        pen.dot({2.2f, -1.6f}, 1.2f);
        pen.dot({0.f, 2.4f}, 1.2f);
        return;
    case Icon::Bell: {
        pen.arc({0.f, -1.f}, 4.f, 1.f, 2.f);
        constexpr std::array<ImVec2, 6> skirt = {
            {{4.f, -1.f}, {4.f, 2.f}, {5.6f, 3.8f}, {-5.6f, 3.8f}, {-4.f, 2.f}, {-4.f, -1.f}}};
        pen.path(skirt, ImDrawFlags_None);
        pen.dot({0.f, 5.6f}, 1.1f);
        return;
    }
    case Icon::Info:
        pen.ring({0.f, 0.f}, 5.6f);
        pen.dot({0.f, -2.7f}, 0.9f);
        pen.line({0.f, -0.6f}, {0.f, 3.2f});
        return;
    case Icon::Pin:
        pen.ring({0.f, -1.6f}, 3.7f);
        pen.line({-3.f, 0.6f}, {0.f, 6.2f});
        pen.line({3.f, 0.6f}, {0.f, 6.2f});
        pen.dot({0.f, -1.6f}, 1.2f);
        return;
    case Icon::List:
        for (const float row : {-4.f, 0.f, 4.f}) {
            pen.dot({-5.2f, row}, 1.f);
            pen.line({-2.4f, row}, {6.2f, row});
        }
        return;
    case Icon::Coin:
        pen.ring({0.f, 0.f}, 5.6f);
        pen.ring({0.f, 0.f}, 2.4f);
        return;
    case Icon::Building:
        pen.box({-4.6f, -6.f}, {4.6f, 6.f}, 0.f);
        pen.block({-2.6f, -4.f}, {-0.8f, -2.2f});
        pen.block({0.8f, -4.f}, {2.6f, -2.2f});
        pen.block({-2.6f, -0.8f}, {-0.8f, 1.f});
        pen.block({0.8f, -0.8f}, {2.6f, 1.f});
        pen.block({-1.f, 3.2f}, {1.f, 6.f});
        return;
    case Icon::Ship: {
        constexpr std::array<ImVec2, 5> hull = {
            {{0.f, -6.5f}, {3.f, -2.f}, {3.f, 3.2f}, {-3.f, 3.2f}, {-3.f, -2.f}}};
        pen.path(hull, ImDrawFlags_Closed);
        pen.line({-3.f, 0.8f}, {-5.6f, 4.6f});
        pen.line({-5.6f, 4.6f}, {-3.f, 3.2f});
        pen.line({3.f, 0.8f}, {5.6f, 4.6f});
        pen.line({5.6f, 4.6f}, {3.f, 3.2f});
        pen.dot({0.f, -1.2f}, 1.2f);
        pen.line({0.f, 4.4f}, {0.f, 6.4f});
        return;
    }
    case Icon::Car: {
        pen.box({-6.2f, -1.f}, {6.2f, 3.f}, 1.f);
        constexpr std::array<ImVec2, 4> roof = {
            {{-3.6f, -1.f}, {-2.2f, -4.2f}, {2.6f, -4.2f}, {4.2f, -1.f}}};
        pen.path(roof, ImDrawFlags_None);
        pen.dot({-3.3f, 3.8f}, 1.7f);
        pen.dot({3.3f, 3.8f}, 1.7f);
        return;
    }
    case Icon::Cog:
        pen.ring({0.f, 0.f}, 3.4f);
        pen.rays(3.8f, 6.2f);
        return;
    case Icon::Key:
        pen.ring({-3.2f, 0.f}, 2.8f);
        pen.line({-0.4f, 0.f}, {6.4f, 0.f});
        pen.line({3.6f, 0.f}, {3.6f, 2.8f});
        pen.line({5.8f, 0.f}, {5.8f, 2.2f});
        return;
    case Icon::Gauge:
        pen.arc({0.f, 2.6f}, 5.8f, 1.f, 2.f);
        pen.line({0.f, 2.6f}, {3.2f, -1.6f});
        pen.dot({0.f, 2.6f}, 1.3f);
        return;
    case Icon::Save:
        pen.box({-5.6f, -5.6f}, {5.6f, 5.6f}, 1.f);
        pen.box({-2.6f, -5.6f}, {2.6f, -2.2f}, 0.f);
        pen.block({-3.2f, 1.4f}, {3.2f, 5.6f});
        return;
    case Icon::Keyboard:
        pen.box({-6.6f, -4.2f}, {6.6f, 4.2f}, 1.5f);
        for (const float column : {-4.f, -1.3f, 1.3f, 4.f}) {
            pen.block({column - 0.7f, -2.2f}, {column + 0.7f, -0.8f});
        }
        pen.line({-3.2f, 1.8f}, {3.2f, 1.8f});
        return;
    case Icon::Route:
        pen.line({-6.f, 0.f}, {5.4f, 0.f});
        pen.line({1.6f, -3.6f}, {5.4f, 0.f});
        pen.line({5.4f, 0.f}, {1.6f, 3.6f});
        return;
    }
}

float keyCapWidth(const char* key) {
    ImFont* const caption = fonts().caption;
    if (!caption) {
        return 0.f;
    }
    return caption->CalcTextSizeA(caption->FontSize, 1000.f, 0.f, key).x + 10.f;
}

void drawKeyCap(ImDrawList* drawList, ImVec2 topLeft, const char* key) {
    ImFont* const caption = fonts().caption;
    if (!caption) {
        return;
    }
    constexpr float height = 16.f;
    const float width = keyCapWidth(key);
    const ImVec2 maximum(topLeft.x + width, topLeft.y + height);
    drawList->AddRectFilled(topLeft, maximum, color(palette::kControl), 3.f);
    drawList->AddRect(topLeft, maximum, color(palette::kFaint), 3.f, ImDrawFlags_None, 1.f);
    drawList->AddText(caption, caption->FontSize,
                      ImVec2(topLeft.x + 5.f, topLeft.y + (height - caption->FontSize) * 0.5f),
                      color(palette::kText), key);
}

void noteHint(const char*& slot, const char* hint) {
    if (hint && ImGui::IsItemHovered()) {
        slot = hint;
    }
}

void sectionTitle(Icon icon, const char* label) {
    ImFont* const caption = fonts().caption;
    if (caption) {
        ImGui::PushFont(caption);
    }
    ImGui::Dummy(ImVec2(0.f, 3.f));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFontSize();
    drawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(origin.x + 8.f, origin.y + height * 0.5f),
             color(palette::kMuted), 0.8f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 19.f);
    ImGui::PushStyleColor(ImGuiCol_Text, palette::kMuted);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 1.f));
    if (caption) {
        ImGui::PopFont();
    }
}

void noteText(const char* text) {
    ImFont* const caption = fonts().caption;
    if (caption) {
        ImGui::PushFont(caption);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, palette::kMuted);
    ImGui::Indent(8.f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 8.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::Unindent(8.f);
    ImGui::PopStyleColor();
    if (caption) {
        ImGui::PopFont();
    }
}

bool switchRow(const char* label, bool on, std::span<const char* const> keys = {}) {
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(label);
    ImGui::InvisibleButton("##switch", ImVec2(width, kRowHeight));
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    if (hovered) {
        drawList->AddRectFilled(minimum, maximum, color(palette::kControl), 3.f);
    }
    const float textY = minimum.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f;
    drawList->AddText(ImVec2(minimum.x + 8.f, textY), color(palette::kText), label);
    const ImVec2 trackSize(28.f, 14.f);
    const ImVec2 track(maximum.x - trackSize.x - 8.f,
                       minimum.y + (kRowHeight - trackSize.y) * 0.5f);
    const float radius = trackSize.y * 0.5f;
    drawList->AddRectFilled(track, ImVec2(track.x + trackSize.x, track.y + trackSize.y),
                            color(on ? palette::kAccent : palette::kSwitchOff), radius);
    const float knobX = on ? track.x + trackSize.x - radius : track.x + radius;
    drawList->AddCircleFilled(ImVec2(knobX, track.y + radius), radius - 2.5f,
                              color(on ? palette::kOnAccent : palette::kMuted));
    float keyRight = track.x - 8.f;
    for (std::size_t index = keys.size(); index > 0; --index) {
        const char* const key = keys[index - 1];
        keyRight -= keyCapWidth(key);
        drawKeyCap(drawList, ImVec2(keyRight, minimum.y + (kRowHeight - 16.f) * 0.5f), key);
        keyRight -= 3.f;
    }
    return clicked;
}

enum class CommandTone : std::uint8_t { Neutral, Danger };

bool commandRow(const char* label, CommandTone tone) {
    const bool danger = tone == CommandTone::Danger;
    ImGui::PushStyleColor(ImGuiCol_Text, danger ? palette::kDanger : palette::kText);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          danger ? palette::kDangerHover : palette::kControlHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          danger ? palette::kDangerHover : palette::kControlActive);
    const bool pressed = ImGui::Button(label, ImVec2(-1.f, kRowHeight));
    ImGui::PopStyleColor(3);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    drawIcon(ImGui::GetWindowDrawList(), Icon::Chevron,
             ImVec2(maximum.x - 12.f, (minimum.y + maximum.y) * 0.5f),
             color(danger ? palette::kDanger : palette::kFaint));
    return pressed;
}

void unavailableRow(const char* label, const char* reason) {
    ImGui::PushID(label);
    ImGui::InvisibleButton("##unavailable", ImVec2(ImGui::GetContentRegionAvail().x, kRowHeight));
    ImGui::PopID();
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImFont* const caption = fonts().caption ? fonts().caption : ImGui::GetFont();
    const ImVec2 reasonSize = caption->CalcTextSizeA(caption->FontSize, 1000.f, 0.f, reason);
    const float reasonX = maximum.x - reasonSize.x - 8.f;
    const ImVec4 clip(minimum.x, minimum.y, reasonX - 8.f, maximum.y);
    drawList->AddText(
        ImGui::GetFont(), ImGui::GetFontSize(),
        ImVec2(minimum.x + 8.f, minimum.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f),
        color(palette::kDisabled), label, nullptr, 0.f, &clip);
    drawList->AddText(caption, caption->FontSize,
                      ImVec2(reasonX, minimum.y + (kRowHeight - caption->FontSize) * 0.5f),
                      color(palette::kMuted), reason);
}

struct IntRange {
    int minimum;
    int maximum;
};

struct FloatRange {
    float minimum;
    float maximum;
};

bool sliderRow(const char* label, int& value, IntRange range, const char* format,
               ImGuiSliderFlags flags = ImGuiSliderFlags_None) {
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::PushID(label);
    const bool changed =
        ImGui::SliderInt("##value", &value, range.minimum, range.maximum, format,
                         flags | ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopID();
    return changed;
}

bool sliderRow(const char* label, float& value, FloatRange range, const char* format) {
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::PushID(label);
    const bool changed =
        ImGui::SliderFloat("##value", &value, range.minimum, range.maximum, format,
                           ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopID();
    return changed;
}

void meterRow(const char* label, const char* value, float fraction) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
    ImGui::TextUnformatted(label);
    const float valueWidth = ImGui::CalcTextSize(value).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - valueWidth - 20.f);
    ImGui::TextColored(palette::kMuted, "%s", value);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
    ImGui::ProgressBar(std::clamp(fraction, 0.f, 1.f),
                       ImVec2(ImGui::GetContentRegionAvail().x - 8.f, 4.f), "");
}

bool beginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, palette::kCard);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kCardPadding, kCardPadding));
    const bool visible = ImGui::BeginChild(
        id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return visible;
}

struct ListRow {
    const char* label;
    const char* detail = "";
    std::optional<ImVec4> dot = std::nullopt;
};

bool listRow(ListRow row, bool selected) {
    ImGui::PushID(row.label);
    const bool clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None,
                                           ImVec2(0.f, kListRowHeight));
    ImGui::PopID();
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const float middle = (minimum.y + maximum.y) * 0.5f;
    float textX = minimum.x + 6.f;
    if (row.dot) {
        drawList->AddCircleFilled(ImVec2(minimum.x + 9.f, middle), 3.f, color(*row.dot), 10);
        textX = minimum.x + 20.f;
    }
    const ImVec2 detailSize = ImGui::CalcTextSize(row.detail);
    const float detailX = maximum.x - detailSize.x - 6.f;
    const ImVec4 clip(textX, minimum.y, detailX - 6.f, maximum.y);
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                      ImVec2(textX, middle - ImGui::GetFontSize() * 0.5f), color(palette::kText),
                      row.label, nullptr, 0.f, &clip);
    if (row.detail[0] != '\0') {
        drawList->AddText(ImVec2(detailX, middle - detailSize.y * 0.5f), color(palette::kMuted),
                          row.detail);
    }
    return clicked;
}

bool segmentButton(const char* label, bool selected, float width) {
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? palette::kAccent : palette::kControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? palette::kAccent : palette::kControlHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          selected ? palette::kAccent : palette::kControlActive);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? palette::kOnAccent : palette::kMuted);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
    const bool pressed = ImGui::Button(label, ImVec2(width, 22.f));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

float columnWidth() {
    return (ImGui::GetContentRegionAvail().x - kColumnGap) * 0.5f;
}

const PlayerSnapshotPlayer* findPlayer(const PlayerSnapshot& players, std::uint64_t clientId) {
    for (const PlayerSnapshotPlayer& player : players) {
        if (player.clientId == clientId) {
            return &player;
        }
    }
    return nullptr;
}

const PlayerSnapshotPlayer* localPlayer(const PlayerSnapshot& players) {
    for (const PlayerSnapshotPlayer& player : players) {
        if (player.local) {
            return &player;
        }
    }
    return nullptr;
}

const char* locationName(PlayerLocation location) {
    switch (location) {
    case PlayerLocation::Facility:
        return "facility";
    case PlayerLocation::Outside:
        return "outside";
    }
    return "";
}

std::array<float, 4>& espColor(EspConfig& esp, int category) {
    switch (category) {
    case 1:
        return esp.colorItems;
    case 2:
        return esp.colorMonsters;
    case 3:
        return esp.colorExits;
    case 4:
        return esp.colorFireExits;
    case 5:
        return esp.colorShips;
    default:
        return esp.colorPlayers;
    }
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

std::optional<MenuPreview> Interface::preview() const {
    return mPreview;
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

void Interface::configSwitch(const char* label, const char* hint, bool& value) {
    if (switchRow(label, value)) {
        value = !value;
        markConfigChanged();
    }
    noteHint(mHint, hint);
}

void Interface::blockedRow(const char* label, const char* hint, const char* reason) {
    unavailableRow(label, reason);
    noteHint(mHint, hint);
}

void Interface::sessionSwitch(const char* label, const char* hint, bool on,
                              PlayerActionRequest request, const char* blocked) {
    if (blocked) {
        blockedRow(label, hint, blocked);
        return;
    }
    if (switchRow(label, on)) {
        queueAction(request);
    }
    noteHint(mHint, hint);
}

void Interface::command(const char* label, const char* hint, PlayerActionRequest request,
                        const char* blocked) {
    if (blocked) {
        blockedRow(label, hint, blocked);
        return;
    }
    if (commandRow(label, CommandTone::Neutral)) {
        queueAction(request);
    }
    noteHint(mHint, hint);
}

void Interface::dangerCommand(const char* label, const char* hint, PlayerActionRequest request,
                              const char* blocked) {
    if (blocked) {
        blockedRow(label, hint, blocked);
        return;
    }
    if (commandRow(label, CommandTone::Danger)) {
        queueAction(request);
    }
    noteHint(mHint, hint);
}

void Interface::render(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs,
                       const MenuFrame& frame, std::string_view runtimeStatus,
                       MenuConfiguration& configuration) {
    mPreview.reset();
    if (mVisibility == MenuVisibility::Hidden) {
        return;
    }
    mHint = nullptr;

    ImFont* const bodyFont = fonts().body;
    if (bodyFont) {
        ImGui::PushFont(bodyFont);
    }

    const float screenWidth = static_cast<float>(frame.width);
    const float screenHeight = static_cast<float>(frame.height);
    if (!mMenuPositionInitialized || frame.width != mLastWindowWidth ||
        frame.height != mLastWindowHeight) {
        mMenuLeft = std::max(40.f, (screenWidth - kMenuWidth) * 0.5f);
        mMenuTop = std::max(40.f, (screenHeight - kMenuHeight) * 0.5f);
        mMenuPositionInitialized = true;
        mLastWindowWidth = frame.width;
        mLastWindowHeight = frame.height;
    }
    mMenuLeft = std::clamp(mMenuLeft, 0.f, std::max(0.f, screenWidth - kMenuWidth));
    mMenuTop = std::clamp(mMenuTop, 0.f, std::max(0.f, screenHeight - kMenuHeight));

    ImGui::SetNextWindowPos(ImVec2(mMenuLeft, mMenuTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kMenuWidth, kMenuHeight), ImGuiCond_Always);
    const bool windowVisible = ImGui::Begin(
        "##kue_menu", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 windowPosition = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    mMenuLeft = windowPosition.x;
    mMenuTop = windowPosition.y;
    mMenuWidth = windowSize.x;
    mMenuHeight = windowSize.y;
    if (!windowVisible) {
        ImGui::End();
        if (bodyFont) {
            ImGui::PopFont();
        }
        return;
    }

    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowMaximum(windowPosition.x + windowSize.x, windowPosition.y + windowSize.y);
    drawList->AddRectFilled(
        windowPosition, ImVec2(windowPosition.x + kSidebarWidth, windowMaximum.y - kFooterHeight),
        color(palette::kSidebar), 6.f, ImDrawFlags_RoundCornersTopLeft);
    drawList->AddLine(ImVec2(windowPosition.x + kSidebarWidth, windowPosition.y),
                      ImVec2(windowPosition.x + kSidebarWidth, windowMaximum.y - kFooterHeight),
                      color(palette::kBorder));
    drawList->AddLine(ImVec2(windowPosition.x, windowMaximum.y - kFooterHeight),
                      ImVec2(windowMaximum.x, windowMaximum.y - kFooterHeight),
                      color(palette::kBorder));
    drawList->AddRect(windowPosition, windowMaximum, color(palette::kBorder), 6.f, 0, 1.f);

    ImGui::SetCursorPos(ImVec2(0.f, 0.f));
    ImGui::InvisibleButton("##drag_handle", ImVec2(windowSize.x, kHeaderHeight));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
        mMenuLeft =
            std::clamp(mMenuLeft + mouseDelta.x, 0.f, std::max(0.f, screenWidth - mMenuWidth));
        mMenuTop =
            std::clamp(mMenuTop + mouseDelta.y, 0.f, std::max(0.f, screenHeight - mMenuHeight));
        ImGui::SetWindowPos(ImVec2(mMenuLeft, mMenuTop));
    }

    drawSidebar();
    drawHeader(snapshot, frame);

    ImGui::SetCursorPos(ImVec2(kSidebarWidth + kPagePadding, kHeaderHeight));
    const ImVec2 pageSize(windowSize.x - kSidebarWidth - kPagePadding * 2.f,
                          windowSize.y - kHeaderHeight - kFooterHeight - kPagePadding * 0.5f);
    if (ImGui::BeginChild("##page", pageSize, ImGuiChildFlags_None)) {
        switch (mPage) {
        case MenuPage::Self:
            drawSelfPage(snapshot, configuration);
            break;
        case MenuPage::Visuals:
            drawVisualsPage(configuration);
            break;
        case MenuPage::Players:
            drawPlayersPage(snapshot);
            break;
        case MenuPage::Enemies:
            drawEnemiesPage(snapshot, catalogs);
            break;
        case MenuPage::Items:
            drawItemsPage(snapshot, catalogs);
            break;
        case MenuPage::Moons:
            drawMoonsPage(snapshot, catalogs);
            break;
        case MenuPage::Trolls:
            drawTrollsPage(snapshot);
            break;
        case MenuPage::Settings:
            drawSettingsPage(configuration, frame);
            break;
        }
    }
    ImGui::EndChild();

    drawFooter(runtimeStatus, configuration);

    ImGui::End();
    if (bodyFont) {
        ImGui::PopFont();
    }
}

void Interface::drawSidebar() {
    struct PageEntry {
        MenuPage page;
        Icon icon;
        const char* label;
    };
    constexpr std::array<PageEntry, 8> pages = {{
        {MenuPage::Self, Icon::Self, "Self"},
        {MenuPage::Visuals, Icon::Visuals, "Visuals"},
        {MenuPage::Players, Icon::Players, "Players"},
        {MenuPage::Enemies, Icon::Enemies, "Enemies"},
        {MenuPage::Items, Icon::Items, "Items"},
        {MenuPage::Moons, Icon::Moons, "Moons"},
        {MenuPage::Trolls, Icon::Trolls, "Trolls"},
        {MenuPage::Settings, Icon::Settings, "Settings"},
    }};

    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    ImFont* const titleFont = fonts().title;
    drawIcon(drawList, Icon::Sun, ImVec2(origin.x + 25.f, origin.y + 26.f), color(palette::kAccent),
             1.15f);
    if (titleFont) {
        drawList->AddText(titleFont, titleFont->FontSize, ImVec2(origin.x + 42.f, origin.y + 15.f),
                          color(palette::kAccent), "SOLAR");
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 2.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(42.f, 3.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
    ImGui::SetCursorPos(ImVec2(0.f, 58.f));
    for (const PageEntry& entry : pages) {
        const bool selected = mPage == entry.page;
        const ImVec4 tint = selected ? palette::kAccent : palette::kMuted;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? palette::kControl : ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_Text, tint);
        ImGui::SetCursorPosX(0.f);
        if (ImGui::Button(entry.label, ImVec2(kSidebarWidth, 28.f))) {
            mPage = entry.page;
            mCapturingMenuKey = false;
        }
        ImGui::PopStyleColor(2);
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        drawIcon(drawList, entry.icon, ImVec2(minimum.x + 25.f, (minimum.y + maximum.y) * 0.5f),
                 color(tint), 1.1f);
        if (selected) {
            drawList->AddRectFilled(minimum, ImVec2(minimum.x + 2.f, maximum.y),
                                    color(palette::kAccent));
        }
    }
    ImGui::PopStyleVar(3);
}

void Interface::drawHeader(const MenuSnapshot& snapshot, const MenuFrame& frame) {
    const char* title = "";
    Icon icon = Icon::Self;
    switch (mPage) {
    case MenuPage::Self:
        title = "Self";
        icon = Icon::Self;
        break;
    case MenuPage::Visuals:
        title = "Visuals";
        icon = Icon::Visuals;
        break;
    case MenuPage::Players:
        title = "Players";
        icon = Icon::Players;
        break;
    case MenuPage::Enemies:
        title = "Enemies";
        icon = Icon::Enemies;
        break;
    case MenuPage::Items:
        title = "Items";
        icon = Icon::Items;
        break;
    case MenuPage::Moons:
        title = "Moons";
        icon = Icon::Moons;
        break;
    case MenuPage::Trolls:
        title = "Trolls";
        icon = Icon::Trolls;
        break;
    case MenuPage::Settings:
        title = "Settings";
        icon = Icon::Settings;
        break;
    }
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float left = origin.x + kSidebarWidth + kPagePadding;
    const float right = origin.x + ImGui::GetWindowWidth() - kPagePadding;
    ImFont* const titleFont = fonts().title;
    drawIcon(drawList, icon, ImVec2(left + 10.f, origin.y + 20.f), color(palette::kAccent), 1.2f);
    if (titleFont) {
        drawList->AddText(titleFont, titleFont->FontSize, ImVec2(left + 26.f, origin.y + 9.f),
                          color(palette::kText), title);
    }
    ImFont* const captionFont = fonts().caption;
    if (!captionFont) {
        return;
    }
    const bool host = snapshot.session.world.has(WorldFeature::Host);
    std::array<char, 48> status{};
    std::snprintf(status.data(), status.size(), "%s  \xC2\xB7  %.0f fps", host ? "HOST" : "CLIENT",
                  static_cast<double>(frame.framesPerSecond));
    const ImVec2 statusSize =
        captionFont->CalcTextSizeA(captionFont->FontSize, 1000.f, 0.f, status.data());
    drawList->AddText(captionFont, captionFont->FontSize,
                      ImVec2(right - statusSize.x, origin.y + 14.f), color(palette::kMuted),
                      status.data());
    drawList->AddCircleFilled(ImVec2(right - statusSize.x - 9.f, origin.y + 20.5f), 3.f,
                              color(host ? palette::kGood : palette::kMuted), 10);
}

void Interface::drawFooter(std::string_view runtimeStatus, const MenuConfiguration& configuration) {
    ImFont* const captionFont = fonts().caption;
    if (!captionFont) {
        return;
    }
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float top = origin.y + ImGui::GetWindowHeight() - kFooterHeight;
    const float textY = top + (kFooterHeight - captionFont->FontSize) * 0.5f;
    const float capY = top + (kFooterHeight - 16.f) * 0.5f;
    const float left = origin.x + 16.f;
    float right = origin.x + ImGui::GetWindowWidth() - 14.f;
    const auto text = [&](const char* value, const ImVec4& tint) {
        right -= captionFont->CalcTextSizeA(captionFont->FontSize, 1000.f, 0.f, value).x;
        drawList->AddText(captionFont, captionFont->FontSize, ImVec2(right, textY), color(tint),
                          value);
        right -= 6.f;
    };
    const auto keyCap = [&](const char* key) {
        right -= keyCapWidth(key);
        drawKeyCap(drawList, ImVec2(right, capY), key);
        right -= 6.f;
    };
    text("close", palette::kFaint);
    keyCap("Esc");
    keyCap(menuKeyName(configuration.menuKey));
    right -= 8.f;
    std::array<char, 64> status{};
    std::snprintf(status.data(), status.size(), "%.*s",
                  static_cast<int>(std::min<std::size_t>(runtimeStatus.size(), 48U)),
                  runtimeStatus.empty() ? "" : runtimeStatus.data());
    text(status.data(), palette::kFaint);
    if (!mHint) {
        return;
    }
    const ImVec4 clip(left, top, right - 10.f, top + kFooterHeight);
    drawList->AddText(captionFont, captionFont->FontSize, ImVec2(left, textY),
                      color(palette::kText), mHint, nullptr, 0.f, &clip);
}

void Interface::drawPlayerPicker(PlayerPicker picker, const MenuSnapshot& snapshot,
                                 std::optional<std::uint64_t>& selected) {
    const PlayerSnapshot& players = snapshot.playerFrame.players;
    const PlayerSnapshotPlayer* const current = selected ? findPlayer(players, *selected) : nullptr;
    if (selected && (!current || current->dead)) {
        selected.reset();
    }
    std::size_t rows = picker.habitatLabel ? 1 : 0;
    for (const PlayerSnapshotPlayer& player : players) {
        if (player.dead) {
            continue;
        }
        ++rows;
        if (!selected && !picker.habitatLabel && (player.local || !localPlayer(players))) {
            selected = player.clientId;
        }
    }
    if (rows == 0) {
        return;
    }
    const float rowPitch = kListRowHeight + ImGui::GetStyle().ItemSpacing.y;
    const float height =
        static_cast<float>(std::min(rows, picker.visibleRows)) * rowPitch + kCardPadding * 2.f;
    if (beginCard(picker.id, ImVec2(0.f, height))) {
        if (picker.habitatLabel) {
            if (listRow({.label = picker.habitatLabel}, !selected)) {
                selected.reset();
            }
        }
        for (const PlayerSnapshotPlayer& player : players) {
            if (player.dead) {
                continue;
            }
            ImGui::PushID(static_cast<int>(player.clientId & 0x7fffffffU));
            if (listRow({.label = player.local ? "You" : player.name.c_str(),
                         .detail = locationName(snapshot.session.location(player.clientId))},
                        selected && *selected == player.clientId)) {
                selected = player.clientId;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void Interface::drawSelfPage(const MenuSnapshot& snapshot, MenuConfiguration& configuration) {
    const SessionState& session = snapshot.session;
    const float width = columnWidth();
    if (beginCard("##self_player", ImVec2(width, 0.f))) {
        sectionTitle(Icon::Move, "MOVEMENT");
        configSwitch("Infinite stamina", "Keeps your sprint meter full.",
                     configuration.infiniteStamina);
        configSwitch("No carry weight", "Carried items never slow you down.",
                     configuration.noWeight);
        constexpr std::array<const char*, 6> flyKeys = {"W", "A", "S", "D", "Space", "Ctrl"};
        if (switchRow("Fly", session.switches.has(SessionSwitch::Fly), flyKeys)) {
            queueAction({PlayerAction::ToggleFly, NoPlayerActionPayload{}});
        }
        noteHint(mHint, "W A S D to move, Space to rise, Ctrl or C to sink.");
        constexpr std::array<const char*, 2> emoteKeys = {"1", "2"};
        if (switchRow("Emote while moving", session.switches.has(SessionSwitch::EmoteWhileMoving),
                      emoteKeys)) {
            queueAction({PlayerAction::SlideTaunt, NoPlayerActionPayload{}});
        }
        noteHint(mHint, "Press 1 or 2 to emote without stopping.");
        sectionTitle(Icon::Battery, "GEAR");
        configSwitch("Infinite battery", "Keeps every battery item you carry fully charged.",
                     configuration.infiniteBattery);
        configSwitch("Extended inventory",
                     "Grows your inventory to 32 slots. Other players see only the first four.",
                     configuration.extendedInventory);
        sectionTitle(Icon::Sound, "SENSES");
        sessionSwitch("Third person", "Moves your camera behind your own character.",
                      session.switches.has(SessionSwitch::ThirdPerson),
                      {PlayerAction::ToggleThirdPerson, NoPlayerActionPayload{}});
        sessionSwitch("Hear everyone",
                      "Plays every player's voice at full volume, any distance, alive or dead.",
                      session.switches.has(SessionSwitch::HearEveryone),
                      {PlayerAction::ToggleHearEveryone, NoPlayerActionPayload{}});
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);
    if (beginCard("##self_status", ImVec2(0.f, 0.f))) {
        sectionTitle(Icon::Pulse, "STATUS");
        std::array<char, 32> value{};
        const float stamina = std::clamp(snapshot.playerFrame.sprintMeter, 0.f, 1.f);
        std::snprintf(value.data(), value.size(), "%.0f%%", static_cast<double>(stamina * 100.f));
        meterRow("Stamina", value.data(), stamina);
        const float carried = std::clamp(snapshot.playerFrame.carryWeight - 1.f, 0.f, 100.f);
        std::snprintf(value.data(), value.size(), "%.0f lb",
                      static_cast<double>(carried * kPoundsPerCarryUnit));
        meterRow("Carried weight", value.data(), carried);
        const PlayerSnapshotPlayer* const local = localPlayer(snapshot.playerFrame.players);
        const int health = local && !local->dead ? local->health : 0;
        std::snprintf(value.data(), value.size(), "%d", health);
        meterRow("Health", value.data(), static_cast<float>(health) / 100.f);
        sectionTitle(Icon::Bolt, "ACTIONS");
        const char* blocked = nullptr;
        if (!local) {
            blocked = "not in a game";
        } else if (local->dead) {
            blocked = "you are dead";
        }
        const PlayerTargetPayload self{local ? local->clientId : 0};
        command("Heal yourself", "Restores your health to full and stops bleeding.",
                {PlayerAction::Heal, self}, blocked);
        dangerCommand("Kill yourself", "Kills your own character right now.",
                      {PlayerAction::Kill, self}, blocked);
    }
    ImGui::EndChild();
}

void Interface::drawVisualsPage(MenuConfiguration& configuration) {
    struct CategoryEntry {
        EspCategory category;
        const char* label;
        const char* title;
        const char* hint;
        bool EspConfig::* enabled;
    };
    constexpr std::array<CategoryEntry, 6> categories = {{
        {EspCategory::Players, "Players", "PLAYERS", "Marks every other living player.",
         &EspConfig::players},
        {EspCategory::Scrap, "Scrap", "SCRAP", "Marks loose scrap that nobody is holding.",
         &EspConfig::items},
        {EspCategory::Enemies, "Enemies", "ENEMIES", "Marks every living enemy.",
         &EspConfig::monsters},
        {EspCategory::Entrance, "Main entrance", "THE MAIN ENTRANCE",
         "Marks the facility's main entrance doors.", &EspConfig::exits},
        {EspCategory::FireExits, "Fire exits", "FIRE EXITS", "Marks the facility's fire exits.",
         &EspConfig::fireExits},
        {EspCategory::Ship, "Ship", "THE SHIP", "Marks your ship.", &EspConfig::ships},
    }};

    EspConfig& esp = configuration.esp;
    const float width = columnWidth();
    if (beginCard("##visuals_marks", ImVec2(width, 0.f))) {
        sectionTitle(Icon::Visuals, "SHOW");
        for (const CategoryEntry& entry : categories) {
            configSwitch(entry.label, entry.hint, esp.*entry.enabled);
        }
        std::array<char, 40> colorTitle{};
        std::snprintf(colorTitle.data(), colorTitle.size(), "COLOR OF %s",
                      categories[static_cast<std::size_t>(mColorCategory)].title);
        sectionTitle(Icon::Palette, colorTitle.data());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
        for (const CategoryEntry& entry : categories) {
            const int index = static_cast<int>(entry.category);
            const std::array<float, 4>& swatch = espColor(esp, index);
            const bool selected = mColorCategory == entry.category;
            ImGui::PushID(index);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 1.f : 0.f);
            ImGui::PushStyleColor(ImGuiCol_Border, palette::kAccent);
            if (ImGui::ColorButton("##swatch", ImVec4(swatch[0], swatch[1], swatch[2], 1.f),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha |
                                       ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(34.f, 16.f))) {
                mColorCategory = entry.category;
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            noteHint(mHint, entry.label);
            ImGui::PopID();
            ImGui::SameLine(0.f, 5.f);
        }
        ImGui::NewLine();
        std::array<float, 4>& edited = espColor(esp, static_cast<int>(mColorCategory));
        constexpr std::array<const char*, 3> channels = {"Red", "Green", "Blue"};
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            int level = static_cast<int>(edited[channel] * 255.f + 0.5f);
            if (sliderRow(channels[channel], level, {.minimum = 0, .maximum = 255}, "%d")) {
                edited[channel] = static_cast<float>(level) / 255.f;
                markConfigChanged();
            }
        }
        if (mColorCategory == EspCategory::Scrap && esp.useScrapTiers) {
            noteText("Scrap uses its value colors while Color scrap by value is on.");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);
    if (beginCard("##visuals_options", ImVec2(0.f, 0.f))) {
        sectionTitle(Icon::Settings, "STYLE");
        configSwitch("Outlines", "Draws a glowing outline around each marked model.", esp.outlines);
        configSwitch("Names", "Writes the name above each mark.", esp.names);
        configSwitch("Scrap values", "Writes the sell value next to each piece of scrap.",
                     esp.values);
        configSwitch("Distance", "Writes how many meters away each mark is.", esp.distance);
        configSwitch("Tracer lines", "Draws a line from the bottom of your screen to each mark.",
                     esp.lines);
        configSwitch("Color scrap by value",
                     "Colors scrap by how much it sells for instead of the scrap color.",
                     esp.useScrapTiers);
        int maximumDistance = static_cast<int>(esp.maxDistance + 0.5f);
        if (sliderRow("Range", maximumDistance, {.minimum = 10, .maximum = 1000}, "%d m")) {
            esp.maxDistance = static_cast<float>(maximumDistance);
            markConfigChanged();
        }
        noteHint(mHint, "Marks farther away than this are hidden.");
        sectionTitle(Icon::Bell, "ALERTS");
        configSwitch("Death notifications", "Shows a popup whenever a player dies.",
                     esp.deathNotifications);
    }
    ImGui::EndChild();
}

void Interface::drawModelPanel(const MenuSnapshot& snapshot, PreviewKind kind,
                               std::optional<std::size_t> catalogIndex, std::string_view name) {
    if (beginCard("##model", ImVec2(0.f, 0.f))) {
        sectionTitle(Icon::Visuals, "MODEL");
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        size.y -= kRowHeight;
        const ImVec2 middle(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        drawIcon(drawList, Icon::Items, middle, color(palette::kFaint), 1.6f);
        const char* note = nullptr;
        if (!catalogIndex) {
            note = "Nothing selected";
        } else if (snapshot.session.world.has(WorldFeature::ModelMissing)) {
            note = "No solid model to show";
        }
        if (catalogIndex && size.x > 0.f && size.y > 0.f) {
            mPreview = MenuPreview{kind,
                                   static_cast<std::uint16_t>(*catalogIndex),
                                   {position.x, position.y, size.x, size.y}};
        }
        ImFont* const caption = fonts().caption;
        if (note && caption) {
            const ImVec2 noteSize = caption->CalcTextSizeA(caption->FontSize, 1000.f, 0.f, note);
            drawList->AddText(caption, caption->FontSize,
                              ImVec2(middle.x - noteSize.x * 0.5f, middle.y + 18.f),
                              color(palette::kMuted), note);
        }
        if (catalogIndex && !name.empty()) {
            const char* const begin = name.data();
            const char* const end = begin + name.size();
            const ImVec2 nameSize = ImGui::CalcTextSize(begin, end);
            const float bottom = position.y + size.y;
            const ImVec4 clip(position.x, bottom, position.x + size.x, bottom + kRowHeight);
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(position.x + std::max(0.f, (size.x - nameSize.x) * 0.5f),
                                     bottom + (kRowHeight - ImGui::GetFontSize()) * 0.5f),
                              color(palette::kText), begin, end, 0.f, &clip);
        }
    }
    ImGui::EndChild();
}

void Interface::drawPlayersPage(const MenuSnapshot& snapshot) {
    const SessionState& session = snapshot.session;
    const PlayerSnapshot& players = snapshot.playerFrame.players;
    if (players.empty()) {
        noteText("No players yet. Join or host a game first.");
        return;
    }
    if (mSelectedPlayer && !findPlayer(players, *mSelectedPlayer)) {
        mSelectedPlayer.reset();
    }
    if (!mSelectedPlayer) {
        mSelectedPlayer = players[0].clientId;
    }

    if (beginCard("##player_list", ImVec2(kListWidth, 0.f))) {
        sectionTitle(Icon::Players, "PLAYERS");
        for (const PlayerSnapshotPlayer& player : players) {
            const std::array<char, kPlayerListLabelCapacity> label = playerListLabel(player);
            ImGui::PushID(static_cast<int>(player.clientId & 0x7fffffffU));
            if (listRow({.label = label.data(),
                         .detail = player.dead ? "dead" : "",
                         .dot = player.dead ? palette::kDanger : palette::kGood},
                        *mSelectedPlayer == player.clientId)) {
                mSelectedPlayer = player.clientId;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);

    const bool detailVisible = beginCard("##player_detail", ImVec2(0.f, 0.f));
    const PlayerSnapshotPlayer* const selected = findPlayer(players, *mSelectedPlayer);
    if (!detailVisible || !selected) {
        ImGui::EndChild();
        return;
    }
    const PlayerSnapshotPlayer& player = *selected;
    const PlayerTargetPayload target{player.clientId};
    const NoPlayerActionPayload none{};
    const bool host = session.world.has(WorldFeature::Host);
    const bool canDie = session.world.has(WorldFeature::PlayersCanDie);
    const char* const dead = player.dead ? "player is dead" : nullptr;
    const char* const orbit = "blocked in orbit";
    const char* const remoteOrbit = player.local || canDie ? nullptr : orbit;

    sectionTitle(Icon::Info, "DETAILS");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
    ImGui::TextColored(palette::kMuted, "Steam %llu   \xC2\xB7   slot %llu",
                       static_cast<unsigned long long>(player.steamId),
                       static_cast<unsigned long long>(player.clientId));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
    if (player.dead) {
        ImGui::TextColored(palette::kDanger, "Dead");
    } else {
        ImGui::TextColored(palette::kGood, "Alive");
        ImGui::SameLine();
        ImGui::TextColored(palette::kMuted,
                           "\xC2\xB7   %d health   \xC2\xB7   %.0f insanity   \xC2\xB7   %s",
                           player.health, static_cast<double>(player.insanity),
                           locationName(session.location(player.clientId)));
    }

    sectionTitle(Icon::Self, "PLAYER");
    command("Teleport to player", "Moves you to where this player is standing.",
            {PlayerAction::TeleportTo, target}, player.local ? "that is you" : dead);
    dangerCommand("Kill player", "Deals lethal damage so the player dies normally.",
                  {PlayerAction::Kill, target}, dead ? dead : remoteOrbit);
    command("Heal player",
            player.local ? "Restores your health to full and stops bleeding."
                         : "Restores this player's health to full. They hear a hit when it lands.",
            {PlayerAction::Heal, target}, dead ? dead : remoteOrbit);
    if (player.local) {
        command("Clear insanity", "Resets your fear level to zero.",
                {PlayerAction::ClearInsanity, target}, dead);
        command("Max insanity", "Raises your fear level to its maximum.",
                {PlayerAction::MaxInsanity, target}, dead);
    } else {
        const char* const blocked = host ? dead : "host only";
        command("Mark for the ghost girl",
                "Makes the ghost girl far more likely to pick this player to haunt.",
                {PlayerAction::MaxInsanity, target}, blocked);
        command("Unmark for the ghost girl", "Removes the ghost girl mark from this player.",
                {PlayerAction::ClearInsanity, target}, blocked);
    }

    sectionTitle(Icon::Enemies, "ENEMIES");
    const char* const noEnemies =
        player.dead
            ? dead
            : (session.world.has(WorldFeature::EnemiesAlive) ? nullptr : "no enemies alive");
    const bool luredHere = session.lure && session.lure->clientId == player.clientId;
    const bool timedLure = luredHere && session.lure->secondsLeft.has_value();
    const bool persistentLure = luredHere && !session.lure->secondsLeft.has_value();
    sessionSwitch("Lure enemies for 30 s",
                  "Draws enemies to this player for 30 seconds, then lets them go.", timedLure,
                  {PlayerAction::LureAllEnemies, target}, timedLure ? nullptr : noEnemies);
    sessionSwitch("Persistent targeting",
                  "Every enemy that can reach this player hunts only them until you turn it off.",
                  persistentLure, {PlayerAction::TogglePersistentLure, target},
                  persistentLure ? nullptr : noEnemies);
    command("Teleport enemies here",
            "Moves every enemy that lives in this player's area next to them.",
            {PlayerAction::TeleportAllEnemies, target}, noEnemies);

    sectionTitle(Icon::Players, "EVERYONE");
    const bool others = players.size() > 1;
    dangerCommand("Kill everyone", "Kills every player, including you.",
                  {PlayerAction::KillAll, none}, others && !canDie ? orbit : nullptr);
    dangerCommand("Kill everyone but me", "Kills every other player.",
                  {PlayerAction::KillAllExceptLocal, none},
                  others ? (canDie ? nullptr : orbit) : "no other players");
    ImGui::EndChild();
}

void Interface::drawEnemiesPage(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    if (mSpawnKind == SpawnKind::Traps) {
        drawTrapTypes(snapshot, catalogs);
    } else {
        drawEnemyTypes(snapshot, catalogs);
    }
}

void Interface::drawEnemyTypes(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    const SessionState& session = snapshot.session;
    const bool host = session.world.has(WorldFeature::Host);
    const bool previews = session.world.has(WorldFeature::ModelPreviews);
    const std::size_t enemyCount = catalogs.enemyCount();
    if (mEnemyTarget && !findPlayer(snapshot.playerFrame.players, *mEnemyTarget)) {
        mEnemyTarget.reset();
    }
    std::optional<EnemyHabitat> requiredHabitat;
    if (mEnemyTarget) {
        requiredHabitat = session.location(*mEnemyTarget) == PlayerLocation::Facility
                              ? EnemyHabitat::Facility
                              : EnemyHabitat::Outside;
    }
    const auto livesThere = [&](const EnemyCatalogEntry& entry) {
        return entry.traits.loadedMap == EnemyMapSupport::Supported &&
               (!requiredHabitat || entry.traits.habitat == *requiredHabitat);
    };
    const auto summonable = [&](const EnemyCatalogEntry& entry) {
        return requiredHabitat && entry.activeCount > 0 && entry.traits.habitat == *requiredHabitat;
    };

    std::optional<EnemyCatalogEntry> chosen;
    std::optional<EnemyCatalogEntry> firstListed;
    if (beginCard("##enemy_types", ImVec2(kListWidth, 0.f))) {
        const float half = (ImGui::GetContentRegionAvail().x - 4.f) * 0.5f;
        segmentButton("Enemies", true, half);
        ImGui::SameLine(0.f, 4.f);
        if (segmentButton("Traps", false, half)) {
            mSpawnKind = SpawnKind::Traps;
        }
        noteHint(mHint, "Turrets, landmines and the other map traps.");
        sectionTitle(Icon::List, "TYPES THAT WORK THERE");
        std::array<char, kRuntimeCatalogNameCapacity + 1> label{};
        std::array<char, 24> detail{};
        for (std::size_t index = 0; index < enemyCount; ++index) {
            const EnemyCatalogLookup lookup = catalogs.enemyAt(index);
            if (lookup.result != CatalogLookupResult::Found ||
                (!livesThere(lookup.entry) && !summonable(lookup.entry))) {
                continue;
            }
            const EnemyCatalogEntry& entry = lookup.entry;
            if (!firstListed) {
                firstListed = entry;
            }
            std::snprintf(label.data(), label.size(), "%.*s", static_cast<int>(entry.name.size()),
                          entry.name.data());
            if (entry.activeCount > 0) {
                std::snprintf(detail.data(), detail.size(), "%u alive", entry.activeCount);
            } else if (requiredHabitat) {
                detail[0] = '\0';
            } else {
                std::snprintf(detail.data(), detail.size(), "%s",
                              entry.traits.habitat == EnemyHabitat::Outside ? "outside"
                                                                            : "facility");
            }
            ImGui::PushID(static_cast<int>(index));
            if (listRow({.label = label.data(), .detail = detail.data()},
                        mSelectedEnemyType == index)) {
                mSelectedEnemyType = index;
            }
            ImGui::PopID();
            if (mSelectedEnemyType == index) {
                chosen = entry;
            }
        }
        if (!firstListed) {
            if (enemyCount == 0) {
                noteText("Enemy types appear once you are in a lobby.");
            } else {
                noteText(mEnemyTarget ? "No enemy type can live where this player is."
                                      : "Land on a moon first. Enemies need a loaded map.");
            }
        }
    }
    ImGui::EndChild();
    if (!chosen && firstListed) {
        chosen = firstListed;
        mSelectedEnemyType = firstListed->id.index;
    }
    ImGui::SameLine(0.f, kColumnGap);

    if (beginCard("##enemy_actions", ImVec2(previews ? kControlsWidth : 0.f, 0.f))) {
        sectionTitle(Icon::Pin, "WHERE");
        drawPlayerPicker({.id = "##enemy_target",
                          .habitatLabel = "Random spot in its own area",
                          .visibleRows = 6},
                         snapshot, mEnemyTarget);
        sliderRow("Count", mEnemyCount, {.minimum = 1, .maximum = kMaximumSpawnCount}, "%d");
        sliderRow("Distance", mEnemyDistanceMeters,
                  {.minimum = 0, .maximum = kMaximumSpawnDistanceMeters},
                  mEnemyDistanceMeters == 0 ? "next to them" : "about %d m away");
        noteHint(mHint, "How far from the chosen player the enemies appear.");

        const std::uint8_t count = static_cast<std::uint8_t>(mEnemyCount);
        const std::uint8_t distance = static_cast<std::uint8_t>(mEnemyDistanceMeters);
        const char* const spawnHint = "Creates new enemies of this type where you chose.";
        const char* spawnBlocked = nullptr;
        if (!host) {
            spawnBlocked = "host only";
        } else if (!chosen) {
            spawnBlocked = "nothing can live there";
        } else if (!livesThere(*chosen)) {
            spawnBlocked = "cannot live there";
        }
        if (spawnBlocked || !chosen) {
            blockedRow("Spawn", spawnHint, spawnBlocked);
        } else if (mEnemyTarget) {
            command("Spawn at player", spawnHint,
                    {PlayerAction::SpawnEnemyAtPlayer,
                     EnemyAtPlayerPayload{chosen->id, count, *mEnemyTarget, distance}});
        } else {
            command("Spawn", spawnHint,
                    {PlayerAction::SpawnEnemy, EnemySpawnPayload{chosen->id, count}});
        }
        const char* const bringHint =
            "Moves up to Count living enemies of this type to the player.";
        if (mEnemyTarget && chosen && summonable(*chosen)) {
            command("Bring living ones here", bringHint,
                    {PlayerAction::SummonEnemyTypeAtPlayer,
                     EnemyAtPlayerPayload{chosen->id, count, *mEnemyTarget, distance}});
        } else {
            blockedRow("Bring living ones here", bringHint,
                       mEnemyTarget ? "none alive there" : "pick a player first");
        }

        sectionTitle(Icon::Enemies, "EVERY LIVING ENEMY");
        const char* const noEnemies =
            session.world.has(WorldFeature::EnemiesAlive) ? nullptr : "no enemies alive";
        dangerCommand("Kill all enemies",
                      host ? "Kills every enemy and removes the ones that cannot die."
                           : "Kills every enemy that can die.",
                      {PlayerAction::KillAllEnemies, NoPlayerActionPayload{}}, noEnemies);
        command("Stun all enemies for 60 s",
                "Freezes every enemy that can be stunned. Touching one can still hurt.",
                {PlayerAction::StunAllEnemies, NoPlayerActionPayload{}}, noEnemies);
    }
    ImGui::EndChild();
    if (previews) {
        ImGui::SameLine(0.f, kColumnGap);
        drawModelPanel(snapshot, PreviewKind::Enemy,
                       chosen ? std::optional<std::size_t>{chosen->id.index} : std::nullopt,
                       chosen ? chosen->name : std::string_view{});
    }
}

void Interface::drawTrapTypes(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    const SessionState& session = snapshot.session;
    const bool previews = session.world.has(WorldFeature::ModelPreviews);
    const std::size_t trapCount = catalogs.hazardCount();
    if (mSelectedTrapType >= trapCount) {
        mSelectedTrapType = trapCount == 0 ? 0 : trapCount - 1;
    }
    if (beginCard("##trap_types", ImVec2(kListWidth, 0.f))) {
        const float half = (ImGui::GetContentRegionAvail().x - 4.f) * 0.5f;
        if (segmentButton("Enemies", false, half)) {
            mSpawnKind = SpawnKind::Enemies;
        }
        ImGui::SameLine(0.f, 4.f);
        segmentButton("Traps", true, half);
        sectionTitle(Icon::List, "TRAP TYPES");
        if (trapCount == 0) {
            noteText("Trap types appear once you are in a lobby.");
        }
        std::array<char, kRuntimeCatalogNameCapacity + 1> label{};
        for (std::size_t index = 0; index < trapCount; ++index) {
            const HazardCatalogLookup lookup = catalogs.hazardAt(index);
            if (lookup.result != CatalogLookupResult::Found) {
                continue;
            }
            std::snprintf(label.data(), label.size(), "%.*s",
                          static_cast<int>(lookup.entry.name.size()), lookup.entry.name.data());
            ImGui::PushID(static_cast<int>(index));
            if (listRow({.label = label.data()}, mSelectedTrapType == index)) {
                mSelectedTrapType = index;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);

    const HazardCatalogLookup chosen = catalogs.hazardAt(mSelectedTrapType);
    const bool found = chosen.result == CatalogLookupResult::Found;
    if (beginCard("##trap_actions", ImVec2(previews ? kControlsWidth : 0.f, 0.f))) {
        sectionTitle(Icon::Pin, "NEAR");
        drawPlayerPicker({.id = "##trap_target", .habitatLabel = nullptr, .visibleRows = 5},
                         snapshot, mTrapTarget);
        sliderRow("Count", mTrapCount, {.minimum = 1, .maximum = kMaximumSpawnCount}, "%d");
        sliderRow("Distance", mTrapDistanceMeters,
                  {.minimum = 0, .maximum = kMaximumSpawnDistanceMeters},
                  mTrapDistanceMeters == 0 ? "at their feet" : "about %d m away");
        noteHint(mHint, "How far from the player the traps are placed.");
        const char* const placeHint =
            "Creates working traps of this type on the floor near the player.";
        const char* blocked = nullptr;
        if (!session.world.has(WorldFeature::Host)) {
            blocked = "host only";
        } else if (!session.world.has(WorldFeature::MoonLoaded)) {
            blocked = "land on a moon first";
        } else if (!mTrapTarget) {
            blocked = "no player alive";
        } else if (!found) {
            blocked = "nothing selected";
        }
        if (blocked || !mTrapTarget || !found) {
            blockedRow("Place near player", placeHint, blocked);
        } else {
            command("Place near player", placeHint,
                    {PlayerAction::SpawnHazardAtPlayer,
                     HazardAtPlayerPayload{chosen.entry.id, static_cast<std::uint8_t>(mTrapCount),
                                           *mTrapTarget,
                                           static_cast<std::uint8_t>(mTrapDistanceMeters)}});
        }
    }
    ImGui::EndChild();
    if (previews) {
        ImGui::SameLine(0.f, kColumnGap);
        drawModelPanel(snapshot, PreviewKind::Trap,
                       found ? std::optional<std::size_t>{mSelectedTrapType} : std::nullopt,
                       found ? chosen.entry.name : std::string_view{});
    }
}

void Interface::drawItemsPage(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    const SessionState& session = snapshot.session;
    const bool host = session.world.has(WorldFeature::Host);
    const bool previews = session.world.has(WorldFeature::ModelPreviews);
    const std::size_t itemCount = catalogs.itemCount();
    if (mSelectedItemType >= itemCount) {
        mSelectedItemType = itemCount == 0 ? 0 : itemCount - 1;
    }
    if (beginCard("##item_types", ImVec2(kListWidth, 0.f))) {
        sectionTitle(Icon::List, "ITEM TYPES");
        if (itemCount == 0) {
            noteText("Item types appear once you are in a lobby.");
        }
        std::array<char, kRuntimeCatalogNameCapacity + 1> label{};
        for (std::size_t index = 0; index < itemCount; ++index) {
            const ItemCatalogLookup lookup = catalogs.itemAt(index);
            if (lookup.result != CatalogLookupResult::Found) {
                continue;
            }
            std::snprintf(label.data(), label.size(), "%.*s",
                          static_cast<int>(lookup.entry.name.size()), lookup.entry.name.data());
            ImGui::PushID(static_cast<int>(index));
            if (listRow({.label = label.data()}, mSelectedItemType == index)) {
                mSelectedItemType = index;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);

    const ItemCatalogLookup chosen = catalogs.itemAt(mSelectedItemType);
    const bool found = chosen.result == CatalogLookupResult::Found;
    if (beginCard("##item_actions", ImVec2(previews ? kControlsWidth : 0.f, 0.f))) {
        sectionTitle(Icon::Self, "GIVE TO");
        drawPlayerPicker({.id = "##item_target", .habitatLabel = nullptr, .visibleRows = 4},
                         snapshot, mItemTarget);
        sliderRow("Count", mItemCount, {.minimum = 1, .maximum = kMaximumSpawnCount}, "%d");
        if (switchRow("Set scrap value", mItemValueFixed)) {
            mItemValueFixed = !mItemValueFixed;
        }
        noteHint(mHint, "Off rolls each scrap item's normal random value.");
        if (mItemValueFixed) {
            sliderRow("Value", mItemValue, {.minimum = 0, .maximum = kMaximumFixedScrapValue},
                      "$%d", ImGuiSliderFlags_Logarithmic);
            noteHint(mHint, "Every spawned scrap item sells for exactly this much.");
        }
        const char* const hostOnly = host ? nullptr : "host only";
        const char* const noPlayer = mItemTarget ? nullptr : "no player alive";
        const char* const spawnHint = "Drops new items on the floor in front of the player.";
        const char* const spawnBlocked =
            hostOnly ? hostOnly : (noPlayer ? noPlayer : (found ? nullptr : "nothing selected"));
        if (spawnBlocked || !mItemTarget || !found) {
            blockedRow("Spawn at player", spawnHint, spawnBlocked);
        } else {
            const std::optional<int> value =
                mItemValueFixed ? std::optional<int>{mItemValue} : std::nullopt;
            command("Spawn at player", spawnHint,
                    {PlayerAction::SpawnItemAtPlayer,
                     ItemAtPlayerPayload{chosen.entry.id, static_cast<std::uint8_t>(mItemCount),
                                         *mItemTarget, value}});
        }
        const char* const bringHint =
            "Moves every item that nobody is holding to the player's feet.";
        if (hostOnly || !mItemTarget) {
            blockedRow("Bring every loose item here", bringHint, hostOnly ? hostOnly : noPlayer);
        } else {
            command("Bring every loose item here", bringHint,
                    {PlayerAction::TeleportItemsToPlayer, PlayerTargetPayload{*mItemTarget}});
        }

        sectionTitle(Icon::Building, "COMPANY");
        command("Ship scrap to the counter",
                "Places every piece of scrap in the ship on the company's sell counter.",
                {PlayerAction::DepositShipScrap, NoPlayerActionPayload{}},
                session.world.has(WorldFeature::CompanyDesk) ? nullptr : "company only");

        sectionTitle(Icon::Coin, "CREDITS");
        sliderRow("Amount", mCreditAmount,
                  {.minimum = 1, .maximum = kMaximumTerminalCreditsPerAction}, "%d",
                  ImGuiSliderFlags_Logarithmic);
        command("Add credits", "Adds this many credits to the crew's terminal balance.",
                {PlayerAction::AddTerminalCredits, TerminalCreditsPayload{mCreditAmount}},
                hostOnly
                    ? hostOnly
                    : (session.world.has(WorldFeature::Terminal) ? nullptr : "not in a lobby"));
    }
    ImGui::EndChild();
    if (previews) {
        ImGui::SameLine(0.f, kColumnGap);
        drawModelPanel(snapshot, PreviewKind::Item,
                       found ? std::optional<std::size_t>{mSelectedItemType} : std::nullopt,
                       found ? chosen.entry.name : std::string_view{});
    }
}

void Interface::drawMoonsPage(const MenuSnapshot& snapshot, const RuntimeCatalogs& catalogs) {
    const bool previews = snapshot.session.world.has(WorldFeature::ModelPreviews);
    const std::size_t moonCount = catalogs.moonCount();
    if (mSelectedMoon >= moonCount) {
        mSelectedMoon = moonCount == 0 ? 0 : moonCount - 1;
    }
    if (beginCard("##moon_list", ImVec2(kListWidth, 0.f))) {
        sectionTitle(Icon::Moons, "MOONS");
        if (moonCount == 0) {
            noteText("Moons appear once you are in a lobby.");
        }
        std::array<char, kRuntimeCatalogNameCapacity + 1> label{};
        for (std::size_t index = 0; index < moonCount; ++index) {
            const MoonCatalogLookup lookup = catalogs.moonAt(index);
            if (lookup.result != CatalogLookupResult::Found) {
                continue;
            }
            std::snprintf(label.data(), label.size(), "%.*s",
                          static_cast<int>(lookup.entry.name.size()), lookup.entry.name.data());
            ImGui::PushID(static_cast<int>(index));
            if (listRow({.label = label.data()}, mSelectedMoon == index)) {
                mSelectedMoon = index;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);

    const MoonCatalogLookup chosen = catalogs.moonAt(mSelectedMoon);
    const bool found = chosen.result == CatalogLookupResult::Found;
    if (beginCard("##moon_actions", ImVec2(previews ? kControlsWidth : 0.f, 0.f))) {
        sectionTitle(Icon::Route, "ROUTE");
        const char* const routeHint = "Sets course for the selected moon at no cost.";
        if (!found) {
            blockedRow("Route the ship here", routeHint, "nothing selected");
        } else {
            command("Route the ship here", routeHint,
                    {PlayerAction::TravelToMoon, MoonTravelPayload{chosen.entry.id}},
                    snapshot.session.world.has(WorldFeature::ShipInOrbit) ? nullptr : "orbit only");
        }
        sectionTitle(Icon::Info, "NOTES");
        noteText("Every moon the game knows is listed, including ones the terminal hides.");
        noteText("(no map) moons have no terrain in the game files and land on Artifice's map "
                 "with their own settings.");
        noteText("(unlisted) moons only work while you host with nobody else connected.");
    }
    ImGui::EndChild();
    if (previews) {
        ImGui::SameLine(0.f, kColumnGap);
        drawModelPanel(snapshot, PreviewKind::Moon,
                       found ? std::optional<std::size_t>{mSelectedMoon} : std::nullopt,
                       found ? chosen.entry.name : std::string_view{});
    }
}

void Interface::drawTrollsPage(const MenuSnapshot& snapshot) {
    const SessionState& session = snapshot.session;
    const auto has = [&](WorldFeature feature) { return session.world.has(feature); };
    const auto on = [&](SessionSwitch state) { return session.switches.has(state); };
    const auto needs = [&](WorldFeature feature, const char* reason) {
        return has(feature) ? nullptr : reason;
    };
    const auto whileOff = [&](SessionSwitch state, const char* reason) {
        return on(state) ? nullptr : reason;
    };
    const auto toggle = [&](PlayerAction action) {
        return PlayerActionRequest{action, NoPlayerActionPayload{}};
    };
    const char* const hostOnly = has(WorldFeature::Host) ? nullptr : "host only";
    const char* const orbitOnly = has(WorldFeature::ShipInOrbit) ? nullptr : "orbit only";
    const char* const lobby = "not in a lobby";

    if (beginCard("##trolls_ship", ImVec2(columnWidth(), 0.f))) {
        sectionTitle(Icon::Ship, "SHIP");
        sessionSwitch("Ship horn", "Holds the ship's loud horn down.", on(SessionSwitch::ShipHorn),
                      toggle(PlayerAction::ToggleShipHorn),
                      whileOff(SessionSwitch::ShipHorn,
                               needs(WorldFeature::ShipHorn, "no horn on this ship")));
        sessionSwitch("Ship lights", "Switches the ship's ceiling lights for everyone.",
                      on(SessionSwitch::ShipLights), toggle(PlayerAction::ToggleShipLights),
                      needs(WorldFeature::ShipLights, lobby));
        sessionSwitch("Terminal noise spam", "Plays the terminal's error beep ten times a second.",
                      on(SessionSwitch::TerminalSound), toggle(PlayerAction::ToggleTerminalSound),
                      whileOff(SessionSwitch::TerminalSound, needs(WorldFeature::Terminal, lobby)));
        const char* const doorBlocked = needs(WorldFeature::ShipDoor, lobby);
        sessionSwitch(
            "Door buttons in orbit",
            "Lets you open the hangar door in space. Only you see it move.",
            on(SessionSwitch::ShipDoorButtons), toggle(PlayerAction::OpenShipDoorSpace),
            whileOff(SessionSwitch::ShipDoorButtons, doorBlocked ? doorBlocked : orbitOnly));
        sessionSwitch("Plushie squeak spam",
                      "Squeezes the pajama man plushie in the ship over and over.",
                      on(SessionSwitch::PlushieSpam),
                      {PlayerAction::TogglePjManSpam,
                       PlushieIntervalPayload{std::chrono::milliseconds{
                           static_cast<int>(mPlushieIntervalSeconds * 1000.f)}}},
                      whileOff(SessionSwitch::PlushieSpam,
                               needs(WorldFeature::Plushie, "no plushie on this ship")));
        if (!on(SessionSwitch::PlushieSpam)) {
            sliderRow("Squeak every", mPlushieIntervalSeconds, {.minimum = 0.f, .maximum = 1.f},
                      "%.2f s");
        }
        dangerCommand("Fire the whole crew",
                      "Ejects everyone into space and resets the save, like missing quota.",
                      toggle(PlayerAction::EjectEveryone), hostOnly ? hostOnly : orbitOnly);

        sectionTitle(Icon::Car, "CRUISER");
        const char* const noCruiser = needs(WorldFeature::Cruiser, "no cruiser");
        sessionSwitch("Cruiser horn", "Holds the cruiser's horn down.",
                      on(SessionSwitch::CruiserHorn), toggle(PlayerAction::ToggleCarHorn),
                      whileOff(SessionSwitch::CruiserHorn, noCruiser));
        sessionSwitch("Ship magnet", "Switches the magnet that holds the cruiser to the ship.",
                      on(SessionSwitch::CruiserMagnet), toggle(PlayerAction::ToggleVehicleMagnet),
                      noCruiser);
        command("Blow up the cruiser", "Destroys the cruiser in an explosion.",
                toggle(PlayerAction::ExplodeCruiser), noCruiser);

        sectionTitle(Icon::Coin, "COMPANY");
        const char* const noDesk = needs(WorldFeature::CompanyDesk, "company only");
        command("Tentacle attack", "Makes the company monster grab whoever stands at the counter.",
                toggle(PlayerAction::ForceTentacleAttack), noDesk);
        sessionSwitch("Counter door spam", "Slams the sell counter's door open ten times a second.",
                      on(SessionSwitch::DeskDoorSound),
                      toggle(PlayerAction::ToggleDepositDeskSound),
                      whileOff(SessionSwitch::DeskDoorSound, noDesk ? noDesk : hostOnly));
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);
    if (beginCard("##trolls_map", ImVec2(0.f, 0.f))) {
        sectionTitle(Icon::Building, "FACILITY");
        const char* const noMines = needs(WorldFeature::Landmines, "no landmines here");
        sessionSwitch("Landmines armed", "Arms or disarms every landmine.",
                      on(SessionSwitch::LandminesArmed), toggle(PlayerAction::ToggleAllMines),
                      noMines);
        command("Detonate landmines", "Explodes every landmine right now.",
                toggle(PlayerAction::BlowUpAllMines), noMines);
        const char* const noTurrets = needs(WorldFeature::Turrets, "no turrets here");
        sessionSwitch("Turrets active", "Switches every turret on or off.",
                      on(SessionSwitch::TurretsActive), toggle(PlayerAction::ToggleAllTurrets),
                      noTurrets);
        command("Send turrets berserk",
                "Makes every turret spin and fire wildly for a few seconds.",
                toggle(PlayerAction::BerserkAllTurrets), noTurrets);
        sessionSwitch("Facility power", "Switches the facility's lights and powered doors.",
                      on(SessionSwitch::FacilityPower), toggle(PlayerAction::ToggleFactoryLights),
                      hostOnly ? hostOnly : needs(WorldFeature::FacilityPower, "no facility here"));
        command("Collapse the main bridge", "Drops the large outdoor bridge.",
                toggle(PlayerAction::ForceBridgeFall),
                hostOnly ? hostOnly : needs(WorldFeature::MainBridge, "no bridge here"));
        command("Collapse the small bridge", "Drops the small wooden bridge.",
                toggle(PlayerAction::ForceSmallBridgeFall),
                needs(WorldFeature::SmallBridge, "no bridge here"));
        command("Press the elevator button", "Sends the mineshaft elevator to the other floor.",
                toggle(PlayerAction::ToggleMineshaftElevator),
                needs(WorldFeature::MineshaftElevator, "no elevator here"));
        command(
            "Hoarding bug infestation", "Turns the facility's spawns into a hoarding bug swarm.",
            toggle(PlayerAction::SpawnHoardingBugInfestation),
            hostOnly ? hostOnly : needs(WorldFeature::HoardingBugNest, "no hoarding bugs here"));

        sectionTitle(Icon::Cog, "GEAR");
        const char* const noShotguns = needs(WorldFeature::Shotguns, "no shotguns here");
        command("Fire every shotgun", "Fires every shotgun on the map once.",
                toggle(PlayerAction::ShootAllShotguns), noShotguns);
        sessionSwitch("Shotgun spam", "Fires every shotgun ten times a second.",
                      on(SessionSwitch::ShotgunSpam), toggle(PlayerAction::ToggleShotgunSpam),
                      whileOff(SessionSwitch::ShotgunSpam, noShotguns));
        command("Explode every jetpack", "Blows up every jetpack on the map.",
                toggle(PlayerAction::ExplodeAllJetpacks),
                needs(WorldFeature::Jetpacks, "no jetpacks here"));
        sessionSwitch(
            "Explode jetpacks when held", "Blows up any jetpack the moment someone is holding it.",
            on(SessionSwitch::JetpackTrap), toggle(PlayerAction::ToggleExplodeJetpacksOnGrab));
        command("Turn masks into Masked",
                "Spawns a Masked enemy from every mask that can become one.",
                toggle(PlayerAction::SpawnMaskedEnemy),
                needs(WorldFeature::HauntedMasks, "no usable masks here"));
    }
    ImGui::EndChild();
}

void Interface::drawSettingsPage(MenuConfiguration& configuration, const MenuFrame& frame) {
    if (mCapturingMenuKey && frame.pressedKey) {
        if (configuration.menuKey != *frame.pressedKey) {
            configuration.menuKey = *frame.pressedKey;
            markConfigChanged();
        }
        mCapturingMenuKey = false;
    }
    if (beginCard("##settings", ImVec2(columnWidth(), 0.f))) {
        sectionTitle(Icon::Key, "MENU KEY");
        if (ImGui::Button(mCapturingMenuKey ? "Press the new key" : "Open and close the menu",
                          ImVec2(-1.f, kRowHeight))) {
            mCapturingMenuKey = !mCapturingMenuKey;
        }
        if (!mCapturingMenuKey) {
            const char* const key = menuKeyName(configuration.menuKey);
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            drawKeyCap(
                ImGui::GetWindowDrawList(),
                ImVec2(maximum.x - keyCapWidth(key) - 8.f, (minimum.y + maximum.y) * 0.5f - 8.f),
                key);
        }
        noteHint(mHint, mCapturingMenuKey
                            ? "Press a letter, digit, F key, Insert, Tab, Enter or Space. Click "
                              "again to cancel."
                            : "Click, then press the key that should open and close this menu.");

        sectionTitle(Icon::Gauge, "PERFORMANCE");
        int refreshRate = static_cast<int>(configuration.pollRate + 0.5f);
        if (sliderRow("Player data", refreshRate, {.minimum = 10, .maximum = 90},
                      "%d times a second")) {
            configuration.pollRate = static_cast<float>(refreshRate);
            markConfigChanged();
        }
        noteHint(mHint, "How often the menu re-reads players, stamina and weight.");
        if (sliderRow("Self cheats", configuration.tickIntervalMs, {.minimum = 33, .maximum = 500},
                      "every %d ms")) {
            markConfigChanged();
        }
        noteHint(mHint, "How often infinite stamina and no carry weight are re-applied.");

        sectionTitle(Icon::Save, "CONFIGURATION");
        if (commandRow("Save settings now", CommandTone::Neutral)) {
            requestConfigSave();
        }
        noteHint(mHint, "Settings also save by themselves shortly after every change.");
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, kColumnGap);
    if (beginCard("##keys", ImVec2(0.f, 0.f))) {
        struct KeyRow {
            const char* label;
            std::span<const char* const> keys;
        };
        const char* const menuKey = menuKeyName(configuration.menuKey);
        const std::array<const char*, 1> openKeys = {menuKey};
        constexpr std::array<const char*, 1> closeKeys = {"Esc"};
        constexpr std::array<const char*, 4> moveKeys = {"W", "A", "S", "D"};
        constexpr std::array<const char*, 1> riseKeys = {"Space"};
        constexpr std::array<const char*, 2> sinkKeys = {"Ctrl", "C"};
        constexpr std::array<const char*, 2> emoteKeys = {"1", "2"};
        const std::array<KeyRow, 6> rows = {{
            {"Open or close the menu", openKeys},
            {"Close the menu", closeKeys},
            {"Fly around", moveKeys},
            {"Fly up", riseKeys},
            {"Fly down", sinkKeys},
            {"Emote while moving", emoteKeys},
        }};
        sectionTitle(Icon::Keyboard, "KEYS");
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        for (const KeyRow& row : rows) {
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, kRowHeight));
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            drawList->AddText(
                ImVec2(minimum.x + 8.f, minimum.y + (kRowHeight - ImGui::GetFontSize()) * 0.5f),
                color(palette::kText), row.label);
            float right = maximum.x - 8.f;
            for (std::size_t index = row.keys.size(); index > 0; --index) {
                const char* const key = row.keys[index - 1];
                right -= keyCapWidth(key);
                drawKeyCap(drawList, ImVec2(right, minimum.y + (kRowHeight - 16.f) * 0.5f), key);
                right -= 3.f;
            }
        }
    }
    ImGui::EndChild();
}
}
