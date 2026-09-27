---
name: prefs-persistence
description: user_prefs.json discipline - every UISettings bool needs a save line AND an extractBool line, with a round-trip test pattern
---

# prefs-persistence

`user_prefs.json` persistence for app/UI preferences, written and read by
`App::savePreferences` / `App::loadPreferences` in `src/app_state.cpp`
(m2rig_core, ImGui-free — so `m2rig_tests` can exercise it headlessly).
Extends `workspace-restore` (owns the `user_prefs.json` vs `imgui.ini`
split) and `app-state` (the `App` methods themselves).

## Contract

- EVERY new `App::UISettings` bool gets BOTH halves in the same change:
  1. a manual save line in `App::savePreferences` (JSON written by hand,
     note the comma placement — the last entry has none), and
  2. an `extractBool("<key>", uiSettings.<field>)` line in
     `App::loadPreferences`.
  One half without the other silently loses the flag.
- Fails OPEN: a prefs file that predates a key leaves the default intact
  (the `extractBool` lambda only writes when the key is present). Loading
  must never reset flags it does not know about, and a missing/corrupt file
  must not crash the app.
- Persistence is for preferences only: the dock TREE lives in `imgui.ini`
  (see `dock-layout`), workspace/asset data lives in the project file
  (see `workspace-restore`).

## Current truth

- `App::savePreferences(configDir)` writes `configDir / "user_prefs.json"`
  as hand-built JSON: camera (`orthographic`), view/overlay bools
  (`showGrid`, `showBones`, `xrayBones`, `showWireOverlay`, `textured`,
  `previewDeform`, `useDqs`), bone display (`boneShowLabels`,
  `boneShowJointCrosses`), gizmo (`gizmoSnap`, `gizmoShowAxisLabels`,
  `gizmoShowPlaneHandles`, `gizmoShowCenterHandle`), symmetry
  (`symmetryEnabled`), UI (`compactMode`, `panelSpacing`,
  `panelRounding`), the 14 `show*Panel` flags, and `mseTabEnabled`.
- `App::loadPreferences(configDir)` reads the same file through local
  `extractBool` / `extractFloat` lambdas over a parsed key->value map.
- `mseTabEnabled` (added Wave 37) is the reference implementation of the
  rule: it moved out of a panels-local global, got a save line, an
  `extractBool` line, and a round-trip test — copy that shape for the next
  flag.
- Save/load are called around app startup/shutdown from the exe side
  (`src/app/main.cpp`), with the same `configDir` argument on both sides.

## Entry points

- `src/app_state.cpp`: `App::savePreferences`, `App::loadPreferences`
  (local `extractBool`/`extractFloat`).
- `include/m2rig/app.hpp`: `App::UISettings` (the flag struct) and the
  `App` declaration.
- Reference test: `tests/test_app.cpp` `app.prefs_round_trip_mse_tab_enabled`.
- Neighbours: `app-state` (rest of the App core), `workspace-restore`
  (`imgui.ini`/autosave/project file), `ui-model` (`panelDescriptors`
  `prefsKey` column — the canonical key names for panels).

## Test gate

- Pattern to copy (from `tests/test_app.cpp`): temp directory under
  `std::filesystem::temp_directory_path()`, flip the flag on App A ->
  `savePreferences(dir)` succeeds -> fresh App B defaults are checked ->
  `loadPreferences(dir)` -> assert the flipped value; third block proves
  the default round-trips too; `remove_all` at the end.
- Existing gates: `app.prefs_round_trip_mse_tab_enabled`,
  `app.resolve_draw_path_routing_matrix` (both in `tests/test_app.cpp`).
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Definition of done for a new pref: save line + `extractBool` line +
  round-trip test + an old file (written before the key existed) still
  loads with the default.
