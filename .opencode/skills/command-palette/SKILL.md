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

- No palette exists today: `Ctrl+K` has zero hits in `src/app/`; no
  fuzzy code exists (zero `fuzzy` hits). This skill is a build plan
  over verified handlers, not a description of shipped UI.
- Keyboard arbitration (`src/app/panels.cpp:3782-3813`): globals gated
  by `appOwnsKeyboard = !WantTextInput && !WantCaptureKeyboard`
  (`:3784`); timeline duplicate gate `timelineOwnsKeyboard` (`:2603-2608`
  `drawTimelinePanel`); viewport `F` gated on `!WantTextInput` (`:2946`);
  `shadingOpen` suppresses paint/orbit/pan/zoom/click-select (`:2910`,
  `:2929-2945`, `:2977-2982`). Shortcuts table documents the full set
  (`:3828-3847`: F, Space, Arrows, Ctrl+Z/Y, Ctrl/Shift+drag paint,
  Drag orbit, Right/Middle pan, Wheel zoom, click select, G/B/X/W/P/D,
  1-7, T, Shift-drag box-select, Ctrl+click additive).
- Global keys mirror menu ops one-to-one (`:3786-3811`): F frame
  (`:3786-3788`), Ctrl+Z/Y undo/redo (`:3790-3791`), G/B/X/W/P/D/T
  toggles (`:3793-3805`), 1-7 view modes (`:3806-3811`).
- Project menu handlers — the palette's import/export verbs
  (`src/app/panels.cpp:3669-3700`): Load sample armor `loadSampleArmor`
  (`:3670-3673`), Import SMD `doImportSmd` (`:3674`), Import FBX/GR2
  bridge `doImportBridged` gated on `bridgeBusy` (`:3675-3677`), Export
  SMD/MSM/GR2-bridge/FBX-Noesis/ANI/GR2->FBX/LOD/batch
  `doExportSmd/doExportMsm/doExportGr2/doExportFbx/doExportAni/
  doExportGr2ToFbx/doExportLod/exportAllBatch` (`:3678-3688`), Save/Load
  workspace `doSaveWorkspace/doLoadWorkspace` (`:3690-3691`), Undo/Redo
  gated on `canUndo/canRedo` (`:3693-3698`), Run validation
  `runValidation` (`:3699`).
- View menu handlers (`src/app/panels.cpp:3702-3773`): Grid/Bones/X-ray/
  Wire toggles (`:3708-3711`), Textured (`:3712-3714`), PBR (`:3715`),
  Paint (`:3716`), Deform (`:3717-3719`), 7 modes with 1-7 shortcuts
  (`:3721-3729`), Orthographic (`:3731`), Frame all F gated without
  asset (`:3732-3736`), VSync (`:3739`), Panels submenu 14 flags + MSE
  tab (`:3741-3760`), live Reset layout (`:3762-3772`).
- Single-state rule (no-fork proof): toolbar view-mode segmented
  (`:916`), Shading radios (`:3221-3230`) and View menu radios
  (`:3723-3729`) all write `app.viewMode` + `gpuDirty`; X-ray/Tex/PBR/
  Paint/Deform toggles likewise single bools (`:934-979`, `:3232-3245`,
  `:3708-3719`). Camera presets Front/Back/Top/Bottom/Left/Right call
  `applyPreset` (`:3197-3213`); Frame/Ortho call
  `frameAabb/frameAabbSmooth/setOrthographic` (`:921-929`, `:3186-3194`).
- Bridge boundary (honest): grnreader98 primary + Noesis fallback,
  presence surfaced in About (`:3817-3822`); GR2 native emit NOT
  supported directly (`:3823`). Palette labels must say `(bridge)`
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

- `src/app/panels.cpp`: Project menu (`:3669-3700`), View menu
  (`:3702-3773`), global shortcuts + `appOwnsKeyboard` (`:3782-3813`),
  Shortcuts table (`:3828-3847`), overlay presets + Shading
  (`:3186-3247`), toolbar (`:915-1051`), timeline transport gate
  (`:2603-2620`).
- `include/m2rig/app.hpp`: `undo/redo/canUndo/canRedo` (`:383-387`),
  `runValidation`, `viewMode/textured/usePbr/paintMode/previewDeform`
  (`:128-149`), `UISettings` flags (`:195-219`), `setStatus`
  (`:296`).
- `include/m2rig/camera.hpp`: `frameAabb/applyPreset/setOrthographic`
  (Front=+Z per `samples.cpp:31-33`, Top/Bottom 0.001 tilt).
- Future home (not yet created): `src/app/palette.cpp` calling the
  above; rank helper in `m2rig_core` for headless tests.

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
