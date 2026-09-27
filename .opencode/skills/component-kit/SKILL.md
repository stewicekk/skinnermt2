---
name: component-kit
description: Wave 37 component kit - tipFor disabled-aware tooltips, dangerButton red family, canonical Validate/X-ray labels and exportActionEnabled gates
---

# component-kit

Shared button chrome and label/gate discipline for every UI surface
(toolbar, menus, panels, palette). The kit lives as inline helpers in
`src/app/panels_internal.hpp` and as data in `include/m2rig/ui_model.hpp`.
Extends `ui-model` (labels + gates are ui_model data) and
`command-palette` (the palette fuzzy-matches the same label text).

## Contract

- `tipFor(const char*)` is THE tooltip helper for a just-submitted item:
  it hovers with `ImGuiHoveredFlags_AllowWhenDisabled`, so a disabled
  control still explains WHY it is disabled. Plain `IsItemHovered()` on a
  disabled item silently drops the tooltip — that was the Wave 37 bug.
- `dangerButtonPush()` / `dangerButtonPop()` push/pop the ONE red family
  (Button / ButtonHovered / ButtonActive from `Theme::kDanger*`, i.e.
  `theme::danger*` from `src/app/theme.cpp`). Destructive-looking but
  undoable operations use it; do not hand-roll `PushStyleColor` trios.
- Canonical labels: UI surfaces call `labelXray()`, `labelValidate()`,
  `labelValidateMenu()` — raw `"Validate"` / `"X-ray"` string literals in
  button/menu code are forbidden.
  - `labelValidate()` (button) = "Validate" and `labelValidateMenu()`
    (menu + palette) = "Run validation" MUST stay distinct strings: the
    palette fuzzy-matches label text, so collapsing them would merge two
    different affordances.
  - `labelXray()` = "X-ray bones" (checkbox/menu), `labelFrameTip()` is
    the shared Frame tooltip.
- Export gating: menu export items go through
  `exportActionEnabled(hasAsset)` (`src/app/panels.cpp` File menu
  `exportItem` helper); `GR2 -> FBX (Noesis)...` is a FILE CONVERSION and
  is deliberately not gated — it stays available without an asset.

## Current truth

- Kit definitions: `tipFor`, `dangerButtonPush/dangerButtonPop`,
  `frameWholeModel`, and the `Theme::` forwarders — all in
  `src/app/panels_internal.hpp`, so all 8 `panels_*.cpp` TUs share them.
- Red-family call sites today: `Auto-rig` in
  `src/app/panels_toolbar.cpp` (inside `BeginDisabled` without an asset)
  and the Flood/Prune pair in `src/app/panels_props.cpp`
  (`dangerButtonPush` before, `dangerButtonPop` after).
- Label call sites: `labelXray()` in `panels.cpp` (View menu),
  `panels_toolbar.cpp`, `panels_props.cpp`, `panels_display.cpp`,
  `panels_viewport.cpp`; `labelValidate()` button in `panels_toolbar.cpp`,
  `panels_props.cpp`, `panels_side.cpp`; `labelValidateMenu()` in the
  `panels.cpp` File menu and in the palette table of
  `panels_actions.cpp`.
- Gate: `exportActionEnabled(bool hasAsset) { return hasAsset; }` in
  `src/ui_model.cpp`; the File menu wraps SMD/MSM/GR2/FBX/ANI/LOD/batch
  export items in `BeginDisabled(!canExport)`. Note the Export panel
  (`drawExportPanel` in `panels_workflow.cpp`) currently wraps ALL Format
  buttons — including `GR2 -> FBX (Noesis)...` — in one
  `BeginDisabled(app.currentAsset() == nullptr)` block; the asset-free GR2
  -> FBX path is the menu item.
- Disabled-with-tip pattern in practice: Frame (F), Undo/Redo, Auto-rig,
  the do* actions with `tipFor("...")` immediately after the control.

## Entry points

- Kit: `src/app/panels_internal.hpp` (`tipFor`,
  `dangerButtonPush/Pop`, `frameWholeModel`).
- Data: `include/m2rig/ui_model.hpp` + `src/ui_model.cpp`
  (`labelXray`, `labelValidate`, `labelValidateMenu`, `labelFrameTip`,
  `exportActionEnabled`, `uiFuzzyMatchPos`).
- Consumers: `src/app/panels_{toolbar,props,side,workflow,actions}.cpp`,
  `src/app/panels.cpp`.
- Colour source: `src/app/theme.cpp` (`danger`, `dangerHover`,
  `dangerActive`); matcher: `command-palette`.

## Test gate

- `tests/test_ui_model.cpp` `ui_model.gate_predicates_and_canonical_labels`
  pins the four labels (including Validate vs Run validation distinctness)
  and `exportActionEnabled`; `ui_model.fuzzy_match_earliest_case_insensitive`
  pins the matcher the palette uses.
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Review rule: any new Button/MenuItem either calls a `label*()` helper or
  justifies why its literal is unique; any new destructive action uses
  `dangerButtonPush/Pop`; any new disabled control ships `tipFor`.
