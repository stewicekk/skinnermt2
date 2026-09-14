# Skill: Pipeline Orchestrator (proposal)

STATUS: proposal only — no pipeline engine exists. The real workflow is
manual GUI steps + `m2rig_cli` batch commands + `.m2rig` workspaces.

## Current equivalents
- Single model: GUI Project menu (Import SMD / Import FBX-GR2 / Export
  SMD / Export MSM / Export GR2-bridge / Save-Load workspace).
- Batch: Export panel "Export all loaded (SMD+MSM)..." with per-asset
  OK/FAIL table (`App::exportAllBatch`), or CLI loops:
  `m2rig_cli validate/smd2smd/smd2msm` (see `cli-reference`).
- State: `.m2rig` JSON (`saveCurrentWorkspace`/`restoreWorkspace`,
  incl. locked bones by name).

## Proposal (if implemented)
Dependency-graph batch (`GR2 -> SMD -> validated -> MSM`) as a CLI-driven
queue with a progress table — extend `exportAllBatch`, not a new engine.

Legacy note: C# `PipelineGraph.xaml` / `PipelineEngine` / `ExportGr2` do
not exist here.
