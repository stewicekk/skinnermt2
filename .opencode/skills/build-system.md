# Skill: Build System

## Description
Configure and verify the complete native C++ build pipeline (no .NET; this
repo is CMake + MSVC, see `CMakeLists.txt`).

## Targets (from `CMakeLists.txt`)
- `m2rig_core` — dependency-free static core library
- `Metin2RiggingStudio` — Dear ImGui + DirectX 11 desktop app (WIN32)
- `m2rig_cli` — headless batch tool (core only, see `tools/cli/main.cpp`)
- `m2rig_tests` — dependency-free CTest harness
- `imgui_lib`, `imguizmo_lib` — third-party, `/W3 /WX-` (warnings never fail)

## Presets (`CMakePresets.json`, VS 2022 x64, C++20)
- `windows-debug` -> `build/debug` (Debug)
- `windows-release` -> `build/release` (Release)

## Commands
```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
# or all-in-one:
powershell -ExecutionPolicy Bypass -File scripts/build-windows.ps1 -Preset windows-release
```

## Rules
- First-party code is `/W4 /WX` (`M2RIG_WARNINGS_AS_ERRORS=ON`): any warning
  fails the build. Fix the warning, never lower the level for first-party code.
- Always `/FS` (PDB sharing); never run two MSBuilds on the same build dir.
- FetchContent needs network at configure time (ImGui + ImGuizmo pins in
  `CMakeLists.txt`); `M2RIG_WITH_GIZMO=OFF` drops the ImGuizmo dependency.
- `pwsh.exe not recognized` after link steps is harmless (exit code 0).
