---
name: dock-layout
description: Default dock builder split order, g_dockBuilt live rebuild + both Reset paths, imgui.ini migration note, Wave 37 host mirror reserve
---

# dock-layout

Dock STRUCTURE for the app: `buildDefaultDockLayout(ImGuiID)` in
`src/app/panels.cpp`, the `g_dockBuilt` rebuild flag, both live Reset paths
and the Wave 37 custom host window that reserves room for the menu bar and
status bar. Extends `layout-system` (dock presets/rules) and
`workspace-restore` (`imgui.ini` persistence vs live rebuild).

## Contract

- Structure and visibility are separate: panel FLAGS are live-read every
  frame (presets apply instantly); the node TREE rebuilds only through
  `buildDefaultDockLayout`, driven by `g_dockBuilt` +
  `requestDockRebuild()` / `markDockBuilt()`.
- `PassthruCentralNode` may appear ONLY on the `DockSpace` call inside
  `drawAllPanels` — never on the DockBuilder node (the builder comment
  explains: `ImGuiDockNodeFlags_DockSpace` is an internal enum value,
  OR-ing the public flag is a C5054 type mismatch).
- Viewport transparency stays load-bearing: the docked Viewport window uses
  `ImGuiWindowFlags_NoBackground`, and the host window derives
  `ImGuiWindowFlags_NoBackground` from the same `PassthruCentralNode` flag.
- Ratios and zone membership are data in `ui_model` (`dockRatios()`,
  `dockZoneWindows()`) and must stay verbatim-identical to this builder.

## Current truth

- Split order inside `buildDefaultDockLayout` (Wave 36 changed it):
  1. `DockBuilderRemoveNode` + `DockBuilderAddNode(..., DockSpace)` +
     `SetNodeSize`.
  2. Up **0.075** -> top strip (Toolbar).
  3. Down **0.10** -> full-width **Timeline** strip, split BEFORE the
     left/right columns so it spans the window.
  4. Left **0.17** -> Assets / Scene / Skeleton.
  5. Right **0.35** -> right column, then split Right **0.4** so Props
     keeps 0.6 (Bone, Weights, Materials, Bone Display, Gizmo,
     Viewport Settings) and Workflow gets 0.4 (Export, Project, Settings).
  6. Down **0.24** -> center-bottom row, then split Right **0.4** so
     Output keeps 0.6 (Validation, Console) and Tools gets 0.4; Viewport
     stays in the center main node; `DockBuilderFinish` closes it.
- First run: `g_dockBuilt` starts false, `drawAllPanels` calls
  `buildDefaultDockLayout(dockspaceId)` then `markDockBuilt()` at the
  DockSpace site.
- Both Reset paths rebuild LIVE (delete `imgui.ini` + `requestDockRebuild()`
  + `buildDefaultDockLayout(resetId)` + `markDockBuilt()` + success status,
  no restart): the Settings panel button "Reset Viewport Layout"
  (`drawSettingsPanel` in `src/app/panels_display.cpp`) and the View menu
  "Reset layout" (`drawAllPanels` in `src/app/panels.cpp`).
- imgui.ini migration: an EXISTING `imgui.ini` keeps its old tree — the new
  structure lands only on first run or via a Reset. The migration note
  lives in the builder comment; do not "helpfully" force-migrate.
- Wave 37 host mirror in `drawAllPanels`: a custom host window mirrors
  pinned ImGui's `DockSpaceOverViewport` but with an explicit reserve =
  menu-bar height (captured from `BeginMainMenuBar`) **+ `kStatusBarHeight`**
  (`vp->WorkSize.y - menuBarHeight - kStatusBarHeight`), so neither chrome
  bar overlays the first/last rows of docked panels.

## Entry points

- `src/app/panels.cpp`: `buildDefaultDockLayout`, `requestDockRebuild`,
  `markDockBuilt`, `g_dockBuilt`, the host mirror + DockSpace inside
  `drawAllPanels`, the View-menu Reset.
- `src/app/panels_display.cpp`: `drawSettingsPanel` Reset button.
- `include/m2rig/ui_model.hpp`: `dockRatios()`, `DockZone`,
  `dockZoneWindows()`, `kStatusBarHeight` (reserve input).
- `layout-system` owns preset semantics; `workspace-restore` owns
  `imgui.ini` lifecycle.

## Test gate

- `tests/test_ui_model.cpp` `ui_model.dock_plan_ratios_and_zone_membership`
  pins every ratio and the window membership of all 8 zones (the mirror of
  this builder).
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Manual gate for structural edits: fresh `imgui.ini` first run, then BOTH
  Reset paths, then restart-with-existing-ini (old tree must survive);
  viewport must still composite (no opaque backdrop) in all three.
