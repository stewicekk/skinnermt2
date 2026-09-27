---
name: app-state
description: ImGui-free App core in src/app_state.cpp - status/toast queues, undo/redo, bone selection sets, sample/import/export verbs, resolveDrawPath, preferences
---

# app-state

`App` itself: `src/app_state.cpp` (m2rig_core, compiled into the exe AND
`m2rig_tests`, ImGui-free so everything here is headlessly testable).
Extends `workspace-restore` (undo/selection sets/autosave/preferences) and
`ui-model` (the status vocabulary `statusKindFrom` supplies).

## Contract

- No ImGui, no exe-only headers in this TU — it may include only
  `m2rig/*.hpp`. Anything UI-shaped (panels, chrome) belongs in
  `src/app/panels_*.cpp`.
- Every state mutation that can fail returns `ResultVoid`/`Result<T>` and
  never reports success it did not achieve; verbs surface their failure
  through `setStatus(..., "error")` at the call site.
- Toast/status semantics follow ui_model: `statusKindFrom` is the shared
  vocabulary, errors are sticky, the queue caps at `kToastQueueCap = 6`.

## Current truth

- Status: `App::setStatus(message, kind)` stores the latest message+kind
  (last writer wins — there is no multi-message status queue) and mirrors
  severity to the `Logger` (error/warning/info). Priority ordering for
  display is `statusKindPriority` in ui_model, consumed by the status bar.
- Toasts: `App::pushToast(message, kind, nowSeconds)` — sticky when
  `kind == "error"`, ttl 0 / 8 s / 5 s (error/warning/other), auto-id,
  drops the oldest beyond 6; `App::tickToasts(nowSeconds)` erases
  `!sticky && now >= until`; `App::dismissToast(id)` removes one.
- Undo/redo: `pushUndoSnapshot(label)`, `restoreSnapshot`, `App::undo()`,
  `App::redo()`, `App::canUndo()`/`canRedo()` (inline in
  `include/m2rig/app.hpp`), `clearUndoHistory()`, `noteWeightsChanged()`
  (marks the asset dirty and refreshes the quality metrics).
- Bone state: selection `isBoneSelected`/`selectBone(additive)`/
  `clearBoneSelection`/`selectBoneHierarchy`; hidden
  `boneVisible`/`setBoneHidden`/`clearHiddenBones`; locked
  `setBoneLocked`/`lockSocketBones`/`unlockAllBones`; named sets
  `saveBoneSelectionSet(name)`/`loadBoneSelectionSet(name)`/
  `deleteBoneSelectionSet(name)` (persisted with the workspace).
- Sample + import/export verbs: `loadSampleArmor()`,
  `loadSampleArmorForProfile(profileId)`; `importSmdFile(path)` ->
  `applySmdText(text, srcPath)` -> `installConverted(mesh, skeleton,
  frames, srcPath, how)` (the single installer every converter shares:
  FBX/bridge/glTF paths funnel into it); `exportSmdFile(path)`; bridged/
  glTF variants `importBridgedFile`, `importGltfFile`,
  `applyBridgedSmdText`, plus MSM/GR2/FBX/ANI exporters.
- Draw-path routing: free function `resolveDrawPath(mode, usePbr,
  wantTextured, hasTexture, skinned)` + `drawPathName` — the pure table
  `drawSceneContents` dispatches on (debug ramps first, Wireframe second,
  solid PBR/texture combinations last; `hasTexture`/`skinned` are
  orthogonal and must never change the result).
- Preferences: `savePreferences(configDir)` / `loadPreferences(configDir)`
  + `applyPreferences()` (see `prefs-persistence`).
- Also here: `currentAsset()`, `runValidation()`, `weightQuality()`,
  `boneInfluenceCount`, `paintStroke`, `mirrorWeights`,
  `transferWeightsFrom/SelfTrained`, `floodSelectedBone`,
  `pruneSelectedBone`, `setVertexInfluenceWeight`/`removeVertexInfluence`/
  `normalizeVertexWeights`, `autoRigFromSkeleton`, timeline/keyframe verbs,
  autosave (`tickAutosave`, `saveAutosaveNow`) and the learning-database
  verbs.

## Entry points

- `src/app_state.cpp` (definitions), `include/m2rig/app.hpp`
  (`App`/`App::UISettings`/`Toast`/`ViewMode`/`DrawPath` declarations).
- Consumers: `src/app/panels*.cpp` (verbs + status), `src/app/main.cpp`
  (startup/shutdown, preferences), `tools/cli/main.cpp` (CLI verbs share
  core), `tests/test_app.cpp`.
- Neighbours: `prefs-persistence` (JSON rules), `ui-model`
  (`statusKindFrom`, `resolveDrawPath`'s enum names), `workspace-restore`
  (workspace file + autosave layout).

## Test gate

- `tests/test_app.cpp`:
  - `app.resolve_draw_path_routing_matrix` — full 7 modes x pbr x
    textured x hasTexture x skinned matrix against an independent
    re-derivation, plus `drawPathName` non-null for every enumerator;
  - `app.prefs_round_trip_mse_tab_enabled` — the reference
    flip -> save -> fresh App -> load pattern.
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- New core behavior lands a case in `tests/test_app.cpp` (or the matching
  core suite); exe-only panels code cannot be covered there, so state
  changes must live in this TU to be testable.
