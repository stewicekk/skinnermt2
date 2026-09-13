# Run Regression Tests

Full pipeline check (native):
```powershell
Set-Location D:\devapp\skinnermt2
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

Model inventory (real paths):
```powershell
(Get-ChildItem Data\Models -Recurse -Filter *.gr2).Count
(Get-ChildItem Data\Models -Recurse -Filter *.fbx).Count
(Get-ChildItem Data\Models -Recurse -Filter *.dds).Count
```

Verify binaries exist:
```powershell
Test-Path "build\release\Release\Metin2RiggingStudio.exe"
Test-Path "build\release\Release\m2rig_cli.exe"
Test-Path "build\release\Release\m2rig_tests.exe"
```

GR2 conversion spot check (grnreader98 ships its own `granny2.dll`):
```powershell
& 'Data\resources\Convert gr2 to mesh\grnreader98.v1.4.0.3.debug (1)\grnreader98.exe' 'Data\Models\warrior_m.gr2' -a
# writes Data\Models\warrior_m.gr2.smd next to input — delete after check
```

FBX conversion spot check (Noesis native FBX DLLs):
```powershell
.\noesis\Noesis.exe ?cmode "Data\Models\ninja.fbx" "$env:TEMP\ninja_check.smd"
```

Legacy note: `C:\rigapp` paths and `granny2.dll <model> -a` from the old
version of this file do not exist; use the commands above.
