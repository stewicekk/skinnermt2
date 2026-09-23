# Skill: Flood / Prune / Auto-rig

Group weight ops (`src/skin_weights.cpp`, `src/app_state.cpp`, Bone/Weights panels).

## Contract
- `floodBone` / `pruneBone`: core ops + App guards + Bone-panel buttons, undoable, lock-aware, mass-reported.
- `autoRigMesh`: nearest segment 1/(d+eps)^2 over topN, deterministic bone-id tie-break, repair to <=4; empty mesh/skeleton returns empty stats (App fails explicitly).
- `App::autoRigFromSkeleton`: empty guards, `pushUndoSnapshot("auto-rig")`, locked-bone restore from snapshot, `dirty+gpuDirty`, `runValidation`, status from `toDisplayString`.
- Weights-panel "Auto-rig from skeleton" button; CLI has no auto-rig (GUI-only, explicit).
- Tests: `flood_then_prune_roundtrip`, `flood_prune_respect_app_locks`, `autorig_binds_every_vertex_deterministically`.
