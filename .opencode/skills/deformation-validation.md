# Skill: Deformation Validation (proposal)

STATUS: proposal only — no BVH/penetration/stretch analyzer exists in the
repo. Current reality:

## What exists
- FK CPU skinning preview (`buildSkinningPalette`, `deformVertex`,
  timeline Play/FPS/Loop + Deform toggle); bind data never modified.
- Weight validation via CLI/GUI (`WEIGHTS_*` checks + export gate).
- Per-bone heatmap viewport (`Weights` view mode).

## If implemented (guidance)
1. Core-only math in `m2rig_core` (penetration = signed distance vs
   reference mesh; stretch = edge-length ratio vs bind).
2. Report through `ValidationReport` with new `MESH_*` ids; never block
   export on heuristics — warnings only.
3. Unit tests with synthetic fixtures in `tests/`, no model binaries.

Legacy note: C# `DeformationValidator`/`PenetrationDetector`/BVH code does
not exist here.
