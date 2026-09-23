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
m2rig_cli autorig <in.smd> <out.smd>
m2rig_cli lod <in.smd> <out.smd> [--ratio <0..1>]
m2rig_cli fbx2smd <in.fbx> <out.smd>   (needs M2RIG_WITH_OPENFBX build)
m2rig_cli gr22smd <in.gr2> <out.smd>   (needs grnreader98, GUI-parity path)
m2rig_cli orient <in.smd>   (transform diagnostics, exit 0 sane / 3 blocking)
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
.\build\release\Release\m2rig_cli.exe autorig tests\data\two_bone.smd $env:TEMP\ar.smd
.\build\release\Release\m2rig_cli.exe lod tests\data\two_bone.smd $env:TEMP\lod.smd --ratio 0.5
.\build\release\Release\m2rig_cli.exe validate $env:TEMP\rt.smd --profile pc_assassin_f
```

## Notes
- `smd2smd`/`smd2msm` run `repairMeshWeights` first and respect the export
  gate, byte-identical logic to Project -> Export in the GUI.
- `autorig` runs the same deterministic `autoRigMesh` bind as the GUI
  "Auto-rig from skeleton" button, then the same repair + gate; covered
  by the `cli-autorig` ctest suite.
- `lod` decimates a copy via deterministic `decimateMesh` (union + repair
  of merged influences, submesh rebuild), then repair + gate; GUI twin is
  `Export LOD SMD...` with a keep-ratio slider; covered by `cli-lod`.
- GR2 bridge conversion has a headless twin: `gr22smd` runs the same
  grnreader98 -> profile-conversion -> repair -> gate chain as
  `App::importBridgedFile` (needs grnreader98 under
  `Data/resources/Convert gr2 to mesh/`); FBX-via-Noesis stays GUI-side.
- `orient` runs `diagnoseOrientation` (head/feet order, mesh overlap,
  scale advisory, rigid-bind joint distance); exit 3 on blocking
  findings. Run after every GR2/FBX import before editing.
