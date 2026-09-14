# Skill: Armor Transfer Pipeline

End-to-end armor re-skinning between characters (native pipeline).

## Steps
1. Load source (skinned): Project -> Import SMD... or Import FBX/GR2
   (bridge) (`App::importSmdFile` / `App::importBridgedFile`).
2. Load target (static or differently skinned) the same way.
3. Weights panel -> Transfer from... -> pick source asset:
   - Quick: `App::transferWeightsFrom` (name-matched kNN transfer).
   - Self-train: `App::transferWeightsSelfTrained` (remap optimizer with
     per-bone confidence + status report).
4. Locked bones are preserved automatically (`App::lockedBones`).
5. Validate (export gate), then Export SMD/MSM or batch-export all.

## Implementation map
- Math: `src/skin_weights.cpp` (`transferWeightsKDTree`,
  `transferWeightsSelfTraining`); remap: `src/profiles.cpp`
  (`mapBonesByProfile`, `mirrorBoneName`).
- Bridges: `src/adapters/gr2_adapter.cpp` (grnreader98 primary,
  `Noesis.exe ?cmode` fallback); never native GR2 emit.
- Batch: `App::exportAllBatch` + `m2rig_cli smd2smd/smd2msm` loops.

Legacy note: C# `ArmorTransferPipeline.cs` / `Gr2Exporter` / `production_pipeline.py`
do not exist here.
