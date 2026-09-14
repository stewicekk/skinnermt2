# Skill: Add Transfer Algorithm

Add a new vertex-weight transfer algorithm to `m2rig_core`.

## Current algorithms (`src/skin_weights.cpp`)
- `transferWeightsKDTree(src, dst, boneRemap, k)` — k-nearest inverse-distance
  blend + `repairVertexInfluences` to 4 influences.
- `transferWeightsSelfTraining(...)` — deterministic coordinate descent over
  the bone remap minimizing symmetry error + unmapped rate + distance + mass.
- Bone remap source: `mapBonesByProfile` (`src/profiles.cpp`), exact
  canonical-name match; locked destination bones are excluded.

## Rules for a new algorithm
1. Core only (`include/m2rig/skin_weights.hpp` + `src/skin_weights.cpp`);
   no D3D/ImGui/ML dependencies.
2. Always finish with `repairVertexInfluences(..., kMetin2MaxInfluences)` and
   accumulate `RepairStats` (mass loss is reported, never silent).
3. Respect `lockedBones` (see `App::transferWeightsFrom` guards).
4. Add unit tests in `tests/test_weights.cpp` + a `Weights:` quality check
   via `computeWeightQuality`.
5. Wire UI in `drawWeightPanel` (`src/app/panels.cpp`, "Transfer from..."
   popup: Quick vs Self-train pattern).

Legacy note: this file previously described C# `Metin2Core.cs` /
`BatchProcessor.cs`; those do not exist here.
