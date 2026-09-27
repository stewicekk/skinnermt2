#pragma once
// Wave 35: exe-local theme module — THE mapping layer from the pure-float
// token tables in core (m2rig::themeTokens() / styleMetrics(),
// include/m2rig/ui_model.hpp, pinned by tests/test_ui_model.cpp) to
// ImGui-facing values (ImVec4 / ImU32 / style fields).
//
// Call chain (single source):
//   ui_model floats (core, tested) -> here (Rgba -> ImVec4) -> panels/viewport
//
// panels_internal.hpp keeps the historical `Theme::` name as a thin forwarder
// over this module so existing call sites stay untouched.
#include <imgui.h>

#include <string>

#include "m2rig/ui_model.hpp"

namespace m2rig::theme {

// Semantic feedback colors (status bar, toasts, validation/console lists).
const ImVec4& ok();
const ImVec4& warn();
const ImVec4& err();
const ImVec4& info();
// kind -> color through statusKindFrom() (same vocabulary as the status bar;
// unknown kinds degrade to Info, never transparent/black).
ImVec4 statusColor(const std::string& kind);

// Destructive button family (Auto-rig, Flood/Prune, delete…): a separate
// role from error TEXT — do not merge with err().
const ImVec4& danger();
const ImVec4& dangerHover();
const ImVec4& dangerActive();

// Viewport clear color (was 4 hardcoded copies across renderer/panels).
const float* viewportClearF();  // {r, g, b, a}
const ImVec4& viewportClear();

// Heatmap ramp (data-viz only, 5 stops blue -> red) as packed ImU32.
const ImU32* heatmapU32();

// Full ImGuiStyle (roundings/metrics/colors) for the selected theme variant.
// Token-covered roles read themeTokensFor(v); one-off chrome (BorderShadow,
// ModalWindowDimBg) stays literal and is marked below.
void applyTheme(ThemeVariant v);

}  // namespace m2rig::theme
