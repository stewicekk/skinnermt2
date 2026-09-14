# Skill: Metin2 Armor System

## Verified facts (not the old estimates)
- GR2 container: V1 `gr2\0`, real V2 `29 DE ..` (all 31 `Data/Models` GR2
  are `29de6cc0`, zero ASCII `Bip01`) — checked by
  `isValidGr2Container` (`src/adapters/gr2_adapter.cpp`).
- Convert: `grnreader98.exe <model.gr2> -a` (ships `granny2.dll` in
  `Data/resources/Convert gr2 to mesh/`); FBX via
  `noesis/Noesis.exe ?cmode <in.fbx> <out.smd>`.
- Skeleton: 23-bone Bip01 core (`src/profiles.cpp`, `src/samples.cpp`) +
  verified optionals (Spine2, fingers, toes, ponytail, armor sockets);
  gender profiles `pc_{warrior,assassin,sura,shaman}_{m,f}` + `pc_wolfman`
  + `pc_mount`.
- Weights: max 4 influences, sum 1.0, mass reported (`RepairStats`).

## Pipeline
GR2/FBX -> bridge SMD -> `m2rig_cli validate` -> paint/transfer/mirror in
GUI -> export gate -> SMD/MSM (`m2rig_cli smd2smd/smd2msm`, batch via
`App::exportAllBatch`).

Legacy note: previous magic numbers (`0xC06CDE29`/v456), 21 sets / 182
models, and `trainer.py` references were unverified and are removed.
