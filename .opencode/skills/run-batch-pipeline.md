# Skill: Batch Model Pipeline (native loop)

Scriptable batch over model files with the headless CLI.

## Loop template (PowerShell)
```powershell
Get-ChildItem Data\Models -Filter *.smd | ForEach-Object {
  & .\build\release\Release\m2rig_cli.exe validate $_.FullName --profile pc_warrior
  if ($LASTEXITCODE -ne 0) { Write-Host ("FAIL: " + $_.Name) }
}
```

## Convert + validate + export
```powershell
# FBX -> SMD (Noesis), then validate + export
.\noesis\Noesis.exe ?cmode "Data\Models\ninja.fbx" "$env:TEMP\ninja.smd"
.\build\release\Release\m2rig_cli.exe validate "$env:TEMP\ninja.smd" --profile pc_assassin_m
.\build\release\Release\m2rig_cli.exe smd2smd "$env:TEMP\ninja.smd" "$env:TEMP\ninja_out.smd"
.\build\release\Release\m2rig_cli.exe smd2msm "$env:TEMP\ninja.smd" "$env:TEMP\ninja.msm"
# GR2 -> SMD (grnreader98, ships granny2.dll):
& 'Data\resources\Convert gr2 to mesh\grnreader98.v1.4.0.3.debug (1)\grnreader98.exe' 'Data\Models\warrior_m.gr2' -a
```

## Exit codes
`0` ok · `1` usage · `2` IO/parse · `3` validation/export-blocked ·
`4` write failure (see `cli-reference`).

Legacy note: `RigApp.exe Batch Pro` / `production_pipeline.py` / 182 GR2
do not exist here.
