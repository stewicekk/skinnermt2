---
name: spatial-index
description: KD-tree/BVH core for transfer, mirror, auto-rig and QEM LOD (Wave 26)
---

# spatial-index

LANDED Wave 26: one deterministic KD-tree core (`spatial.hpp/cpp`)
replacing brute-force loops in `transferWeightsKDTree`, `buildDonorTable`
and `findMirrorPairs` — exact top-k/radius matching brute force (up to
1-ulp boundary ties), NaN-safe, fully deterministic (no RNG/threads).
`autoRigMesh` deliberately stays brute-force: nearest bone SEGMENTS do
not reduce to point queries (proven counterexample), O(V*B) negligible.
Heat/geodesic auto-rig + QEM LOD still open (need topology solvers, not
just this index).

## Current truth (Wave 26 landed, verified)

- Transfer/donor/mirror run on the `KdTree` (`spatial.hpp/cpp`):
  `transferWeightsKDTree` + `buildDonorTable` query exact top-k,
  `findMirrorPairs` radius-searches the reflected point (same greedy
  rule); self-learn nested loop (`self_learning.cpp:394-471`) untouched.
- Mirror still O(n^2) worst-case (all-in-tolerance), one-way copy, `axis`
  param still ignored by `mirrorMeshWeights`; callers use defaults.
- Auto-rig nearest segment + `1/(d+eps)^2` (`:482-528`) deliberately
  brute-force (point-incompatible metric); 0x
  heat/diffusion/geodesic/voxel/biharmonic in production.
- LOD shortest-edge + full re-scan per collapse (`lod.cpp:87-108`);
  midpoint shrink; boundary erosion documented (`lod.hpp:1-15`);
  `(length,minId,maxId)` ordering; `lod_orphans` contract (`:130-180`).
- Self-train: `Mesh tmp` clone per candidate eval (`:743`),
  fixed order, <=6 candidates, `maxIter <=25`; two parallel transfer
  stacks (`skin_weights.cpp` vs `self_learning.cpp` with unguarded
  `stof/stoul`, `lastUpdated=0.0 // TODO`).

## Target contract (index landed; solvers open)

- DONE Wave 26: KD-tree core + transfer/donor/mirror wiring (exact
  parity, NaN-safe, deterministic). O(n log²n) build + O(log n) queries
  (not O(n log n) flat — build sorts per level).
- Voxel-geodesic + heat/biharmonic solver beside `autoRigMesh`,
  segment-distance as initializer; bone-visibility priors.
- QEM LOD (Garland-Heckbert + heap + attribute/weight-space penalty +
  boundary locks); keep tie-break idiom + `lod_orphans` + export-only design.
- Mirror: absolute + bbox-relative tolerance, KD pairing, bidirectional +
  repair+renorm, live `computeSymmetryError` readout (metric exists).
- Single transfer core long-term (Hungarian init -> L-BFGS/anneal refine).

## Entry points

- `transferWeightsKDTree/transferWeightsSelfTraining/findMirrorPairs/
  mirrorMeshWeights/autoRigMesh/decimateMesh`, `MeshTopology`,
  `computeSymmetryError`, `SelfLearningTransfer::*`.

## Test gate

- `transfer_determinism`, `mirror_tolerance_scaled`,
  `qem_no_boundary_erosion` (ninja 3208->1603 reference),
  `geodesic_no_bleed` fingers/legs synthetic, `lod_determinism` kept.

## Failure handling

Determinism is contractual (strict-less ordering, id tie-breaks).
`removedMass` never silent. `Mesh tmp`-clone cost must not regress
before the index lands — measure ninja/warrior_m wall time per PR.
