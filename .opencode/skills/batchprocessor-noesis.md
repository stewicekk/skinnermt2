# Skill: Noesis Batch Processing

Batch-convert FBX (and attempted GR2) via the Noesis bridge.

## Reality check
- Primary GR2 path is **grnreader98**, not Noesis:
  `grnreader98.exe <model.gr2> -a` (ships `granny2.dll`; writes
  `<input>.smd` next to input — stage copies in temp, see
  `convertGr2ToSmdViaGrnReader`).
- Noesis path is FBX-first: `noesis/Noesis.exe ?cmode <in.fbx> <out.smd>`
  (native FBX DLLs present). Noesis GR2 plugins require `granny2.dll`,
  which is absent from `noesis/` — expect "Unknown file type".
- Code: `src/adapters/gr2_adapter.cpp`, `src/extractors/
  universal_weight_extractor.cpp`, `App::importBridgedFile`.
- All bridge runs capture stdout/stderr into the Console panel
  (`adapters/bridge_process`); timeouts terminate the child process.

## Batch loop (PowerShell)
```powershell
Get-ChildItem Data\Models -Filter *.fbx | ForEach-Object {
  .\noesis\Noesis.exe ?cmode $_.FullName ($env:TEMP + "\" + $_.BaseName + ".smd")
  .\build\release\Release\m2rig_cli.exe validate ($env:TEMP + "\" + $_.BaseName + ".smd")
}
```

Legacy note: C# `BatchProcessor.cs` / `NoesisIntegration.ConvertGr2ToFbx`
do not exist here.
