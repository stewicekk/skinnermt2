# Skill: Docs & Skills Index

## Docs (`docs/`)
- `AGENT_STATE.md` — wave log + architecture + test coverage (authoritative)
- `MASTER_PROMPT.md` — orchestration spec (agents, skills, waves)
- `REPOSITORY_AUDIT.md`, `IMPLEMENTATION_PLAN.md`, `USER_GUIDE.md`
- `DEPENDENCIES.md`, `CHANGELOG.md` — pins + release history

## Skills (`.opencode/skills/`, tracked .md + 5 native guides)
Build/test: `build-system`, `build-rigapp`, `regression-tests`,
`run-regression`, `validate-models`, `cli-reference`.
Pipeline: `add-export-format`, `add-transfer-algorithm`, `analyze-models`,
`armor-transfer`, `batchprocessor-noesis`, `run-batch-pipeline`,
`pipeline-orchestrator`, `export-rigged-collection`, `validate-models`.
Formats/tools: `metin2core-noesis`, `noesis-integration-cs`,
`metin2-armor-system`, `viewport3d-control`, `theme-system`,
`weight-painter`, `weight-transfer-engine`, `weight-transfer-knn`,
`auto-bone-binding`, `rigaapppaths-update`, `cleanup-legacy-code`,
`cleanup-root-folder`, `constraint-enforcer`, `deformation-validation`,
`docs-skills` (this file), `dual-quaternion-skinning`, `lod-optimization`,
`train-ml-model`.
Viewport/UI integration (Wave 15): `gizmo-manipulators`,
`autosave-recovery`, `msm-inspector`, `textured-viewport`,
`flood-prune-autorig`.
10x program (Step 1-2, `docs/UPGRADE_10X_BRAINSTORM.md`): `pbr-rendering`,
`gpu-skinning`, `modern-textures`, `quaternion-anim`, `spatial-index`,
`gltf-pipeline`, `viewport-ux`, `workspace-restore`, `build-ci`,
`docs-brain-sync` (all with contract + entry points + test gates).
Wave 30+ program (Step 3, `docs/UPGRADE_10X_BRAINSTORM.md` items 9-11):
`threejs-parity` (native three.js-grade viewport, explicit webview
AGAINST; extends `gltf-pipeline` + `viewport-ux`), `cz-localization`
(string inventory + `cs.json` key plan + diacritics font gate, persisted
keys NEVER translated; extends `workspace-restore` + `viewport-ux`),
`release-ops` (version single-source, `package-windows.ps1` ZIP+SHA,
changelog/guide truth-pass gate; extends `build-ci` +
`docs-brain-sync`), `mse-effects` (`MseRuntime::update` overlay wiring +
bone resolver + real-`.mse` golden; extends `gltf-pipeline` +
`pbr-rendering`), `weight-table` (virtualized per-vertex table over
`set/remove/normalizeVertex` + `boneHistogram_`; extends `viewport-ux` +
`spatial-index`). Agents: `web-preview-engineer`, `localization-engineer`,
`release-engineer`, `effects-engineer` (22-line wrappers owning the five
 skills above).
Wave 31 program: `gltf-animation` (baked-SmdFrames sampler emission +
linear TRS import + `--anim` contract + shell-only `msm2smd`
intermediate; extends `gltf-pipeline` + `quaternion-anim`),
`texture-cache` (FNV-1a content-hash SRV dedup + 256MB LRU + stats +
2 s TTL probe cache; extends `modern-textures` + `pbr-rendering`).
Agents: `anim-export-engineer`, `cache-engineer` (22-line wrappers
owning the two skills above).
Wave 32 program: `layout-system` (Rig/Paint/Anim/Review dock presets +
toolbar/overlay wrap + status priority + toast stacking + empty-state
centering + scroll-region policy + theme-token palette + tooltip
coverage; extends `viewport-ux` + `workspace-restore`),
`command-palette` (Ctrl+K fuzzy action table over existing handlers,
appOwnsKeyboard arbitration, no forks; extends `viewport-ux`).
Agents: `layout-engineer`, `palette-engineer` (22-line wrappers owning
the two skills above).
Wave 38 program (UI/UX restructure Waves 33-37): `ui-model` (core UI truth
 tables in m2rig_core — theme/style token floats, 4x14 layout presets, 19
 panel descriptors, dock plan, status/toast rules, view modes, 6 toolbar
 groups, fuzzy matcher, export gate, canonical labels; NO ImGui; extends
 `layout-system`), `theme-module` (exe-local `src/app/theme.cpp` maps token
 floats to ImVec4, `viewportClearF`/`heatmapU32` single source,
 `applyDarkTheme` moved out of main.cpp; extends `ui-model`),
 `panel-modules` (Wave 34 split: `panels.cpp` menu/dock orchestration + 8
 `panels_*.cpp` bodies + `panels_internal.hpp` cross-TU contract, local
 preset/label copies forbidden; extends `ui-model`), `dock-layout`
 (`buildDefaultDockLayout` split order incl. the full-width Timeline strip,
 both live Reset paths over `g_dockBuilt`, imgui.ini keep-until-Reset
 note, Wave 37 host reserve; extends `layout-system`), `toolbar-ux`
 (six-group Stage|Rig|View|Display|Status|Panels toolbar, ui_model-measured
 wrap, preset segmented row, danger Auto-rig, disabled honesty; extends
 `layout-system`), `component-kit` (`tipFor` disabled tooltips,
 `dangerButtonPush/Pop`, canonical Validate/X-ray labels,
 `exportActionEnabled` gates; extends `ui-model`), `status-chrome`
 (`kStatusBarHeight`/toast anchor/step constants, collapse predicates,
 sticky TTL rules, dockspace reserve coupling; extends `layout-system`),
 `prefs-persistence` (`user_prefs.json` save line + `extractBool` line for
 every UISettings bool, round-trip test pattern; extends
 `workspace-restore`), `ux-audit` (triage rule: reproduce with file:line,
 load-bearing areas untouched, prefer ui_model data fixes; extends
 `layout-system`), `app-state` (`src/app_state.cpp` App core: status/toast
 queues, undo/redo, bone selection sets, sample + SMD import/export,
 `resolveDrawPath`, preferences; extends `workspace-restore`),
 `msvc-warnings` (`/W4 /WX /FS` + C2220, `probe()`/ternary C4127
 discipline, one MSBuild per build dir, build->fix->focused->full gate
 loop; extends `build-ci`).
Agents: `ui-model-engineer`, `theme-engineer`, `panel-engineer`,
 `dock-engineer`, `toolbar-engineer`, `ui-kit-engineer`, `chrome-engineer`,
 `state-engineer`, `ux-reviewer`, `skill-curator`, `msvc-engineer`
 (22-line wrappers owning the eleven skills above).
Note: `*/SKILL.md` template files are placeholders, not finished skills,
until they carry a real contract + entry points + tests (see `rendering`).

## Conventions for editing skills
- Reference real paths (`D:\devapp\skinnermt2`, `Data/Models`,
  `noesis/Noesis.exe`, `tools/cli/main.cpp`) and real commands
  (`cmake --preset`, `ctest --preset`, `m2rig_cli ...`).
- Never `C:\rigapp`, `dotnet`, or `182 models` counts (unverified).
- Noesis GR2 import does not work (missing `granny2.dll`); document the
  grnreader98 primary path instead.

Legacy note: previous version described `C:\rigapp\DOCS` + `RigApp.exe`;
rewritten for this repo.
