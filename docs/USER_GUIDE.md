# Metin2 Rigging Studio — User Guide

## Overview

Professional Metin2 armor skinning workstation. Native C++ desktop app (primary) with optional React web frontend and FastAPI AI backend.

## Quick Start

### Native Desktop (Recommended)
```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\debug\Debug\Metin2RiggingStudio.exe
```

### Web Frontend
```powershell
cd frontend && npm install && npm run dev
```

### AI Backend
```powershell
cd ai_backend && pip install -r requirements.txt && python server.py
```

## Core Workflow

1. **Load model** — Import SMD via File menu or drag-drop
2. **Inspect skeleton** — Bone tree in left panel, click to select
3. **Paint weights** — Brush tools (Add/Subtract/Smooth/Normalize), adjust radius/strength
4. **Mirror** — X/Y/Z symmetry axis toggle
5. **Transfer** — Reference-based kNN weight transfer or AI neural transfer
6. **Validate** — Pre-export check (zero-weight verts, bone limit, normalization)
7. **Export** — SMD / MSM / GR2 (via bridge)

## Brush Tools

| Tool | Action |
|------|--------|
| Add | Increase weight for selected bone |
| Subtract | Decrease weight |
| Smooth | Blend adjacent weights |
| Normalize | Force sum = 1.0 per vertex |

## View Modes

- **Solid** — Lit rendering
- **Wireframe** — Edge view
- **Heatmap** — Weight intensity (blue→red) per selected bone

## Export Formats

- **SMD** — Half-Life skeletal mesh (ASCII)
- **MSM** — Metin2 Script Mesh (client-side loading)
- **GR2** — Granny 3D (requires configured compiler via `GRANNY_COMPILER` env var)

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| H | Toggle sub-mesh visibility |
| Alt+H | Restore all sub-meshes |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |

## AI Weight Transfer

1. Upload reference SMD (already skinned)
2. Upload target SMD (unskinned)
3. Backend uses KD-tree for vertex correspondence
4. Weights transferred by topological proximity
5. Result appears in viewport instantly

## Neural Weight Prediction

- Model: SimpleWeightNet (3→128→128→N bones)
- Training: `python train_pipeline.py --data /path/to/dataset`
- Inference: POST `/ai_skin` with vertices + nbones

## Workspace

Projects auto-save to `.m2rig` JSON files. Sessions restore on restart.

## Validation Rules

- ≤4 bone influences per vertex
- Weights sum ≈ 1.0 (tolerance 1e-2)
- No NaN/Infinity
- Bone count ≤ 256 (Metin2 limit)
- Socket bones (equip_left/right, stip) protected

## Known Limitations

- GR2 native parsing not implemented (bridge only)
- Animation preview is skeleton-only (GPU skinning future)
- Dock layout rebuilt each launch (workspace persists state, not layout)
