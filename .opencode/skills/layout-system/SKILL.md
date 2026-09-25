---
name: layout-system
description: Dock presets, toolbar/overlay wrap, status priority, toast stacking, empty-state, scroll regions, theme tokens, tooltip coverage (Wave 32)
---

# layout-system

Dock presets, toolbar/overlay wrap rules, status-bar priority, toast
stacking, empty-state centering, scroll-region policy, theme-token
palette and tooltip coverage over the EXISTING ImGui dock in
`src/app/panels.cpp` + `src/app/main.cpp`. Extends `viewport-ux` +
`workspace-restore`; layout state never forks (single `UISettings`
flags + `imgui.ini`).

## Current truth

- Dock tree (`src/app/panels.cpp:3548-3599` `buildDefaultDockLayout`):
  Left 0.17 (`:3560`), Right 0.35 (`:3562`), Down 0.24 (`:3564`), Up
  0.075 (`:3566`); right split Properties/Workflow 0.4 (`:3570`),
  bottom split Output/Tools 0.4 (`:3574`). Docks: Toolbar->Top
  (`:3576`), Assets/Scene/Skeleton->Left (`:3577-3579`),
  Viewport->Main (`:3580`), Bone/Weights/Materials/Bone
  Display/Gizmo/Viewport Settings->Props (`:3582-3587`),
  Export/Project/Settings->Workflow (`:3589-3591`),
  Validation/Console->Output (`:3593-3594`), Timeline/Tools->Tools
  (`:3596-3597`). No `PassthruCentralNode` by design (`:3550-3555`);
  Viewport stays transparent via load-bearing
  `ImGuiWindowFlags_NoBackground` (`:2867-2869`, `:3554-3555`).
- Layout presets (`src/app/panels.cpp:3341-3383`): `kLayoutPresets[4][14]`
  Rig/Paint/Anim/Review (`:3345-3358`, names `:3359`, tips `:3360-3365`)
  write the 14 `UISettings` flags live every frame (`:3368-3380`) +
  success status (`:3379-3380`). Reset Viewport Layout (`:3386-3396`)
  deletes `imgui.ini` + `requestDockRebuild` + `buildDefaultDockLayout`
  NOW with no restart (`:3390-3395`); View menu Reset (`:3762-3772`)
  is the same live path.
- Panel flags (`include/m2rig/app.hpp:195-219` `UISettings`):
  `showMenuBar/Toolbar/StatusBar` + spacing/rounding/compactMode
  (`:196-201`) + 14 panel bools (`:204-218`: Bone, Weights, Materials,
  MSMInspector, Project, Export, Validation, Console, System/Tools,
  Timeline, Settings, BoneDisplay=false, Gizmo=false,
  ViewportSettings=false). Toolbar quick-toggles a 7-subset
  (`src/app/panels.cpp:1025-1043`); View menu Panels submenu covers all
  14 + MSE tab (`:3741-3760`).
- Toolbar (`src/app/panels.cpp:915-1051` `drawToolbar`): view-mode
  segmented (`:916`), Frame(F) `BeginDisabled` without asset
  (`:920-924`), Persp/Ortho (`:928-929`), X-ray/Tex/PBR/Paint/Deform/DQS
  (`:934-979`), red-family Auto-rig same op as Weights panel
  (`:980-994`), Undo/Redo gated on `canUndo/canRedo` (`:996-1006`),
  Validate (`:1008`), Grid/Bones/Wire-ovl (`:1013-1021`), panel buttons
  (`:1025-1043`), FPS + `bridgeBusy` running readout (`:1045-1050`).
  Today one `SameLine` chain — no wrap rule (overflows narrow docks).
- Overlay (`src/app/panels.cpp:3183-3247`): Frame(F) (`:3186`),
  Ortho/Persp (`:3193-3194`), Front/Back/Top/Bottom/Left/Right
  `applyPreset` (`:3197-3213`), Shading button + `viewport_shading_pop`
  (`:3218-3247`: 7 mode radios `:3221-3230`, 6 toggles `:3232-3239`,
  Paint-with-bone-guard `:3241-3245`). Wave-23 arbitration: no
  `AllowOverlap`, `btnDown` + rect-hover (`:2888-2912`); `shadingOpen`
  suppresses paint/orbit/pan/zoom/click-select (`:2910`, `:2929-2945`,
  `:2977-2982`). Bottom text lines asset (`:3252-3254`) + diag
  (`:3258-3261`); aspect always logical `avail.x/avail.y` (`:3079`,
  `:3117`, `:3012`).
- Status bar (`src/app/panels.cpp:1054-1127` `drawStatusBar`, called
  `:3956`): fixed 24 px bottom (`:1059-1060`), order Viewport WxH
  VALID/INVALID (`:1067`) -> asset v/t/bones (`:1074-1078`) + Bone id
  (`:1084`) -> Draw calls + amber untextured-fallback (`:1096-1100`)
  -> Eye + Dist (`:1107`) -> right-aligned status (`:1114-1123`),
  colors info grey / success green / warning amber / error red
  (`:1119-1122`), empty renders `Ready` (`:1123`).
- Toasts (`src/app/panels.cpp:1130-1171` `drawToasts`, called `:3957`;
  `include/m2rig/app.hpp:303-315` `Toast{message,kind,until,sticky,id}`
  + `pushToast/dismissToast/tickToasts`): bottom-right anchor
  (`:1138-1140`), reverse-iterate stacking (`:1144`), one window per
  toast `##Toast+id` (`:1152`), `TextWrapped` + Dismiss for non-sticky
  (`:1153-1159`), expiry erase `!sticky && now>=until` (`:1166-1170`).
  Contract: sticky errors stay until dismissed, info/warning expire.
- Empty-state (`src/app/panels.cpp:3262-3279`, centered at
  `cursor+(0.5w,0.45h)` `:3263`): `No model loaded` (`:3265`), step 1
  `Project > Import FBX/GR2 or Load sample armor` (`:3267`), step 2
  `Press F` (`:3269`), `Drag=orbit | Right-drag=pan | Wheel=zoom`
  (`:3271`), bottom hint (`:3274`), diag line (`:3277-3278`); status bar
  parallel `No model loaded` (`:1088`). Too-small warning (`:3280-3285`).
- Scroll regions: Viewport window `NoScrollbar|NoScrollWithMouse`
  (`:2868`) + 320x240 min size (`:2866`); inspector trees inner-scroll
  via `BeginChild("msm_tree" ...)` (`:529`) and
  `BeginChild("mse_tree" ...)` (`:632`); all other panels use default
  ImGui scroll (no custom scroll code today).
- Theme tokens (`src/app/main.cpp:59-137` `applyDarkTheme`, wired
  `:182`): rounding 6/4/4/4/4/6/4 (`:61-67`), padding 12,12 / Frame
  10,6 / spacing 10,8 (`:70-72`), WindowBg 0.08,0.09,0.11 + Child/Popup/
  DockingEmptyBg (`:83-86`), teal-cyan accent 0.30,0.78,0.82 family —
  TextSelectedBg (`:102`), Header/Button + hover/active
  (`:104-110`), CheckMark/SliderGrab (`:111-113`), Separator hover/active
  (`:116-117`), TabActive (`:121`), DockingPreview 0.40 alpha (`:134`),
  Modal dim 0.70 (`:136`). Destructive red Auto-rig 0.55,0.22,0.20
  (`src/app/panels.cpp:984-986`). Docking via `NavEnableKeyboard |
  DockingEnable` + `config/imgui.ini` (`src/app/main.cpp:178-181`).
- Tooltip coverage today: every toolbar control carries
  `IsItemHovered->SetTooltip` (`src/app/panels.cpp:925-1027`), every
  overlay preset/Shading button likewise (`:3190-3219`); status/toast
  text is intentionally tooltip-free.
- Bridge boundary (honest): About modal reports Noesis/grnreader
  presence (`src/app/panels.cpp:3817-3822`); GR2 native emit is
  NOT supported directly (`:3823`). Never invent bridge paths.

## Target contract

- Dock presets Rig/Paint/Anim/Review keep the exact 4x14 tables at
  `panels.cpp:3345-3358` as the single source; any preset edit updates
  the table + the 4 tip strings + this skill, never a second copy.
  Live apply + live reset (no restart) stays load-bearing.
- Toolbar wrap rule: `SameLine` chain gains measured-wrap — when
  `GetContentRegionAvail` < next-button width, wrap to a second row
  instead of clipping; overlay row wraps preset buttons before Shading.
  Frame/Undo/Redo stay `BeginDisabled`-gated (no fake-enabled clicks).
- Status priority: error > warning > success > info; concurrent
  messages queue with the highest severity right-aligned; `Ready`
  only when the queue is empty. Order of the five segments unchanged.
- Toast stacking: newest on top (keep reverse-iterate), max 5 visible
  with overflow counter (`+N more`); sticky errors never auto-expire,
  never hidden behind the counter; Dismiss only touches its own id.
- Empty-state centering: the 4-line guide stays centered at
  0.5w/0.45h on any viewport size; hints name only real verbs
  (`Project > Import FBX/GR2`, `Project > Load sample armor`, `F`,
  orbit/pan/zoom). No dead menu paths (Wave-18 `File >` class fix kept).
- Scroll-region policy: Viewport never scrolls (`NoScrollbar` kept);
  inspector trees keep inner `BeginChild` scroll; panels taller than
  the dock scroll the panel, never the whole dock node; no nested
  scrollable child inside another scrollable child without an explicit
  height.
- Theme-token palette: every new color reads the `applyDarkTheme`
  tokens (teal-cyan accent, red destructive, amber warning, green
  success) — no hardcoded ad-hoc colors; new panels get rounding/
  padding from the same function.
- Tooltip coverage rule: every toolbar/overlay Button/Checkbox ships
  with a `SetTooltip` naming the action + shortcut + precondition
  (bone-needed, asset-needed); status/toast stay tooltip-free.

## Entry points

- `src/app/panels.cpp`: `drawToolbar` (`:915`), `drawStatusBar`
  (`:1054`), `drawToasts` (`:1130`), `drawViewportPanel` (`:2863`,
  overlay `:3183`, empty-state `:3262`), Settings presets
  (`:3341-3396`), `buildDefaultDockLayout` (`:3548`),
  `drawMainMenuBar/drawAllPanels` reset paths (`:3762-3772`, `:3956`).
- `src/app/main.cpp`: `applyDarkTheme` (`:59`), `io.ConfigFlags` +
  `imgui.ini` (`:178-181`).
- `include/m2rig/app.hpp`: `UISettings` 14 flags (`:195-219`), `Toast`
  + queue ops (`:303-315`), `statusMessage/statusKind` (`:169-170`).
- `workspace-restore` owns `user_prefs.json` vs `imgui.ini` split;
  `viewport-ux` owns overlay-vs-orbit arbitration + Shading popover.

## Test gate

- Verified (re-run every layout PR): `math.camera_apply_preset_view_mapping`
  (`tests/test_math.cpp:946`, Front yaw 0 / Back |yaw| pi / Top-Bottom
  tilt guard), `math.grid_blue_axis_points_positive_z` (`:992`),
  `math.camera_orbit_applies_dpi_scale` (`:1024`),
  `app.resolve_draw_path_routing_matrix` (`tests/test_app.cpp:35`).
- Future (explicitly not yet in `tests/`): `layout_preset_rig_paint_anim_review_applies_14_flags`,
  `layout_toolbar_overlay_wrap_no_overlap`,
  `layout_status_priority_error_over_info`,
  `layout_toast_sticky_error_survives_tick`,
  `layout_empty_state_centered_hints`,
  `layout_scroll_viewport_noscroll_inner_child_scrolls`,
  `layout_theme_token_teal_accent_pinned`,
  `layout_tooltip_every_toolbar_control`.
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (same for
  `windows-debug`).

## Failure handling

- Wave-23 rules are load-bearing (logical aspect, tilt guards,
  Front=+Z, overlay arbitration) — any layout PR re-runs their tests
  first; look-vs-truth conflicts go to the solution-judge (evidence
  wins, cf. Front/Back revert).
- Preset/reset regressions fail closed: if the 14-flag apply or live
  reset mis-docks, keep the last good `imgui.ini` and report an error
  status — never a half-built dock tree, never fake success.
- Bridge wording stays honest (`NOT_SUPPORTED_DIRECTLY`); layout docs
  never promise native GR2 emit.
