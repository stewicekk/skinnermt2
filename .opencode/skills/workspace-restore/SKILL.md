---
name: workspace-restore
description: Project persistence, autosave, layouts, sparse undo and selection sets (Wave 30)
---

# workspace-restore

Owns everything that survives a restart or an undo keypress: `.m2rig`
projects, autosave/recovery, workspace layouts, undo stacks, locks,
hidden sets and selection sets — plus the localization of persisted data.

## Current truth

- `saveWorkspace/loadWorkspace` + `assets[]` reimport both directions
  (Wave-18); brush/camera/ortho/fov persist; unresolvable workspaces fail
  WITHOUT touching the live session (regression-tested).
- Undo = `InfluenceSnapshot` full influence array + full pose per entry
  (`app.hpp:441-449`, `app_state.cpp:682-698`); cap 50 with `erase(begin)`;
  `restoreSnapshot` moves data out (`:700-747`); every dab/drag/push one
  entry (`:761,822,900,943,988,1012,1037,1063,1085,1102`); per-stroke
  coalescing deferred (Wave-18); bake outside pose-undo (`app.hpp:308-310`).
- Locks/hidden/sets persisted by bone NAME, tolerant parse
  (`project_file.cpp:475,542-573`); `selectedBones` + hierarchy select +
  name-keyed sets (`app.hpp:63-80`, `app_state.cpp:310-373`).
- Autosave `.tmp` + atomic rename, dirty-gated + `session.lock` recovery
  modal; interval slider + Save-now + last-backup readout.
- 14 panel flags in `user_prefs.json` (`ui`) vs docking in `imgui.ini`
  (`main.cpp:179-180`) — two sources; reset needs restart
  (`panels.cpp:2532,2829`); `dockBuilt` static; hard-coded splits.
- 0x i18n: persisted keys are EN literals; no migration story.

## Target contract

- Single `workspace_layout.json` (ImGui ini + panel flags + camera) +
  named `Rig/Paint/Anim/Review` switcher + per-`.m2rig` layout + live
  reset (no restart); `dockBuilt` resettable.
- Sparse stroke-coalesced undo: `{vertexId, before[], after[]}` touched
  verts only + pose channel masks + time/idle group commit; separate
  weight vs pose stacks; optional LZ4 on idle; bake stays explicitly
  outside pose-undo (honest status preserved).
- Multi-bone ops over `selectedBones`/sets: flood/prune/isolate/lock/
  hide/mirror/transfer-remap + hierarchy/invert/grow; active-vs-selected
  distinction; median pivot (with `viewport-ux`).
- `cs.json` localization (Czech first) + diacritics font; persisted-key
  migration table (old EN key -> new id) so workspaces survive translation.

## Entry points

- `saveWorkspace/loadWorkspace/saveCurrentWorkspace/restoreWorkspace/
  tickAutosave`, `pushUndoSnapshot/App::undo/redo/canUndo/canRedo/
  takeSnapshot/restoreSnapshot`, `selectBone*/boneSelectionSets/
  lockedBones/hiddenSubmeshes`, `project_file.*`.

## Test gate

- `workspace_restore_reimport` both directions (exists),
  `undo_coalesced_stroke` (N dabs = 1 entry),
  `sets_survive_reload_apply`, `layout_live_reset`.

## Failure handling

Restore never touches the live session until the replacement is fully
validated (existing contract). Undo entries stay lock-aware
(flood/prune on locked bones FAIL, never fake-ok). `.m2rig` format
changes are versioned + tolerant-parse (unknown fields ignored, never
fatal).
