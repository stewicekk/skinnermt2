---
name: viewport-ux
description: Blender-grade viewport overlays, input router, camera and paint UX (Wave 30)
---

# viewport-ux

Turns the working-but-basic viewport into a professional authoring
surface. Covers panel split, overlays, picking math, camera flights,
paint station, weight table, multi-select ops, gizmo completion,
command access and the P0 dialog bug.

## Status: Wave-30a UI slice landed (Wave 26 wave)

- Toolbar "Auto-rig" (same undoable op as Weights panel, red severity
  family, BeginDisabled without asset); Flood/Prune red + tooltips.
- Viewport "Shading" popover (`viewport_shading_pop`: 7 mode radios +
  6 toggles + Paint-with-bone-guard; single state, no fork) + input
  guard: orbit/pan/paint/zoom/click-select suppressed while open
  (reset/completion paths untouched).
- Teal-cyan theme replacing the blue accent (all families, alphas kept).

## Current truth

- `panels.cpp` 3067 lines, one TU; viewport fn ~404 lines
  (`drawViewportPanel:2062-2466`); picking/box/label math in anonymous
  namespace (untestable); facade 7 fns (`panels.hpp:17-32`); Wave-23
  split naming-only; `drawSceneContents` vs `renderScene` duplicate
  `2.8f` fallback (`:2279,2712`).
- Camera Wave-23 pinned (DPI orbit/pan/zoom, fmod yaw, pole reflection,
  Top/Bottom 0.001 tilt, Front=+Z per `samples.cpp:31-33`); tests
  `camera_orbit_*`, `camera_apply_preset_view_mapping`,
  `grid_blue_axis_*`. Gaps: any input cancels smooth (`camera.hpp:225,
  236,253`); linear yaw lerp (350-degree spin); immediate ortho flip
  (`:373`); stale near/far (no `updateClip` in orbit/pan).
- Overlay-vs-orbit fixed Wave-23 (no `AllowOverlap`, `btnDown`+rect-hover
  `:2087-2109`) with residual: button-press sets `btnDown`, <6px release
  double-fires button+pick; off-window release sticks (`:2261`).
- Paint: Ctrl/Shift+drag dab (`:2125`) vs Ctrl+click tree multi-select
  (`:143-153`) — same modifier, two meanings; per-dab undo; ring-only
  feedback (`:2447-2461`); no curve/pressure/spacing/fill.
- Weight editor = single-vertex slider (`:1446-1497`), jump-to-0
  (`:1448-1450`); multi-select data real (`app.hpp:63-80`) but gizmo/
  paint/flood single-primary; `boxSelectCandidates` dead (`:2115`).
- Gizmo Parent approximate (per-op branches `:270-297`, tooltips);
  Wave-22 follow-up not started; display knobs dead (no ImGuizmo style
  call); single-bone only.
- Overlays: buttons row + 2 text lines (`:2377-2421`); 0x view-cube,
  shading popover, grid editor, stats, profiler, pie menu.
- **P0 bug:** `SHBrowseForFolderA` ANSI (`file_dialog.cpp:121`) breaks
  diacritics paths; open/save blocking modal; no multi-select.
- Themes dark-only (`main.cpp:59-137`); 0x i18n (CZ base, EN-only UI);
  DPI fonts blur (no PerMonitorV2); a11y = `NavEnableKeyboard` only.

## Target contract

- Split: `viewport/` (panel/overlays/picking) + `gizmo/` + `panels/`
  per-panel + `shell/` (menubar/dock/shortcuts); pure picking math to
  `m2rig_core` with unit tests; `panels.hpp` stays facade.
- Input router `Gizmo > Overlay > Box/Paint > Orbit/Pan` + overlay-rect
  hit-test first + capture on drag start.
- Camera: shortest-arc yaw, damped flights, `clipAuto` vs manual near/far,
  preset bookmarks; overlay exclusion in pick promotion.
- Paint station: stroke object + coalesced undo, falloff editor, `[/]`
  size, `Shift`-smooth/`Ctrl`-subtract, live affected highlight, symmetry
  gizmo + topology mirror.
- Weight table v1 (virtualized, filter/sort/bulk/CSV); median-pivot bulk
  gizmo; unified `decomposeParentDelta` + axis-lock + real ImGuizmo styles.
- Header `[Shading▾][Overlays▾][View▾]`, view-cube, grid/snap popover,
  frame-graph from `fps+drawCalls`; `Q`-pie + `Ctrl+K` palette + keymap
  editor in `user_prefs.json`.
- Dialogs: `SHBrowseForFolderW`, multi-select open, non-blocking pick
  where possible, thumbnail preview.

## Entry points

- `drawViewportPanel/drawSceneContents/renderScene/updateBoneGizmo/
  pickMeshPoint/pickBoneAt/drawSkeletonTree`, `ArcballCamera::*`,
  `openFileDialog/saveFileDialog/pickFolder`.

## Test gate

- `box_project_roundtrip`, `pick_bone_root_clickable`,
  `yaw_shortest_arc`, `overlay_router_priority`,
  `aspect_uses_logical` (2x DPI).

## Failure handling

Wave-23 fixes are load-bearing (DPI scale, tilt guards, Front=+Z,
logical aspect) — every viewport PR re-runs their tests. Solution-judge
adjudicates look-vs-truth conflicts (evidence wins, cf. Front/Back revert).
