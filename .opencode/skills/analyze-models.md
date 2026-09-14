# Skill: Analyze Models

Inventory and sanity-check the model library.

## Real paths (not `C:\rigapp\models`)
- `Data/Models/` — 31 GR2 + FBX pairs + ~80 DDS (0 SMD/MSM; generate via bridges)
- `tests/data/two_bone.smd`, `tests/data/sample.msm` — offline fixtures
- `tests/ninja_excerpt.inc` — real 90-bone Noesis SMD excerpt (offline)

## Commands
```powershell
(Get-ChildItem Data\Models -Recurse -Filter *.gr2).Count
(Get-ChildItem Data\Models -Recurse -Filter *.fbx).Count
.\build\release\Release\m2rig_cli.exe validate tests\data\two_bone.smd
.\build\release\Release\m2rig_cli.exe info tests\data\two_bone.smd
```

## Per-model deep check
- GR2 container: `isValidGr2Container` (V1 `gr2\0`, real V2 `29 DE ..`)
- Convert: `grnreader98.exe <model.gr2> -a` (ships `granny2.dll`), or
  `noesis/Noesis.exe ?cmode <model.fbx> <out.smd>` for FBX
  (Noesis GR2 plugins need `granny2.dll`, absent in `noesis/`)
- Validate output: `m2rig_cli validate <out.smd> --profile pc_<race>_<m|f>`
