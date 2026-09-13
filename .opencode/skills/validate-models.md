# Validate Models (native CLI)

Verify integrity of SMD models with the headless tool
(`build/release/Release/m2rig_cli.exe`).

## Quick Check
```powershell
.\build\release\Release\m2rig_cli.exe validate tests\data\two_bone.smd
# exit 0 = valid + export-ready; 3 = export blocked (see report)
```

## Any model file
```powershell
.\build\release\Release\m2rig_cli.exe validate Data\Models\warrior_m.gr2.smd --profile pc_warrior_m
.\build\release\Release\m2rig_cli.exe info <model.smd>
```

## Weight Check (same rules as the GUI export gate)
- max 4 bone influences per vertex (`WEIGHTS_OVER_LIMIT` = error)
- weights sum to 1.0 (`WEIGHTS_UNNORMALIZED` = warning, tolerance 1e-2)
- no NaN/negative (`WEIGHTS_INVALID` = error), no zero-weight verts
- bone indices in range (`WEIGHTS_BAD_BONE` = error)
- expected Bip01 core present per `--profile` (23 bones; gender variants
  `pc_{warrior,assassin,sura,shaman}_{m,f}`, `pc_wolfman`, `pc_mount`)
- socket deform use (`equip_*`/`stip`/`saddle` carrying weights = warning)

## Batch over a folder
```powershell
Get-ChildItem Data\Models -Filter *.smd | ForEach-Object {
  & .\build\release\Release\m2rig_cli.exe validate $_.FullName --profile pc_warrior
  if ($LASTEXITCODE -ne 0) { Write-Host ("FAIL: " + $_.Name) }
}
```

Legacy note: `C:\rigapp\models` (182 models) and `crossref_gr2_smd.py` do not
exist here; `Data/Models` holds 31 GR2 + FBX pairs + ~80 DDS (0 SMD/MSM —
generate via grnreader98/Noesis bridges or `m2rig_cli smd2smd`).
