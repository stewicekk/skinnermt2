---
name: command-palette
description: Ctrl+K fuzzy action palette over existing toolbar/menu/preset/import-export handlers (Wave 32)
---

# command-palette

A `Ctrl+K` fuzzy action palette that routes to EXISTING handlers only —
toolbar + menu + preset views + import/export verbs. No forked logic,
no duplicate state. Extends `viewport-ux` (input router + overlay
arbitration); keyboard arbitration defers to the existing
`appOwnsKeyboard` gate.

## Current truth

- Shipped since Round E (2026-09-24): `Ctrl+K` opens the centered
  `cmd_palette` popup (`drawCommandPalette`, `src/app/panels_actions.cpp`)
  over the 30-action `kCmdPalette` table — fuzzy-substring rank,
  Enter/click runs, `Esc` closes, `Up/Down` navigate. Every row calls an
  EXISTING handler (toolbar/menu/preset/import-export verbs); no forked
  logic, no duplicate state. While open, viewport gestures defer exactly
  like `shadingOpen` (no orbit/pan/paint/zoom/click-select).
- Keyboard arbitration (`src/app/panels.cpp` `drawAllPanels`): globals
  gated by `appOwnsKeyboard = !WantTextInput && !WantCaptureKeyboard`;
  timeline duplicate gate `timelineOwnsKeyboard` (`drawTimelinePanel`);
  viewport `F` gated on `!WantTextInput`; `shadingOpen` suppresses
  paint/orbit/pan/zoom/click-select (`drawViewportPanel`). Shortcuts
  table (Help > Shortcuts) documents the global set (F, Space/Arrows,
  Ctrl+Z/Y, paint, orbit/pan/zoom, click select, G/B/X/P/D, W/E/R gizmo,
  1-7, T, box-select, additive, brush size, nudge).
- Global keys mirror menu ops one-to-one (`drawAllPanels`): F frame,
  Ctrl+Z/Y undo/redo, G/B/X/P/D/T toggles, W/E/R gizmo ops, Tab
  edit-mode toggle, 1-7 view modes.
- Project menu handlers — the palette's import/export verbs
  (`drawAllPanels` Project menu): Load sample armor `loadSampleArmor`,
  Import SMD `doImportSmd`, Import FBX/GR2 bridge `doImportBridged` gated
  on `bridgeBusy`, GR2-from-Data/Models `doImportGr2FromModels`, Import
  glTF `doImportGltf`, mesh-onto-skeleton `doImportMeshOntoSkeleton`,
  Export SMD/MSM/GR2-bridge/FBX-Noesis/ANI/GR2->FBX/LOD/batch
  `doExportSmd/doExportMsm/doExportGr2/doExportFbx/doExportAni/
  doExportGr2ToFbx/doExportLod/exportAllBatch`, Save/Load workspace
  `doSaveWorkspace/doLoadWorkspace`, Undo/Redo gated on
  `canUndo/canRedo`, Run validation `runValidation`.
- View menu handlers (`drawAllPanels` View menu): Grid/Bones/X-ray/Wire
  toggles, Textured, PBR, Paint, Deform, 7 modes with 1-7 shortcuts,
  Orthographic, Frame all F gated without asset, VSync, Panels submenu
  14 flags + MSE tab, live Reset layout.
- Single-state rule (no-fork proof): toolbar view-mode segmented
  (`drawToolbar`), Shading radios and View menu radios
  (`drawViewportPanel` / `drawAllPanels`) all write `app.viewMode` +
  `gpuDirty`; X-ray/Tex/PBR/Paint/Deform toggles likewise single bools.
  Camera presets Front/Back/Top/Bottom/Left/Right call `applyPreset`;
  Frame/Ortho call `frameAabb/frameAabbSmooth/setOrthographic`.
- Bridge boundary (honest): grnreader98 primary + Noesis fallback,
  presence surfaced in About (`drawAllPanels` About modal); GR2 native
  emit NOT supported directly. Palette labels must say `(bridge)`
  where the handler does.

## Target contract

- `Ctrl+K` opens the palette; `Esc` closes without action; typing
  filters; `Up/Down + Enter` runs; click runs; while open, viewport
  gestures defer exactly like `shadingOpen` (no orbit/pan/paint/zoom/
  click-select). `appOwnsKeyboard` stays the arbiter — the palette owns
  keys only while focused, and releases them on close.
- Fuzzy-substring rank (new pure core helper, headless-testable):
  case-insensitive ordered-subsequence match over `id + label +
  keywords`; score = contiguous-run bonus - gaps - length penalty;
  exact-prefix > substring > subsequence; ties broken by table order
  (stable, deterministic, no RNG/threads). Empty query lists the full
  table in declared order.
- Action table = toolbar + menu + preset views + import/export verbs,
  each row `{id, label, keywords, when-enabled, run}` where `run`
  calls the EXISTING handler named above (table: Frame(F), Persp/Ortho,
  X-ray, Tex, PBR, Paint, Deform, DQS, Auto-rig, Undo, Redo, Validate,
  Grid, Bones, Wire-ovl, 7 view modes, 6 camera presets + Frame all,
  VSync, 14 panel toggles, Reset layout, Load sample armor, Import
  SMD, Import FBX/GR2 (bridge), Export SMD/MSM/GR2/FBX/ANI/GR2->FBX/LOD/
  batch, Save/Load workspace, Play-pause + frame step). Disabled rows
  render greyed with the reason (no asset, `canUndo==false`,
  `bridgeBusy`) and never run — same gates as menu/toolbar.
- No forks: palette rows never reimplement ops; a routing-parity test
  pins that every palette id resolves to the same call the menu makes.
  Labels reuse tooltip strings (`Fit the whole model in view (F)`,
  bridge suffixes) so docs cannot drift.

## Entry points

- `src/app/panels.cpp` (`drawAllPanels`): Project menu, View menu,
  global shortcuts + `appOwnsKeyboard`, Shortcuts table, About modal.
- `src/app/panels_actions.cpp`: `kCmdPalette` action table +
  `drawCommandPalette` (the palette's home).
- `src/app/panels_viewport.cpp` (`drawViewportPanel`): overlay presets +
  Shading popover. `src/app/panels_toolbar.cpp` (`drawToolbar`):
  toolbar. `drawTimelinePanel`: timeline transport gate.
- `include/m2rig/app.hpp`: `undo/redo/canUndo/canRedo`, `runValidation`,
  `viewMode/textured/usePbr/paintMode/previewDeform`, `UISettings`
  flags, `setStatus`.
- `include/m2rig/camera.hpp`: `frameAabb/applyPreset/setOrthographic`
  (Front=+Z per `samples.cpp:31-33`, Top/Bottom 0.001 tilt).

## Test gate

- Verified (keep green): `math.camera_apply_preset_view_mapping`
  (`tests/test_math.cpp:946`), `math.camera_orbit_clamp_and_pan_scale`
  (`tests/test_math.cpp:312`), `app.resolve_draw_path_routing_matrix`
  (`tests/test_app.cpp:35`).
- Future (explicitly not yet in `tests/`): `palette_fuzzy_substring_rank_orders_prefix_first`,
  `palette_ctrl_k_arbitration_defers_to_appOwnsKeyboard`,
  `palette_action_table_calls_existing_handlers_no_fork`,
  `palette_disabled_rows_state_reasons_no_asset_cannot_undo_bridgeBusy`.
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (same for
  `windows-debug`).

## Failure handling

- Palette failures fail closed: unknown action id or disabled row
  reports an error status and runs nothing — never a half-applied op,
  never fake success. `bridgeBusy` and no-asset gates mirror the menu
  exactly.
- While open, the palette never steals `WantTextInput` owners: if a
  text field owns the keyboard, `Ctrl+K` still opens (explicit user
  intent) but typing stays in the palette field only; closing restores
  the prior focus without firing viewport gestures.
- Bridge wording stays honest (`(bridge)` suffix kept); the palette
  never claims native GR2 emit.
