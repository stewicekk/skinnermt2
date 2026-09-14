# Skill: Weight Model Training (proposal)

STATUS: proposal only — no PyTorch/dataset code exists in this repo.
Weighting is deterministic algorithms, not learned models.

## Current mechanisms (`src/skin_weights.cpp`)
- `transferWeightsKDTree` — kNN inverse-distance blend + repair.
- `transferWeightsSelfTraining` — coordinate descent over the bone remap
  minimizing symmetry error + unmapped rate + distance + removed mass,
  with per-bone confidence (`SelfTrainStats`).
- Brush painting with falloff curves + `RepairStats` mass accounting.

## Proposal (if implemented)
Any learning approach must: train OFFLINE (never a core dependency),
export a static bone-remap/confidence table, and validate through the
existing `m2rig_cli validate` gate + `tests/test_weights.cpp`.

Legacy note: `training/smd_extracted`, 182 NPZ files, `trainer.py`,
`.pt` checkpoints and torch do not exist here.
