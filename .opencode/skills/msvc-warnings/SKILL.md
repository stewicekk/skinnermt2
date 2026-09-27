---
name: msvc-warnings
description: /W4 /WX /FS warning discipline - C4127 constexpr-condition workarounds, one MSBuild per build dir, build-fix-focused-full gate loop (extends build-ci)
---

# msvc-warnings

Warning hygiene for the MSVC-only build: first-party targets compile
`/W4` with `/WX` (warnings are errors) and everything must stay clean
without suppressions. Extends `build-ci` (presets, budgets, harness); this
skill owns the warning-level contract and the fix loop.

## Contract

- CMakeLists sets `M2RIG_WARNINGS_AS_ERRORS` ON by default, so first-party
  targets (`m2rig_core`, `m2rig_fbx`, `m2rig_gltf`, `m2rig_tests`,
  `m2rig_cli`, `Metin2RiggingStudio`) get `/W4` + `/WX`; third-party
  wrappers (`openfbx_lib`, `cgltf_lib`, `meshopt_lib`, `imgui_lib`,
  `imguizmo_lib`) stay `/W3 /WX-` — do not tighten or loosen either side.
- Global MSVC flags: `/permissive- /Zc:__cplusplus /utf-8 /FS`.
  With `/WX`, a warning surfaces as error `C2220` (warning treated as
  error), so the fix belongs in the source, never in a blanket disable.
- Never run two MSBuilds on one build dir (PDB lock) — one build at a
  time, and never start a build while another session holds
  `build\release` / `build\debug`.
- No `#pragma warning(disable: ...)` and no `/wd` for first-party code
  unless the case is documented in the same change.

## Current truth

- **C4127** "conditional expression is constant" is the recurring one —
  `constexpr` conditions in tests AND production trip it:
  - tests: use the `probe()` template trick (existing in
    `tests/test_ui_model.cpp`) — passing the constant through a function
    defeats constant folding, e.g. `CHECK_EQ(probe(kToolbarGroupCount), 6)`
    instead of `CHECK_EQ(kToolbarGroupCount, 6)`;
  - production: use a ternary or a runtime `bool` instead of
    `if (constexpr)` — e.g. `src/app/panels.cpp` builds the host
    `ImGuiWindowFlags` with a ternary and comments that an `if` over the
    `constexpr` dockspace flags would trip C4127.
- Warnings-as-errors covers every TU that lands in a first-party target,
  including `src/renderer.cpp` (compiled into BOTH `Metin2RiggingStudio`
  and `m2rig_tests`) and `src/app_state.cpp`.
- Known constraint from AGENTS.md: `/W4 /WX /FS` clean is a stated
  property of the tree — a PR that introduces a warning is not mergeable
  even if tests pass.

## Entry points

- Flag wiring: `CMakeLists.txt` (`M2RIG_WARNINGS_AS_ERRORS`, the
  `target_compile_options` blocks per target, the global `add_compile_options`).
- Workaround precedents: `tests/test_ui_model.cpp` (`probe()`);
  `src/app/panels.cpp` (ternary over constexpr flags).
- Sibling: `build-ci` (presets, budgets, CI matrix), `testing` (harness),
  `msvc-engineer` agent owns this skill.

## Test gate

- Gate loop (in order, no skipping):
  1. `cmake --preset windows-release`
  2. `cmake --build --preset windows-release`
  3. fix the warning at its source (data/type/logic — not a disable)
  4. focused test: run `build\release\Release\m2rig_tests.exe` (prints
     `[suite <name>]` blocks + `N/N cases passed`) or re-run the affected
     `ctest` cases
  5. full regression: `ctest --preset windows-release --output-on-failure`
- Release gate at the time of writing: 18/18 ctest tests, `m2rig_tests` =
  213/213 cases. Same loop with `windows-debug` when the change is
  debug-only (note: debug heap-triage runs are opt-in via
  `M2RIG_HEAP_TRIAGE` and are slow).
- Evidence to report: the exact compiler message (`C4127` etc.), the file
  you changed, and the green gate output — never "should be clean".
