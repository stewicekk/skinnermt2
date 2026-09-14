# Skill: Auto Bone Binding (proposal)

Automatic initial skinning for unrigged meshes. STATUS: not implemented —
kNN transfer (`transferWeightsKDTree`) is the current mechanism.

## Proposal (if implemented)
1. New core API in `m2rig_core` (no GPU/ML deps): bind each vertex to the
   nearest bones by distance to bone segments, keep top-4 + normalize +
   `RepairStats`.
2. CLI subcommand (e.g. `m2rig_cli autobind <in.smd>`) + unit tests in
   `tests/test_weights.cpp`.
3. ImGui panel section in `drawWeightPanel`, undoable via
   `App::pushUndoSnapshot`, respecting `lockedBones`.

## Rules
- Max 4 influences, mass reported, validation gate respected.
- No tetrahedral/TetGen/GPU dependencies in core.

Legacy note: C# voxel/tetrahedral binders do not exist here.
