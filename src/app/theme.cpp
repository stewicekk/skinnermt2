// Wave 35 theme module — see src/app/theme.hpp for the contract.
// Everything that already existed as a token in m2rig::themeTokens() is
// READ from that table (tests/test_ui_model.cpp pins the values); chrome
// that never got a token stays literal and is marked "one-off" below.
#include "theme.hpp"

namespace m2rig::theme {

namespace {

ImVec4 toVec4(const Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

// Packed color from a token (exact IM_COL32 semantics: byte round-trip).
ImU32 toCol32(const Rgba& c) {
    const ImU32 r = static_cast<ImU32>(c.r * 255.0f + 0.5f);
    const ImU32 g = static_cast<ImU32>(c.g * 255.0f + 0.5f);
    const ImU32 b = static_cast<ImU32>(c.b * 255.0f + 0.5f);
    const ImU32 a = static_cast<ImU32>(c.a * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

}  // namespace

// --- semantic feedback (formally panels.cpp Theme namespace) ----------------
const ImVec4& ok() {
    static const ImVec4 v = toVec4(themeTokens().success);
    return v;
}
const ImVec4& warn() {
    static const ImVec4 v = toVec4(themeTokens().warn);
    return v;
}
const ImVec4& err() {
    static const ImVec4 v = toVec4(themeTokens().danger);
    return v;
}
const ImVec4& info() {
    static const ImVec4 v = toVec4(themeTokens().info);
    return v;
}
ImVec4 statusColor(const std::string& kind) {
    // Same vocabulary as App status + toasts (statusKindFrom degrades
    // unknown kinds to Info — never transparent, never black).
    switch (statusKindFrom(kind)) {
        case StatusKind::Success: return ok();
        case StatusKind::Warning: return warn();
        case StatusKind::Error: return err();
        case StatusKind::Info: break;
    }
    return info();
}

// --- destructive button family ---------------------------------------------
const ImVec4& danger() {
    static const ImVec4 v = toVec4(themeTokens().destructive);
    return v;
}
const ImVec4& dangerHover() {
    static const ImVec4 v = toVec4(themeTokens().destructiveHover);
    return v;
}
const ImVec4& dangerActive() {
    static const ImVec4 v = toVec4(themeTokens().destructiveActive);
    return v;
}

// --- viewport clear (single source for the former 4 copies) -----------------
const float* viewportClearF() {
    static const float v[4] = {themeTokens().viewportClear.r, themeTokens().viewportClear.g,
                               themeTokens().viewportClear.b, themeTokens().viewportClear.a};
    return v;
}
const ImVec4& viewportClear() {
    static const ImVec4 v = toVec4(themeTokens().viewportClear);
    return v;
}

// --- heatmap ramp (data-viz only) -------------------------------------------
const ImU32* heatmapU32() {
    static const ThemeTokens& t = themeTokens();
    static const ImU32 v[5] = {toCol32(t.heatmap[0]), toCol32(t.heatmap[1]),
                               toCol32(t.heatmap[2]), toCol32(t.heatmap[3]),
                               toCol32(t.heatmap[4])};
    return v;
}

// --- full dark style (moved verbatim from main.cpp applyDarkTheme) ----------
void applyTheme(ThemeVariant v) {
    ImGuiStyle& style = ImGui::GetStyle();
    const StyleMetrics& m = styleMetrics();
    style.WindowRounding = m.windowRounding;
    style.ChildRounding = m.childRounding;
    style.FrameRounding = m.frameRounding;
    style.GrabRounding = m.grabRounding;
    style.PopupRounding = m.popupRounding;
    style.ScrollbarRounding = m.scrollbarRounding;
    style.TabRounding = m.tabRounding;
    style.WindowBorderSize = m.windowBorderSize;
    style.FrameBorderSize = m.frameBorderSize;
    style.WindowPadding = ImVec2(m.windowPaddingX, m.windowPaddingY);
    style.FramePadding = ImVec2(m.framePaddingX, m.framePaddingY);
    style.ItemSpacing = ImVec2(m.itemSpacingX, m.itemSpacingY);
    style.ItemInnerSpacing = ImVec2(m.itemInnerSpacingX, m.itemInnerSpacingY);
    style.IndentSpacing = m.indentSpacing;
    style.ScrollbarSize = m.scrollbarSize;
    style.GrabMinSize = m.grabMinSize;
    style.TabBorderSize = m.tabBorderSize;
    // Professional editor style override (per UI spec)
    style.WindowPadding = ImVec2(10, 10);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 10.0f;
    // one-off chrome (not tokenized — alignment lives in style only):
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    const ThemeTokens& t = themeTokensFor(v);
    ImVec4* c = style.Colors;
    // Backgrounds (Window/Title/Scrollbar share surface1; TitleCollapsed =
    // surface0 — the values were always identical, now single-sourced).
    c[ImGuiCol_WindowBg] = toVec4(t.surface1);
    c[ImGuiCol_ChildBg] = toVec4(t.surface2);
    c[ImGuiCol_PopupBg] = toVec4(t.surface3);
    c[ImGuiCol_DockingEmptyBg] = toVec4(t.surface0);
    // Borders
    c[ImGuiCol_Border] = toVec4(t.border);
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);  // one-off
    // Frames/Inputs
    c[ImGuiCol_FrameBg] = toVec4(t.surfaceInput);
    c[ImGuiCol_FrameBgHovered] = toVec4(t.frameHover);
    c[ImGuiCol_FrameBgActive] = toVec4(t.frameActive);
    // Titles/Menus
    c[ImGuiCol_TitleBg] = toVec4(t.surface1);
    c[ImGuiCol_TitleBgActive] = toVec4(t.titleActive);
    c[ImGuiCol_TitleBgCollapsed] = toVec4(t.surface0);
    c[ImGuiCol_MenuBarBg] = toVec4(t.surface2);
    // Text
    c[ImGuiCol_Text] = toVec4(t.textPrimary);
    c[ImGuiCol_TextDisabled] = toVec4(t.textDisabled);
    c[ImGuiCol_TextSelectedBg] = toVec4(t.accentSoft);
    // Headers/Selection
    c[ImGuiCol_Header] = toVec4(t.accent);
    c[ImGuiCol_HeaderHovered] = toVec4(t.accentHover);
    c[ImGuiCol_HeaderActive] = toVec4(t.accentActive);
    // Buttons
    c[ImGuiCol_Button] = toVec4(t.accent);
    c[ImGuiCol_ButtonHovered] = toVec4(t.accentHover);
    c[ImGuiCol_ButtonActive] = toVec4(t.accentActive);
    c[ImGuiCol_CheckMark] = toVec4(t.accentBright);
    c[ImGuiCol_SliderGrab] = toVec4(t.accentBright);
    c[ImGuiCol_SliderGrabActive] = toVec4(t.sliderGrabActive);
    // Separators
    c[ImGuiCol_Separator] = toVec4(t.border);
    c[ImGuiCol_SeparatorHovered] = toVec4(t.separatorHover);
    c[ImGuiCol_SeparatorActive] = toVec4(t.separatorActive);
    // Tabs
    c[ImGuiCol_Tab] = toVec4(t.tab);
    c[ImGuiCol_TabHovered] = toVec4(t.tabHover);
    c[ImGuiCol_TabActive] = toVec4(t.accent);
    c[ImGuiCol_TabUnfocused] = toVec4(t.surface2);
    c[ImGuiCol_TabUnfocusedActive] = toVec4(t.tabUnfocusedActive);
    // Scrollbar
    c[ImGuiCol_ScrollbarBg] = toVec4(t.surface1);
    c[ImGuiCol_ScrollbarGrab] = toVec4(t.scrollbarGrab);
    c[ImGuiCol_ScrollbarGrabHovered] = toVec4(t.scrollbarGrabHover);
    c[ImGuiCol_ScrollbarGrabActive] = toVec4(t.scrollbarGrabActive);
    // Resize grip
    c[ImGuiCol_ResizeGrip] = toVec4(t.resizeGrip);
    c[ImGuiCol_ResizeGripHovered] = toVec4(t.resizeGripHover);
    c[ImGuiCol_ResizeGripActive] = toVec4(t.resizeGripActive);
    // Docking
    c[ImGuiCol_DockingPreview] = toVec4(t.dockingPreview);
    // Modal dimming
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);  // one-off
}

}  // namespace m2rig::theme
