# Skill: Docs & Skills Index

## Docs (`docs/`)
- `AGENT_STATE.md` — wave log + architecture + test coverage (authoritative)
- `MASTER_PROMPT.md` — orchestration spec (agents, skills, waves)
- `REPOSITORY_AUDIT.md`, `IMPLEMENTATION_PLAN.md`, `USER_GUIDE.md`
- `DEPENDENCIES.md`, `CHANGELOG.md` — pins + release history

## Skills (`.opencode/skills/`, 33 files)
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

## Conventions for editing skills
- Reference real paths (`D:\devapp\skinnermt2`, `Data/Models`,
  `noesis/Noesis.exe`, `tools/cli/main.cpp`) and real commands
  (`cmake --preset`, `ctest --preset`, `m2rig_cli ...`).
- Never `C:\rigapp`, `dotnet`, or `182 models` counts (unverified).
- Noesis GR2 import does not work (missing `granny2.dll`); document the
  grnreader98 primary path instead.

Legacy note: previous version described `C:\rigapp\DOCS` + `RigApp.exe`;
rewritten for this repo.
