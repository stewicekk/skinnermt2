# Metin2 Rigging Studio

Full-stack Metin2 armor skinning workspace: native C++ desktop app with Dear ImGui + D3D11 viewport, React/Three.js web frontend, FastAPI AI backend, and full SMD/MSM/GR2 export pipeline.

## Architecture

| Layer | Tech | Role |
|-------|------|------|
| Native Core | C++20, zero-dep | SMD parser, weight engine, kNN transfer, MSM export, repair pipeline |
| Desktop UI | Win32 + D3D11 + ImGui docking | Viewport, weight paint, bone tree, timeline, export panels |
| Web Frontend | React 18, Three.js, Zustand, Tailwind | Interactive 3D viewport, weight painting, AI transfer |
| AI Backend | FastAPI + PyTorch + SciPy | KD-tree weight transfer, neural weight prediction, MSM generation |
| Collaboration | WebSocket (collab_server.py) | Real-time multi-user editing |

## Quick Start

### Native desktop app (primary)
```powershell
cd D:\devapp\skinnermt2
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\debug\Debug\Metin2RiggingStudio.exe
```

### Web frontend
```powershell
cd frontend
npm install
npm run dev
```
Open `http://localhost:5173`.

### AI backend
```powershell
cd ai_backend
pip install -r requirements.txt
python server.py
```
API on `http://localhost:8000`.

## Project Structure

```
├── CMakeLists.txt              # Native build (m2rig_core + GUI + CLI + tests)
├── include/m2rig/              # Public C++ API (18 headers)
├── src/                        # Core implementations (18 .cpp files)
├── src/app/                    # Desktop UI (main.cpp, panels.cpp, renderer.cpp)
├── tools/cli/                  # Headless CLI (validate/info/smd2smd/smd2msm)
├── tests/                      # CTest harness (8 test files, 35+ checks)
├── frontend/                   # React web app (Vite + TypeScript)
├── ai_backend/                 # FastAPI server + PyTorch models
├── modules/                    # 3ds Max MaxScript plugins (28 .ms files)
├── core/                       # Core MaxScript rigging tools (7 .ms files)
├── docs/                       # Documentation
├── Data/                       # Reference models (binary .gr2/.fbx ignored in git)
└── scripts/                    # Build scripts
```

## Key Features

- SMD 1.0 import/export with full round-trip
- Weight painting (Add/Subtract/Smooth/Normalize/Blur/Sharpen)
- kNN weight transfer with bone remapping
- AST-based MSM parser/exporter
- AI neural weight prediction (PyTorch)
- GR2 bridge via Noesis/Granny
- Workspace persistence (.m2rig JSON)
- Pre-export validation with auto-repair
- Animation timeline with CPU skinning preview
- ImGuizmo bone manipulators (Translate/Rotate)

## Documentation

- `docs/USER_GUIDE.md` — Detailed user guide
- `docs/AGENT_STATE.md` — Agent state & completed waves
- `docs/IMPLEMENTATION_PLAN.md` — Roadmap (waves 9+)
- `docs/REPOSITORY_AUDIT.md` — Repository audit
- `docs/ARCHITECTURE.md` — Architecture overview
- `docs/API.md` — API reference

## Test Status

35/35 checks + 4 CLI suites green (ctest). Release build clean.

## License

Project-specific. See individual source files.
