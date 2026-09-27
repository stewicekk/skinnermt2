---
name: ui-model
description: Core UI truth tables in m2rig_core (theme/style tokens, 4x14 layout presets, 19 panel descriptors, dock plan, status/toast rules, view modes, toolbar groups, canonical labels; no ImGui)
---

# ui-model

The single source of truth for UI facts that the ImGui layer renders but the
core can own: `include/m2rig/ui_model.hpp` + `src/ui_model.cpp`, compiled into
`m2rig_core` (CMakeLists `src/ui_model.cpp`), so the exe AND `m2rig_tests`
link exactly the same tables. Zero third-party deps, NO ImGui types —
`include/m2rig/*.hpp` must never include `<imgui.h>`. Extends
`layout-system` (its preset/status/toast/toolbar rules live here as data);
feeds `command-palette` (matcher) and `component-kit` (labels/gates).

## Contract

- A behavior-changing visual fix edits BOTH the table in `src/ui_model.cpp`
  and its golden in `tests/test_ui_model.cpp` in the SAME change — the test
  is the spec, the table is the implementation. Never one without the other.
- Out-of-range inputs degrade safely (`false`, `""`, `nullptr`, `0.0f`),
  never UB: `layoutPresetValue/Name/Tip`, `panelFlagPtr`,
  `viewModeNameByIndex`, `toolbarGroupName`, `toolbarGroupWidth`,
  `dockZoneWindows`.
- New UI facts (labels, group order, ratios, TTLs, gates) land HERE first
  and get consumed from here; a second copy inside
  `src/app/panels_*.cpp` is a defect (see `panel-modules`: local preset/
  label tables are forbidden).

## Current truth

- Status vocabulary: `StatusKind` + `statusKindFrom`/`statusKindName`
  (wire strings `info|success|warning|error`; unknown strings degrade to
  Info, never transparent), `statusKindSticky` (error only),
  `statusKindToastTtl` (error 0 s = sticky, warning 8 s, info/success 5 s),
  `statusKindPriority` (error > warning > success > info),
  `kToastQueueCap = 6` (oldest dropped first).
- Theme VALUES as pure floats: `ThemeTokens` + `themeTokens()` (accent
  family incl. `accentSoft`, success/`warn`/`danger`/`info`/`infoMuted`,
  the `destructive*` trio, surfaces 0-3 + `surfaceInput`, text family,
  `border`, `viewportClear`, `heatmap[5]`) and `StyleMetrics` +
  `styleMetrics()` (7 roundings, 3 border sizes, paddings/spacings,
  indent/scrollbar/grab sizes). `src/app/theme.cpp` is the ONLY mapper to
  ImVec4.
- Layout presets: `kLayoutPresetCount = 4` x `kPanelFlagCount = 14`;
  columns follow `App::UISettings` declaration order (Bone, Weights,
  Materials, MSMInspector, Project, Export, Validation, Console, System,
  Timeline, Settings, BoneDisplay, Gizmo, ViewportSettings). API:
  `layoutPresetValue`, `layoutPresetName`, `layoutPresetTip`,
  `applyLayoutPreset`, `layoutPresetMatches`. Rows: **Rig** enables Bone +
  Weights + MSM + Project + Validation + Tools + Bone Display + Gizmo (Wave
  36 switched MSM/Tools on), **Paint** = Bone + Weights + Materials +
  Viewport Settings, **Anim** = Bone + Timeline + Gizmo, **Review** =
  Materials + Export + Validation + Console + Tools + Timeline. Tips must
  name every enabled panel (pinned).
- Panel descriptors: `panelDescriptors()` returns 19 entries — 14 flagged
  with `presetColumn` 0..13 and 5 always-on chrome (Toolbar, Assets, Scene,
  Skeleton, Viewport) with `presetColumn = -1`. Fields: `id`, `window`
  (`ImGui::Begin` title, `""` for tab-only), `dockWindow`, `menuLabel`
  (`View > Panels`, `""` = not listed), `prefsKey` (`user_prefs.json`,
  `""` = not persisted), `defaultVisible`. `findPanelDescriptor`,
  `panelFlagPtr` (nullptr when not flagged).
- Dock plan (tested mirror of `buildDefaultDockLayout`): `dockRatios()` =
  `{left .17, right .35, bottom .24, top .075, timeline .10,
  rightWorkflowSplit .4, bottomToolsSplit .4}`; `DockZone` has 8 values
  including `TimelineStrip`; `dockZoneWindows(zone, count)` lists the window
  titles per zone (RightProps = 6, RightWorkflow = 3, BottomOutput = 2,
  TimelineStrip = 1).
- Status chrome constants: `kStatusBarHeight = 24`,
  `kToastAnchorAboveBottom = 34`, `kToastStackStepPx = 70` — coupled on
  purpose (if they drift, toasts land under the bar);
  `statusBarShowCamera(avail, drawNeed, camNeed, msgNeed)` and
  `statusBarShowDraw(avail, drawNeed, msgNeed)` collapse with strict `>`
  and `+80` slack: camera drops first, draw second, viewport/asset/status
  segments always show.
- View modes: `kViewModeCount = 7`; `viewModeName` (menu label),
  `viewModeShortLabel` (segmented control), `viewModeTip` (with digit
  shortcut), `viewModeNameByIndex` (nullptr when out of range).
- Toolbar groups: `kToolbarGroupCount = 6`, order Stage | Rig | View |
  Display | Status | Panels, `toolbarGroupName`, `toolbarGroupWidth`
  (measured widths for the default style), `toolbarShouldWrap` (wraps only
  on overflow; exact fit does not wrap).
- Palette matcher `uiFuzzyMatchPos` (earliest case-insensitive substring
  hit), gate `exportActionEnabled(hasAsset)`, canonical labels
  `labelXray()` = "X-ray bones", `labelValidate()` = "Validate",
  `labelValidateMenu()` = "Run validation", `labelFrameTip()`.

## Entry points

- Tables + API: `include/m2rig/ui_model.hpp`, `src/ui_model.cpp`.
- Consumers: `src/app/panels_internal.hpp` (includes it for every panels
  TU), `src/app/theme.cpp`, `src/renderer.cpp` (core-only include path),
  `src/app/panels.cpp` (menu gates + dock reserve),
  `src/app/panels_toolbar.cpp` / `panels_display.cpp` / `panels_actions.cpp`.
- Owning neighbours: `theme-module` (floats -> ImVec4),
  `status-chrome` (chrome rendering that must read these constants),
  `dock-layout` (the builder the dock tables mirror).

## Test gate

- Suite `ui_model` in `tests/test_ui_model.cpp`: `status_kind_roundtrip_and_unknown_degrades`,
  `toast_ttl_sticky_and_cap_contract`, `theme_tokens_accent_and_semantic_families_pinned`,
  `theme_surfaces_and_viewport_clear_pinned`, `style_metrics_match_apply_dark_theme`,
  `layout_presets_pin_current_matrix`, `layout_preset_tip_mentions_every_enabled_panel`,
  `layout_preset_apply_writes_exactly_14_flags`, `panel_descriptors_cover_flags_menu_and_prefs`,
  `dock_plan_ratios_and_zone_membership`, `status_bar_collapse_thresholds`,
  `view_mode_names_match_enum_order`, `toolbar_groups_and_wrap_rule`,
  `layout_preset_matches_after_apply`, `fuzzy_match_earliest_case_insensitive`,
  `gate_predicates_and_canonical_labels`.
   Count: 16 tests (verified with `Select-String -Pattern '^M2RIG_TEST'
   tests\test_ui_model.cpp` — re-measure before quoting, never trust a
   count that was not just measured).
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (same for
  `windows-debug`). Release gate at the time of writing: 18/18 ctest
  tests, `m2rig_tests` = 213/213 cases.
- Suite-level evidence without a rebuild: run
  `build\release\Release\m2rig_tests.exe` — the runner prints one
  `[suite <name>]` block per suite plus `N/N cases passed`.
