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
