---
name: ux-audit
description: Triage rule for UX findings - reproduce with file:line, keep load-bearing areas untouched, prefer data fixes in ui_model that tests pin
---

# ux-audit

The admission rule for UX/UI findings (adopted from the solution-judge D6
ruling). A finding is only IN scope when all three gates pass; everything
else is deferred with a written note instead of half-fixed. Extends
`layout-system` (the load-bearing rules listed below) and
`docs-brain-sync` (evidence + doc truth discipline).

## Contract

A finding is accepted only when:

1. **It reproduces.** The report carries a concrete `file:line` (and, where
   relevant, the exact control: menu item, button, panel, keyboard path)
   plus the observed vs expected behavior. "The toolbar feels off" is not
   a finding; "the disabled Undo tooltip disappears" with a location is.
2. **It does not touch a load-bearing area** unless the change is proven by
   an existing test. Load-bearing today:
   - viewport compositing: `ImGuiWindowFlags_NoBackground` on the docked
     Viewport and the offscreen-texture draw (`drawViewportPanel`,
     `renderScene`);
   - `PassthruCentralNode` only on the `DockSpace` call in `drawAllPanels`
     (never on the DockBuilder node);
   - overlay drag arbitration / orbit-paint guards (`btnDown` +
     viewport-rect hover, `shadingOpen` suppressing paint/orbit/pan/zoom/
     click-select);
   - `BeginDisabled` honesty gates (never re-enable a control that cannot
     act to make a tooltip or highlight work);
   - `g_dockBuilt` live rebuild (both Reset paths, no restart);
   - `imgui.ini` persistence (an existing file keeps its tree until Reset);
   - status collapse semantics (camera first, draw second, strict `>`).
3. **The fix is preferably data in `ui-model`** — a label, table row,
   constant or predicate — so `tests/test_ui_model.cpp` pins it. A fix that
   lives in ui_model needs no new UI plumbing to be covered.

## Current truth

- Fixes that pass land WITH regression coverage in the same change; a
  finding whose fix cannot be expressed this way is deferred with a note
  (what, why deferred, what evidence would unblock it) rather than
  partially applied.
- Canonical data homes by finding type:
  - label/wording drift -> `labelXray`/`labelValidate`/`labelValidateMenu`/
    `labelFrameTip` (`ui-model` / `component-kit`);
  - tooltip missing on a disabled control -> `tipFor` in
    `src/app/panels_internal.hpp` (`component-kit`);
  - red/danger styling -> `dangerButtonPush/Pop` (`component-kit`);
  - preset/panel visibility wrong -> `kLayoutPresets` /
    `panelDescriptors` (`ui-model`), never a local table;
  - status/toast geometry or collapse -> `kStatusBarHeight` /
    `kToastAnchorAboveBottom` / `kToastStackStepPx` /
    `statusBarShowCamera` / `statusBarShowDraw` (`status-chrome`);
  - dock ratios/zone membership -> `dockRatios()` / `dockZoneWindows()`
    (`dock-layout`);
  - preference not persisting -> save line + `extractBool` line
    (`prefs-persistence`).
- Known honest-state nuance worth reporting (not silently "fixing"):
  `drawStatusBar`/`drawToasts` inline the same 24/34/70 numbers and the
  same `+80` comparisons that ui_model pins — a divergence report should
  cite both sides (`status-chrome` documents the convergence rule).

## Entry points

- Report format: file:line + reproduction + observed/expected + which of
  the three gates it passes.
- Data homes: `include/m2rig/ui_model.hpp`, `src/ui_model.cpp`,
  `src/app/panels_internal.hpp` (kit), `src/app/theme.cpp` (colors).
- Rule sources: this file, `layout-system` (Wave 22/32 load-bearing list),
  `AGENTS.md` failure protocol.

## Test gate

- Every accepted fix ships a test: suite `ui_model` in
  `tests/test_ui_model.cpp` for data fixes (e.g.
  `gate_predicates_and_canonical_labels`, `status_bar_collapse_thresholds`,
  `dock_plan_ratios_and_zone_membership`, `toolbar_groups_and_wrap_rule`);
  `tests/test_app.cpp` for state/prefs fixes (`resolve_draw_path_routing_matrix`,
  `prefs_round_trip_mse_tab_enabled`).
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Review rule: a PR that claims a UX fix but adds no test and does not name
  the ui_model row it changed is returned, not merged.
