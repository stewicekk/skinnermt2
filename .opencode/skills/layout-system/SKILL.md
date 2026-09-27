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

- Dock tree (`src/app/panels.cpp` `buildDefaultDockLayout`): Wave-36
  split ORDER — top strip 0.075 (Toolbar) first, then the FULL-WIDTH
  Timeline strip 0.10 (split BEFORE left/right so it spans the window),
  then left 0.17 (Assets/Scene/Skeleton), right 0.35 (Properties 0.6:
  Bone/Weights/Materials/Bone Display/Gizmo/Viewport Settings +
  Workflow 0.4: Export/Project/Settings), center-bottom 0.24 (Output
  0.6: Validation/Console + Tools 0.4). Viewport stays central. No
  `PassthruCentralNode` in the builder (C5054 public/internal enum
  mismatch — the Wave-37 dock mirror in `drawAllPanels` carries it); the
  docked Viewport window stays transparent via load-bearing
  `ImGuiWindowFlags_NoBackground`. imgui.ini migration: an existing
  saved layout keeps its old tree until Reset Viewport Layout / View >
  Reset layout (both live paths, no restart).
- Layout presets (`src/ui_model.cpp` `kLayoutPresets[4][14]`):
  Rig/Paint/Anim/Review write the 14 `UISettings` flags live every frame
  + success status. Reset Viewport Layout deletes `imgui.ini` +
  `requestDockRebuild` + `buildDefaultDockLayout` NOW with no restart;
  View menu Reset is the same live path.
- Panel flags (`include/m2rig/app.hpp` `UISettings`): menu-bar/toolbar/
  status-bar + spacing/rounding/compactMode + 14 panel bools (Bone,
  Weights, Materials, MSM Inspector, Project, Export, Validation,
  Console, System/Tools, Timeline, Settings, BoneDisplay=false,
  Gizmo=false, ViewportSettings=false). Toolbar Panels group = ">>"
  overflow popup over the same bools (`drawToolbar`); View menu Panels
  submenu covers all 14 + MSE tab.
- Toolbar (`src/app/panels_toolbar.cpp` `drawToolbar`): six groups in
  render order Stage|Rig|View|Display|Status|Panels with measured group
  wrap — a group starts a new line when `CursorX + groupWidth` exceeds
  the toolbar width instead of clipping (`toolbarGroupWidth`,
  ui_model's single tested source). Stage = Rig/Paint/Anim/Review
  preset buttons (active row highlighted via `layoutPresetMatches`);
  Rig = red-family Auto-rig (`BeginDisabled` without asset, same
  undoable op as the Weights panel); View = 7-mode segmented + Frame
  (F) (`BeginDisabled` without asset) + Ortho/Persp; Display =
  X-ray/Tex/PBR/Paint/Deform/DQS + Grid/Bones/Wire ovl (same App
  bools); Status = Undo/Redo (gated `canUndo/canRedo`) + Validate +
  FPS + `bridgeBusy` running readout; Panels = ">>" overflow popup
  over the same `UISettings` bools.
- Overlay (`src/app/panels_viewport.cpp` `drawViewportPanel`): Frame
  (F), Ortho/Persp, Front/Back/Top/Bottom/Left/Right `applyPreset`,
  Shading button + `viewport_shading_pop` (7 mode radios + 6 toggles +
  Paint-with-bone-guard). Wave-23 arbitration: no `AllowOverlap`,
  `btnDown` + rect-hover; `shadingOpen` suppresses
  paint/orbit/pan/zoom/click-select. Bottom text lines asset + diag;
  aspect always logical `avail.x/avail.y`. Wave-43: Edit-mode indicator
  ("Mode: Edit (Tab)" + selected-vertex count) bottom-left.
- Status bar (`src/app/panels_toolbar.cpp` `drawStatusBar`): fixed
  24 px bottom; order Viewport WxH ok/too small -> asset
  `id | v/t/bones` (+ `Bone: name [id]`) -> Draw calls (+ amber
  "(no texture)") -> Dist -> right-aligned status; priority collapse
  drops camera first, then draw calls (viewport + asset + status
  stay); status separated by " | " and ellipsis-truncated when it
  would clip; colors via `Theme::statusColor` (info grey / success
  green / warning amber / error red); empty renders `Ready`.
- Toasts (`src/app/panels_toolbar.cpp` `drawToasts`;
  `include/m2rig/app.hpp` `Toast{message,kind,until,sticky,id}` +
  `pushToast/dismissToast/tickToasts`): bottom-right anchor,
  per-toast Y stacking above the 24 px bar (70 px step), one window
  per toast `##Toast+id`, `TextWrapped` + Dismiss for non-sticky,
  expiry erase `!sticky && now>=until`. Contract: sticky errors stay
  until dismissed, info/warning expire.
- Empty-state (`drawViewportPanel`): centered guide — "No model
  loaded", step 1 "Project > Import FBX/GR2 or Project > Load sample
  armor", step 2 "Load a model (Project > Import), then press F to
  frame it", "Drag = orbit | Right-drag = pan | Wheel = zoom"; status
  bar parallel "No model loaded".
- Scroll regions: Viewport window `NoScrollbar|NoScrollWithMouse` +
  min size; inspector trees inner-scroll via `BeginChild`; Skeleton
  action buttons live in a `BeginChild` scroll region (Wave-42); all
  other panels use default ImGui scroll.
- Theme tokens (`src/ui_model.cpp` `themeTokens()` — "newschool"
  dark palette, Wave 41): electric-indigo accent 0.42,0.35,0.92 family
  on a cool blue-grey surface ramp (surface0 0.055,0.060,0.075 ->
  surfaceInput 0.130,0.145,0.175); semantic success/warn/danger/info
  families; 16 chrome tokens promoted from `theme.cpp` literals
  (frameHover/frameActive/titleActive/sliderGrabActive/separator*/
  tab*/scrollbar*/resizeGrip*/dockingPreview) — the theme is fully
  data-driven. Variants: Dark (canonical) / Light / HighContrast via
  `themeTokensFor(ThemeVariant)`, persisted + Settings combo.
  Viewport clear 0.040,0.045,0.060; heatmap ramp modernized
  blue->red jet. Destructive red Auto-rig via `dangerButtonPush/Pop`
  (component kit). Docking via `NavEnableKeyboard | DockingEnable` +
  `config/imgui.ini` (`src/app/main.cpp`).
- Tooltip coverage today: every toolbar control carries
  `IsItemHovered->SetTooltip` (`drawToolbar`), every overlay
  preset/Shading button likewise (`drawViewportPanel`); status/toast
  text is intentionally tooltip-free.
- Bridge boundary (honest): About modal reports Noesis/grnreader
  presence (`drawAllPanels` About modal); GR2 native emit is
  NOT supported directly. Never invent bridge paths.

## Target contract

- Dock presets Rig/Paint/Anim/Review keep the exact 4x14 tables in
  `src/ui_model.cpp` (`kLayoutPresets`) as the single source; any preset
  edit updates the table + the 4 tip strings + this skill, never a
  second copy. Live apply + live reset (no restart) stays load-bearing.
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

- `src/app/panels.cpp`: `buildDefaultDockLayout`, `drawAllPanels`
  (menus, reset paths, About modal, global shortcuts).
- `src/app/panels_toolbar.cpp`: `drawToolbar`, `drawStatusBar`,
  `drawToasts`. `src/app/panels_viewport.cpp`: `drawViewportPanel`
  (overlay, Shading popover, empty-state, Edit-mode indicator).
- `src/ui_model.cpp`: `kLayoutPresets` + `applyLayoutPreset` +
  `layoutPresetMatches` + `themeTokens`/`themeTokensFor`.
- `src/app/theme.cpp`: `applyDarkTheme` (ImVec4 mapping),
  `viewportClearF`/`heatmapU32`. `src/app/main.cpp`:
  `io.ConfigFlags` + `imgui.ini`.
- `include/m2rig/app.hpp`: `UISettings` 14 flags, `Toast` + queue ops,
  `statusMessage/statusKind`.
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
