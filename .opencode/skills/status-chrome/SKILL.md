---
name: status-chrome
description: Status bar + toast chrome constants, collapse predicates, sticky TTL rules and the dockspace reserve coupling (extends layout-system)
---

# status-chrome

Bottom chrome: the 24 px status bar and the bottom-right toast stack.
Rendering lives in `src/app/panels_toolbar.cpp` (`drawStatusBar`,
`drawToasts`); the numbers and predicates are single-sourced in
`include/m2rig/ui_model.hpp` and pinned by tests. Extends `layout-system`
(status priority + toast stacking rules) and `ui-model` (the constants).

## Contract

- Chrome geometry is three coupled constants in ui_model, and they must
  move together: `kStatusBarHeight = 24`, `kToastAnchorAboveBottom = 34`
  (bar height + gap), `kToastStackStepPx = 70` (per-toast step). If the
  anchor falls to or below the bar, toasts land under it — the test pins
  `kToastAnchorAboveBottom > kStatusBarHeight`.
- Collapse semantics are strict: `statusBarShowCamera` and
  `statusBarShowDraw` use `>` (a segment shows only when it fits, not when
  it exactly fits), camera drops first, draw drops second; viewport,
  asset and the right-aligned status message never collapse.
- Bar height feeds layout: `kStatusBarHeight` is part of the dockspace
  reserve in the Wave 37 host mirror (`src/app/panels.cpp`
  `drawAllPanels`) — changing the bar height changes docked-panel space.
- Severity vocabulary is shared: `statusKindFrom` / `statusKindSticky` /
  `statusKindToastTtl` / `statusKindPriority` (ui_model) with
  `kToastQueueCap = 6` (oldest dropped first).

## Current truth

- `drawStatusBar(const App&, const Renderer&, const ViewportRect&)`:
  `##StatusBar` window, no decoration/move/docking/background/saved
  settings, pinned to the viewport bottom at 24 px; segment order =
  viewport `WxH` VALID/INVALID -> asset `id | v / t / bones` (+ `Bone:
  name [id]`) -> `Draw calls: N` (+ amber `(untextured fallback)`) ->
  `Eye/Dist` -> right-aligned status coloured by
  `Theme::statusColor(app.statusKind)`, empty renders `Ready`.
- `drawToasts(App&)`: early-out when `app.toasts.empty()`; reverse iterate
  (newest on top), one `##Toast<id>` window per toast at
  `viewport bottom - 34 - index * 70`, `TextWrapped` message, `Dismiss`
  only for non-sticky, expiry erase `!sticky && now >= until`. Sticky
  errors therefore survive `tickToasts` until dismissed.
- TTL/priority rules as implemented: `App::pushToast` (src/app_state.cpp)
  marks `kind == "error"` sticky with ttl 0, warning 8 s, else 5 s and
  caps the queue at 6 — the same table `statusKindToastTtl`/`statusKindSticky`
  pin in ui_model.
- Honest drift note: `drawStatusBar`/`drawToasts` still inline the same
  literals (24 px window size/offset, `-34.0f` anchor, local `70.0f` step)
  and the same `avail > needs + 80.0f` comparisons instead of calling
  `statusBarShowCamera`/`statusBarShowDraw`. The ui_model values are the
  tested source — when touching either function, read the ui_model
  constants/predicates rather than re-deriving them, and never let the two
  diverge.
- Reserve coupling: the host mirror reserves
  `vp->WorkSize.y - menuBarHeight - kStatusBarHeight`, so the bar, the
  toasts and the dock rows share one height number.

## Entry points

- Constants + predicates: `include/m2rig/ui_model.hpp`
  (`kStatusBarHeight`, `kToastAnchorAboveBottom`, `kToastStackStepPx`,
  `statusBarShowCamera`, `statusBarShowDraw`), `src/ui_model.cpp`.
- Rendering: `src/app/panels_toolbar.cpp` `drawStatusBar`, `drawToasts`.
- Queue side: `App::setStatus`, `App::pushToast`, `App::tickToasts`,
  `App::dismissToast` in `src/app_state.cpp` (see `app-state`).
- Reserve consumer: `drawAllPanels` host mirror in `src/app/panels.cpp`.
- `layout-system` owns the priority/stacking rules this skill implements.

## Test gate

- `tests/test_ui_model.cpp` `ui_model.status_bar_collapse_thresholds`:
  exact boundary cases (camera needs `> 200`, draw needs `> 160` for the
  sample needs; both false AT the boundary), the draw-shows-camera-does-not
  middle case, plus the constant coupling pins
  (`kStatusBarHeight == 24`, `kToastAnchorAboveBottom > kStatusBarHeight`,
  `kToastStackStepPx > 0`).
- `ui_model.status_kind_roundtrip_and_unknown_degrades`,
  `ui_model.toast_ttl_sticky_and_cap_contract` cover the vocabulary.
- Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure` (release gate at the
  time of writing: 18/18 ctest tests, `m2rig_tests` = 213/213 cases).
- Manual gate: narrow the window until draw/camera segments disappear
  (right-aligned message must never clip), push an error toast (stays
  until Dismiss), and confirm docked panels keep their first/last rows
  (reserve) and toasts clear the bar.
