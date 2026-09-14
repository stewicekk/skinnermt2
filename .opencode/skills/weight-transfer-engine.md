# Skill: Weight Transfer Engine (kNN)

Native transfer (`src/skin_weights.cpp`): `transferWeightsKDTree` blends
the k-nearest source vertices by inverse distance, remaps bones by exact
canonical name (`mapBonesByProfile`), then repairs to max 4 influences
with mass reporting. `transferWeightsSelfTraining` additionally optimizes
the remap (symmetry + unmapped + distance + mass cost).

## Use
- GUI: Weights panel -> Transfer from... -> source asset (Quick or
  Self-train); locked destination bones are preserved; undoable.
- Sample templates ("Sample as...", 14 race/gender profiles) provide
  transfer sources/targets.

## Limits (honest scope)
kNN + coordinate descent only. No Heat/BBW/Geodesic-Voxel solvers, no GPU
compute shaders — those are proposals, not code.

Legacy note: C# `WeightTransferEngine` does not exist here.
