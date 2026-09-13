# Build the native app (SUPERSEDES the old C# WPF instructions below)

Build the C++ desktop + CLI (VS 2022 x64):
```powershell
Set-Location D:\devapp\skinnermt2
cmake --preset windows-release
cmake --build --preset windows-release
```

Binaries land in `build/release/Release/`:
- `Metin2RiggingStudio.exe` — ImGui + D3D11 workstation
- `m2rig_cli.exe` — headless batch tool
- `m2rig_tests.exe` — test harness

Verify with `ctest --preset windows-release --output-on-failure`
(5 suites: `m2rig_tests`, `cli-validate`, `cli-info`, `cli-smd2smd`,
`cli-smd2msm`).

---
Legacy note: this file previously described `dotnet build C:\rigapp` for a
C# WPF prototype. That project does not exist in this repo; the native
`Metin2RiggingStudio` above is the maintained application.
