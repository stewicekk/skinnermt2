#pragma once
// Vector UI icons drawn via the ImGui draw list (Wave 44).
//
// Why draw-list and not an icon font / emoji: the default ImGui font atlas only
// spans U+0020..U+00FF, so emoji and Dingbat glyphs (📁 ⚙ ✓ ⚠ ▶ …) render as
// missing-glyph boxes. A hand-built 256x256 texture would be blurry and add a
// binary asset. Vector icons are self-contained, DPI-crisp at any UI scale and
// theme-tinted for free via ImGuiCol_Text.
//
// The icon identity -> label/tooltip table is core (m2rig/ui_model.hpp, pinned
// by tests); this exe-only module owns the glyph geometry + the two ImGui
// surfaces (button / tab item) that embed the glyphs.

#include "m2rig/ui_model.hpp"

#include <imgui.h>

namespace m2rig {

// Draw `icon` centered at `center`, fitting a `size` px box, stroked/filled in
// `color`. Geometry lives on a 16x16 grid (y down) scaled to `size`; the stroke
// is ~1.6 grid units so it stays crisp from 10px to 28px.
void drawIcon(ImDrawList* dl, UiIcon icon, ImVec2 center, float size, ImU32 color);

// Icon + label button with full ImGui button semantics (background, hover,
// press, disabled dimming). The button itself is a real ImGui::Button over a
// run of spaces wide enough to clear icon+label, so the hit area, focus and
// disabled states are stock ImGui; the glyph + text are then drawn on top.
// label == nullptr -> iconLabel(icon); tooltip comes from iconTooltip(icon).
// Returns true when pressed.
bool iconButton(UiIcon icon, const char* label = nullptr, bool disabled = false);

// Icon + label tab item. Same space-run trick: BeginTabItem receives enough
// leading spaces to clear the glyph, so the tab keeps its real width, click
// area and close button while the glyph + text are drawn at the label slot.
// Returns true when the tab is open (draw the body).
bool iconTabItem(UiIcon icon, const char* label = nullptr, bool* p_open = nullptr,
                 ImGuiTabItemFlags flags = 0);

}  // namespace m2rig
