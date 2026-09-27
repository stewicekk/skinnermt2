---
name: panel-modules
description: Wave 34 panels split contract - panels.cpp menu/dock orchestration + 8 panels_*.cpp bodies + panels_internal.hpp cross-TU declarations
---

# panel-modules

The mechanical split of the old monolith `src/app/panels.cpp` into one
orchestration TU plus 8 panel TUs, joined by the contract header
`src/app/panels_internal.hpp` (CMakeLists adds all 8 next to `panels.cpp`
under the exe target). Extends `ui-model` (shared tables, no local copies)
and `layout-system` (menu/dock orchestration it hosts).

## Contract

- EVERY symbol that crosses a `panels_*.cpp` TU boundary is declared in
  `panels_internal.hpp`; `panels.hpp` stays the public app-facing surface
  (`src/app/main.cpp`, `src/app/app_gpu.cpp` see it, never this header).
- State touched by exactly ONE TU stays file-local (static / anonymous
  namespace); anything reachable from two TUs is `extern` in the contract
  header. Shared state today: `g_mainWindow`, `g_dockBuilt`,
  `g_mse`/`g_msePath`/`g_mseEmitterCount`/`g_msePreview`/`g_msePlaying`/
  `g_mseTime`/`g_mseDuration`.
- `panels_internal.hpp` includes `m2rig/ui_model.hpp` — local copies of the
  preset/label/descriptor tables are FORBIDDEN. `panels_display.cpp` used
  to hold a stale 4x14 fork; it was removed in Wave 36 and now calls
  `layoutPresetName`/`applyLayoutPreset`/`layoutPresetTip` from core.
- ImGui types ARE allowed in this exe-only header (it defines
  `IMGUI_DEFINE_MATH_OPERATORS` before `<imgui.h>` once for all TUs);
  `include/m2rig/*.hpp` stays ImGui-free.
- `Theme::` names in the header are forwarders to `src/app/theme.cpp` —
  never re-state a color there.

## Current truth

- `src/app/panels.cpp` (orchestration + shared helpers): `g_dockBuilt`,
  `requestDockRebuild()`, `markDockBuilt()`, `setMainWindowHandle()`,
  `buildDefaultDockLayout(ImGuiID)`, `renderScene()`, `drawAllPanels()`
  (menu bar, View/File menus, host mirror, DockSpace, status/toast calls),
  `boneSegments()`, `drawSkeletonTree()`, `updateBoneGizmo()`, plus the
  shared `frameWholeModel()` helper in the header.
- The 8 panel TUs (what each owns):
  - `panels_actions.cpp` — `doImportSmd/doImportBridged/doImportGltf`,
    `doExportSmd/doExportMsm/doExportFbx/doExportAni/doExportLod`,
    `doSaveWorkspace/doLoadWorkspace/doOpenMsm`, `drawCommandPalette()`.
  - `panels_msm.cpp` — `drawMsmInspector`, `drawMseEffects`, `doOpenMse`,
    `mseEffectDuration`, MSE preview state.
  - `panels_viewport.cpp` — `drawSceneContents`, picking
    (`pickMeshPoint`, `pickBoneAt`), textured/PBR submesh range draws.
  - `panels_toolbar.cpp` — `drawToolbar`, `drawStatusBar`, `drawToasts`,
    local `segmentedButton`/`drawViewModeSegmented` (file-local, correct).
  - `panels_side.cpp` — `drawAssetsPanel`, `drawScenePanel`,
    `drawSkeletonPanel`.
  - `panels_props.cpp` — `drawBoneProperties`, `drawWeightPanel` (+
    file-local weight-table/CSV helpers).
  - `panels_workflow.cpp` — `drawMaterialPanel`, `drawProjectPanel`,
    `drawExportPanel`, `drawValidationPanel`, `drawSystemPanel`,
    `drawConsolePanel`, `drawTimelinePanel` (+ file-local row/flow helpers).
  - `panels_display.cpp` — `drawSettingsPanel` (preset row + Reset button),
    `drawBoneDisplayPanel`, `drawGizmoPanel`, `drawViewportSettingsPanel`.
- Helper discipline in the header: `frameWholeModel` (single Frame(F)
  verb), `tipFor` (disabled-aware tooltips, `component-kit`), the
  `Theme::` forwarders, `dangerButtonPush/Pop`, and the `do*`/`draw*`
  declarations grouped by owning TU.

## Entry points

- Contract header: `src/app/panels_internal.hpp`.
- Bodies: `src/app/panels.cpp` + `src/app/panels_{actions,msm,viewport,
  toolbar,side,props,workflow,display}.cpp`.
- Public surface: `include/m2rig/panels.hpp`; shared tables:
  `include/m2rig/ui_model.hpp`.
- Neighbours: `theme-module` (colors), `dock-layout` (builder + host
  mirror), `toolbar-ux` / `status-chrome` (toolbar/status/toast bodies).

## Test gate

- Panels TUs are exe-only (CMakeLists puts them on `Metin2RiggingStudio`,
  not on `m2rig_tests`), so their shared data is pinned through core:
  suite `ui_model` in `tests/test_ui_model.cpp` (presets, panel
  descriptors, labels, dock plan, toolbar groups).
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at
  the time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Split rule for reviewers: moving code between these TUs must not change
  behavior — a move PR carries no feature edits, and any cross-TU symbol it
  introduces appears in `panels_internal.hpp` in the same change.
