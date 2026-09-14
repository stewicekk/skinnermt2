# Metin2 Rigging Studio

Native C++20 Metin2 armor skinning workstation: dependency-free core, DirectX 11 + Dear ImGui desktop app, headless CLI, and full SMD/MSM/FBX/GR2-bridge pipeline. (Legacy React prototype lives in `_archive/`; the Python `ai_backend/` is an experimental reference, not required.)

## Architecture

| Layer | Tech | Role |
|-------|------|------|
| Native Core | C++20, zero-dep | SMD parser, weight engine, kNN + self-training transfer, MSM export, repair pipeline |
| Desktop UI | Win32 + D3D11 + ImGui docking + ImGuizmo | Viewport, weight paint, bone tree, timeline, export panels |
| Headless CLI | `m2rig_cli` | validate/info/smd2smd/smd2msm/fbx2smd, exit codes 0-4 |
| Bridges | grnreader98 (GR2), Noesis (FBX) | External converters with log capture, never native GR2 emit |

## Quick Start

### Native desktop app (primary, release)
```powershell
cd D:\devapp\skinnermt2
cmake --preset windows-release
cmake --build --preset windows-release
.\build\release\Release\Metin2RiggingStudio.exe
```

### CLI quick check
```powershell
.\build\release\Release\m2rig_cli.exe validate tests\data\two_bone.smd
ctest --preset windows-release --output-on-failure
```

## Project Structure

```
├── CMakeLists.txt              # Native build (core + GUI + CLI + tests + FBX + gizmo)
├── include/m2rig/              # Public C++ API (24 headers)
├── src/                        # Core implementations (24 .cpp files)
├── src/app/                    # Desktop UI (main.cpp, panels.cpp, app_gpu.cpp)
├── tools/cli/                  # Headless CLI (validate/info/smd2smd/smd2msm/fbx2smd)
├── tests/                      # CTest harness (8 test files, 53 checks)
├── _archive/frontend/          # Legacy React prototype (reference only)
├── ai_backend/                 # Experimental Python reference (not required)
├── modules/                    # 3ds Max MaxScript plugins (34 .ms files)
├── core/                       # Core MaxScript rigging tools (7 .ms files)
├── docs/                       # Documentation
├── Data/                       # Reference models (binary .gr2/.fbx ignored in git)
└── scripts/                    # Build scripts
```

## Key Features

- SMD 1.0 import/export with full round-trip + cross-reference validation
- Native FBX import (OpenFBX, 90-bone parity verified)
- Weight painting (Add/Subtract/Set/Smooth/Blur/Sharpen/Normalize/Prune/Flood) + bone locking
- kNN + self-training weight transfer with bone remapping
- AST-based MSM parser/exporter + inspector panel
- GR2 via grnreader98/Noesis bridges (log capture, honest NOT_SUPPORTED_DIRECTLY)
- Textured D3D11 viewport (DDS DXT1/3/5) with 7 view modes, X-ray, ImGuizmo trio
- Workspace persistence (.m2rig JSON) + autosave with crash recovery
- Pre-export validation with auto-repair + batch export
- Animation timeline with CPU skinning preview + transport controls

## Documentation

- `docs/USER_GUIDE.md` — Detailed user guide
- `docs/AGENT_STATE.md` — Agent state & completed waves (authoritative status)
- `docs/MASTER_PROMPT.md` — Orchestration spec (agents, skills, waves)
- `docs/IMPLEMENTATION_PLAN.md` — Original roadmap (partially superseded by AGENT_STATE)
- `docs/REPOSITORY_AUDIT.md` — Repository audit snapshot
- `docs/DEPENDENCIES.md` — Pins, tools, licenses
- `docs/CHANGELOG.md` — Release history

## Test Status

53/53 checks + 6 CLI suites green (`ctest --preset windows-release`). Release build clean (`/W4 /WX`).

## License

Project-specific. See individual source files.
