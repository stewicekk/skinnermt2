# Skill: m2rig_cli reference

Headless batch tool: `build/release/Release/m2rig_cli.exe` (core only, no
GUI). Same import/validate/export core as the desktop app.

## Synopsis
```
m2rig_cli [--help | --version]
m2rig_cli validate <in.smd> [--profile <id>]
m2rig_cli validate-msm <in.msm>
m2rig_cli info <in.smd>
m2rig_cli smd2smd <in.smd> <out.smd>
m2rig_cli smd2msm <in.smd> <out.msm>
m2rig_cli fbx2smd <in.fbx> <out.smd>   (needs M2RIG_WITH_OPENFBX build)
```

## Exit codes
- `0` ok (validate: valid + export-ready)
- `1` usage error
- `2` IO/parse failure (`readTextFile` / `parseSmd` / `smdToAsset`)
- `3` validation failed / export blocked (`report.exportBlocked()`)
- `4` output write failure

## Examples
```powershell
.\build\release\Release\m2rig_cli.exe validate tests\data\two_bone.smd
.\build\release\Release\m2rig_cli.exe info tests\data\two_bone.smd
.\build\release\Release\m2rig_cli.exe smd2smd tests\data\two_bone.smd $env:TEMP\rt.smd
.\build\release\Release\m2rig_cli.exe smd2msm tests\data\two_bone.smd $env:TEMP\shape.msm
.\build\release\Release\m2rig_cli.exe validate $env:TEMP\rt.smd --profile pc_assassin_f
```

## Notes
- `smd2smd`/`smd2msm` run `repairMeshWeights` first and respect the export
  gate, byte-identical logic to Project -> Export in the GUI.
- GR2/FBX bridge conversion stays GUI-side (`App::importBridgedFile`);
  feed the CLI with SMD (convert first via grnreader98 / Noesis).
