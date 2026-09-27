---
name: toolbar-ux
description: Six-group toolbar Stage|Rig|View|Display|Status|Panels with ui_model-measured wrap, preset segmented row and disabled honesty
---

# toolbar-ux

`drawToolbar(App&)` in `src/app/panels_toolbar.cpp`: a grouped, wrapping
toolbar over the EXISTING `App`/`App::UISettings` bools (no forked state).
Extends `layout-system` (toolbar wrap + tooltip coverage) and `ui-model`
(group widths, preset matrix, view-mode labels).

## Contract

- Group widths and the wrap decision are NOT re-measured ad hoc:
  `toolbarGroupWidth(group)` + `toolbarShouldWrap` in `ui_model` are the
  single, tested source; the toolbar only compares cursor position against
  them.
- Disabled honesty: a control that cannot act is wrapped in
  `BeginDisabled(...)` AND still explains itself — every hover tooltip uses
  `tipFor(...)` (or `IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)`) so
  disabled items keep their reason (`component-kit`).
- Every button toggles an existing `App` bool or calls an existing `App`
  verb; no toolbar-local state except the transient popup.

## Current truth

- Six groups, render order **Stage | Rig | View | Display | Status |
  Panels** (`kToolbarGroupCount = 6`, Wave 36b). The local `beginGroup`
  lambda: first group renders inline; later groups `SameLine` +
  `SeparatorEx(Vertical)` when they fit, otherwise `NewLine()` when
  `cursorX + groupWidth > toolbarWidth` — never clipped.
- **Stage** = 4 segmented buttons (file-local `segmentedButton`) from the
  ui_model matrix: label `layoutPresetName`, active state
  `layoutPresetMatches(app.uiSettings, p)`, tooltip `layoutPresetTip`, action
  `applyLayoutPreset` + `app.setStatus("Layout preset applied: " + name,
  "success")`. The Settings panel (`drawSettingsPanel` in
  `src/app/panels_display.cpp`) uses the SAME wording and the SAME
  ui_model calls — that duplication of wording is deliberate, the data is
  not duplicated.
- **Rig** = `Auto-rig`, `BeginDisabled` without an asset, wrapped in
  `dangerButtonPush()/dangerButtonPop()` (the one red family), tooltip via
  `AllowWhenDisabled`; one call site into `App::autoRigFromSkeleton()`.
- **View** = `drawViewModeSegmented` (7 modes, `viewModeShortLabel`,
  digit shortcuts), `Frame (F)` (`frameWholeModel`, disabled without asset),
  `Ortho -> Persp` / `Persp -> Ortho` toggle.
- **Display** = `labelXray()` checkbox (never a raw "X-ray" string), `Tex`,
  `PBR`, `Paint` (warns when no bone selected), `Deform`, `DQS`, `Grid`,
  `Bones`, `Wire ovl` — each with a named shortcut/precondition tooltip.
- **Status** = `Undo`/`Redo` gated on `canUndo()/canRedo()` with
  `AllowWhenDisabled` tips, `labelValidate()` button -> `app.runValidation()`,
  `FPS %.0f` readout, plus `Running <label> ...` while `bridgeBusy`.
- **Panels** = `">>"` overflow popup toggling the same `App::UISettings`
  bools (`showBonePanel`, `showWeightsPanel`, `showMaterialsPanel`,
  `showExportPanel`, `showProjectPanel`, `showBoneDisplayPanel`,
  `showGizmoPanel`) — a subset by design, no second source of truth.

## Entry points

- `src/app/panels_toolbar.cpp`: `drawToolbar`, `segmentedButton`,
  `drawViewModeSegmented`.
- `include/m2rig/ui_model.hpp`: `kToolbarGroupCount`, `toolbarGroupName`,
  `toolbarGroupWidth`, `toolbarShouldWrap`, `layoutPreset*`,
  `viewModeShortLabel`, `labelXray`, `labelValidate`, `labelFrameTip`.
- `src/app/panels_internal.hpp`: `tipFor`, `dangerButtonPush/Pop`,
  `frameWholeModel`.
- `src/app/panels.cpp`: toolbar window host + menu equivalents.

## Test gate

- `tests/test_ui_model.cpp` `ui_model.toolbar_groups_and_wrap_rule` pins
  group count/order, positive widths and the wrap truth table (exact fit
  does not wrap, 1 px over wraps); `ui_model.layout_presets_pin_current_matrix`,
  `ui_model.layout_preset_matches_after_apply`,
  `ui_model.view_mode_names_match_enum_order`.
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Manual gate: narrow the app window until the toolbar wraps — groups must
  move to a new line whole, and every disabled control must still show its
  tooltip.
