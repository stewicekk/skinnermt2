---
name: theme-module
description: exe-local theme module (src/app/theme.cpp) mapping core float tokens to ImVec4, viewport clear + heatmap single source, applyDarkTheme moved out of main.cpp
---

# theme-module

Wave 35 exe-local mapping layer from the pure-float tables in core
(`m2rig::themeTokens()` / `m2rig::styleMetrics()`, `include/m2rig/ui_model.hpp`)
to ImGui-facing values: `src/app/theme.hpp` + `src/app/theme.cpp`,
namespace `m2rig::theme`. Extends `ui-model` (it owns the VALUES; this module
owns only the mapping) and `layout-system` (`applyDarkTheme` is the token
consumer it describes).

## Contract

- Call chain is one-way and single-sourced:
  `ui_model` floats (core, tested) -> `theme.cpp` (Rgba -> ImVec4/ImU32) ->
  panels / viewport / renderer. Never write a token value directly in a
  panel.
- `src/app/theme.cpp` is exe-only (it includes `<imgui.h>`); anything
  compiled into BOTH the exe and `m2rig_tests` — notably
  `src/renderer.cpp` (CMakeLists adds it to `Metin2RiggingStudio` and to
  `m2rig_tests`) — may include ONLY core headers (`m2rig/*.hpp`), never
  `theme.hpp` / `panels_internal.hpp`.
- Roles stay separate: `err()` (error TEXT) and `danger()` (destructive
  BUTTON fill) must not be merged.

## Current truth

- Semantic feedback: `ok()`, `warn()`, `err()`, `info()`; `statusColor(kind)`
  maps a wire string through `statusKindFrom()` — same vocabulary as the
  status bar, unknown kinds degrade to Info (never transparent/black).
- Destructive family: `danger()`, `dangerHover()`, `dangerActive()` — the
  one red family used by `dangerButtonPush()` (`component-kit`).
- Viewport clear: `viewportClearF()` returns `{r,g,b,a}` floats and
  `viewportClear()` the ImVec4 — THE single source that replaced the 4+
  hardcoded copies across renderer/panels.
- Heatmap ramp: `heatmapU32()` returns 5 packed `ImU32` stops (blue -> red),
  data-viz only — not a semantic UI color.
- `applyDarkTheme()` was moved verbatim out of `src/app/main.cpp` (which now
  only includes `theme.hpp` and calls `m2rig::theme::applyDarkTheme()`):
  metrics come from `styleMetrics()`, token-covered colors come from
  `themeTokens()`, and every remaining literal is marked `// one-off` in the
  source (e.g. `FrameBgHovered`, `TitleBgActive`, `SeparatorHovered`,
  `Tab*`, `ScrollbarGrab*`, `ResizeGrip*`).
- `src/app/panels_internal.hpp` keeps the historical `Theme::` name as a
  thin forwarder (`Theme::kOk/kWarn/kErr/kInfo/kDanger*`,
  `Theme::statusColor`) over this module so existing call sites stay
  untouched — it must not hold values of its own.

## Entry points

- API: `src/app/theme.hpp` (`ok/warn/err/info/statusColor`,
  `danger/dangerHover/dangerActive`, `viewportClearF/viewportClear`,
  `heatmapU32`, `applyDarkTheme`).
- Implementation: `src/app/theme.cpp`; startup call in `src/app/main.cpp`.
- Value tables + tests: `include/m2rig/ui_model.hpp`, `src/ui_model.cpp`,
  `tests/test_ui_model.cpp`.
- Overlay U32 draw-list colors (selection box, labels, brush ring) are a
  separate family in `src/app/panels_viewport.cpp` and stay literal by
  design — do not tokenize them here.

## Test gate

- `tests/test_ui_model.cpp`: `theme_tokens_accent_and_semantic_families_pinned`,
  `theme_surfaces_and_viewport_clear_pinned`,
  `style_metrics_match_apply_dark_theme` (pin the float source that
  `applyDarkTheme` reads; metrics are asserted through `styleMetrics()`).
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Regression rule: a token edit that is not visible in the golden is
  incomplete; a color change made directly in a panel instead of
  `theme.cpp`/`ui_model` is a defect.
