# Metin2 Armor Skinning & Rigging Studio - Complete Guide

## Table of Contents
1. [Overview](#overview)
2. [Installation & Setup](#installation--setup)
3. [Project Structure](#project-structure)
4. [Quick Start](#quick-start)
5. [Using the Application](#using-the-application)
6. [Training the AI Model](#training-the-ai-model)
7. [Weight Painting Tutorial](#weight-painting-tutorial)
8. [Export Pipeline](#export-pipeline)
9. [Advanced Features](#advanced-features)
10. [Troubleshooting](#troubleshooting)

---

## Overview

The Metin2 Armor Skinning & Rigging Studio is a full-stack web application for creating and editing armor skins for the Metin2 MMORPG. It provides:

- **3D Viewport** with interactive orbit camera and wireframe/solid rendering
- **Weight Painting** with brush tools (Add, Subtract, Smooth, Normalize)
- **AI-Assisted Weight Transfer** from reference armor models
- **SMD/GR2/MSM Export** for direct Metin2 client compatibility
- **Animation Preview** with skeletal animation playback
- **Full Undo/Redo** system for non-destructive editing

---

## Installation & Setup

### Prerequisites
- Node.js 18+ (for frontend)
- Python 3.11+ (for backend)
- npm or yarn

### Frontend Setup

```bash
cd frontend
npm install --legacy-peer-deps
npm run dev
```

The frontend runs on `http://localhost:5173`

### Backend Setup

```bash
cd ai_backend
pip install -r requirements.txt
python server.py
```

The backend API runs on `http://localhost:8000`

### Docker Setup (Optional)

```bash
docker-compose up -d
```

---

## Project Structure

```
metin2-skinning-studio/
├── frontend/                    # React + Three.js + Zustand
│   ├── src/
│   │   ├── components/         # UI components (BoneTree, ToastContainer, ErrorBoundary)
│   │   ├── canvas/            # Three.js scene setup (Scene.tsx)
│   │   ├── stores/            # Zustand state management (useRiggingStore.ts)
│   │   ├── lib/               # Core libraries:
│   │   │   ├── smdExporter.ts    # SMD export module
│   │   │   ├── msmGenerator.ts   # MSM script generator
│   │   │   ├── heatmap.ts        # Weight heatmap visualization
│   │   │   ├── undoRedo.ts       # Command pattern undo/redo
│   │   │   ├── autoSave.ts       # IndexedDB auto-save
│   │   │   ├── validation.ts     # Pre-export validation
│   │   │   ├── animationPlayer.ts# Animation playback
│   │   │   ├── boneLocking.ts    # Bone weight locking
│   │   │   ├── meshIsolation.ts  # Hide/unhide sub-meshes
│   │   │   └── weightUtils.ts    # Weight normalization math
│   │   ├── api/               # API client (placeholder)
│   │   └── App.tsx            # Main application component
│   ├── package.json
│   ├── tsconfig.json
│   └── vite.config.ts
│
├── ai_backend/                  # FastAPI + PyTorch + SciPy
│   ├── server.py              # API routes
│   ├── model.py               # PyTorch weight prediction model
│   ├── kd_tree.py             # KD-Tree implementation
│   ├── bone_mapper.py         # Bone mapping logic
│   ├── bone_mapper_train.py   # Model training script
│   ├── train_pipeline.py      # Training pipeline
│   ├── inference_client.py    # Client for AI inference
│   ├── export_model.py        # TorchScript model export
│   └── requirements.txt
│
├── core/                      # Core tools (MilkShape scripts)
├── modules/                   # Additional modules
├── docs/                      # Documentation
└── tools/                     # Utility scripts
```

---

## Quick Start

1. **Start the backend:**
   ```bash
   cd ai_backend && python server.py
   ```

2. **Start the frontend:**
   ```bash
   cd frontend && npm run dev
   ```

3. **Open browser:** Navigate to `http://localhost:5173`

4. **Import a model:** Click "Upload Model" or drag-and-drop an `.smd` or `.gr2` file

5. **Select a bone:** Click on a bone in the left hierarchy panel

6. **Paint weights:** Use the brush tools in the right panel to paint weights

7. **Export:** Click the export buttons to generate `.smd`, `.gr2`, or `.msm` files

---

## Using the Application

### UI Layout

- **Left Panel (Bone Hierarchy):** Shows the skeleton tree with lock/hide controls
- **Center (3D Viewport):** Interactive Three.js canvas with orbit controls
- **Right Panel (Tools):** Brush settings, symmetry, AI tools, export options
- **Bottom Bar:** Status log and animation timeline

### Viewport Controls

| Action | Shortcut |
|--------|----------|
| Rotate | Left mouse drag |
| Zoom | Scroll wheel |
| Pan | Right mouse drag |
| Hide mesh | H key |
| Show all | Alt+H |
| Undo | Ctrl+Z |
| Redo | Ctrl+Y |

### Brush Tools

1. **Add:** Increases weight for selected bone (red in heatmap)
2. **Subtract:** Decreases weight for selected bone (blue in heatmap)
3. **Smooth:** Blends weights between adjacent vertices
4. **Normalize:** Ensures all weights sum to 1.0

### Brush Settings

- **Radius:** Controls the brush size (0.1 - 5.0)
- **Strength:** Controls how much weight is added/removed per stroke (0.1 - 1.0)
- **Symmetry:** Mirror weights across X axis (left/right bones)

---

## Training the AI Model

The AI model uses PyTorch to predict per-vertex weights based on vertex position. Training requires a dataset of reference armors with known weights.

### Dataset Preparation

1. **Collect reference armors:** At least 10-20 already-skinned armor pieces per class
2. **Extract vertex data:** Use the `/ai_skin` endpoint to get vertex positions and weights
3. **Format as JSON:**
   ```json
   {
     "vertices": [[x, y, z], ...],
     "weights": [[bone_id, weight], ...],
     "bones": ["bip01 pelvis", "bip01 spine", ...]
   }
   ```

### Training Pipeline

```bash
cd ai_backend
python train_pipeline.py --data /path/to/dataset --epochs 100 --output model.pt
```

### Training Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| --data | Required | Path to training dataset |
| --epochs | 100 | Number of training epochs |
| --hidden | 128 | Hidden layer size |
| --batch-size | 32 | Training batch size |
| --learning-rate | 0.001 | Optimizer learning rate |
| --output | model.pt | Output model path |

### Model Architecture

```
SimpleWeightNet:
  Input: 3 (x, y, z position)
  Hidden: 128 neurons (ReLU)
  Hidden: 128 neurons (ReLU)
  Output: N bones (softmax)
```

### Inference

Once trained, use the model for weight prediction:

```bash
python server.py --model model.pt
```

Or via API:
```bash
curl -X POST http://localhost:8000/ai_skin \
  -H "Content-Type: application/json" \
  -d '{"vertices": [[0,0,0], ...], "nbones": 32}'
```

---

## Weight Painting Tutorial

### Basic Weight Painting

1. **Select a bone** from the left hierarchy panel
2. **Choose brush mode** (Add/Subtract/Smooth/Normalize)
3. **Adjust brush settings** (radius, strength)
4. **Paint on the model** by clicking and dragging in the viewport
5. **Watch the heatmap** update in real-time (red = full weight, blue = zero)

### Using Symmetry Mode

1. Enable **Symmetry** in the right panel
2. Select **X axis** for left-right mirroring
3. Paint on the left side - weights automatically mirror to right side
4. Useful for symmetric armor pieces (chest, shoulders, legs)

### Bone Weight Locking

1. Click the **lock icon** next to a bone in the hierarchy
2. Locked bones (🔒) cannot have their weights modified
3. Protects critical bones like `Bip01 Pelvis`, `Bip01 Head`, `equip_right`
4. Essential during AI retargeting to preserve socket positions

### Mesh Isolation

1. Press **H** to hide the current sub-mesh
2. Press **Alt+H** to show all hidden meshes
3. Press **Ctrl+H** to isolate the selected mesh (hide everything else)
4. Critical for accessing hidden vertices (armpits, inner thighs)

---

## Export Pipeline

### SMD Export (ASCII Skeletal Mesh Data)

The `.smd` format is Half-Life's skeletal animation format used as intermediate.

```bash
# Via UI: Click "Generate .smd" in right panel
# Via API:
curl -X POST http://localhost:8000/api/v1/compile-gr2 \
  -H "Content-Type: application/json" \
  -d '{"smd_content": "...", "material_map": {...}}'
```

**SMD Format:**
```
version 1
nodes
  0 "Bip01" -1
  1 "Bip01 Pelvis" 0
  ...
end
skeleton
time 0
  0 0.0 0.0 0.0
  ...
end
triangles material_0
  material_0
    x y z
    nx ny nz
    bone1 weight1 bone2 weight2 ...
  ...
end
```

### GR2 Compilation

The `.gr2` (Granny 3D) format is what Metin2 client actually loads.

**Process:**
1. Frontend generates ASCII `.smd`
2. Backend receives `.smd` via `/api/v1/compile-gr2`
3. Backend calls Granny compiler (sub-process)
4. Returns binary `.gr2` blob

**Note:** Requires Granny SDK installed on server. The backend acts as a bridge.

### MSM Generation (Metin2 Script Mesh)

The `.msm` file tells the Metin2 client how to load the model.

```bash
# Via UI: Click "Generate .msm" in right panel
# Via API:
curl -X POST http://localhost:8000/api/v1/generate-msm \
  -H "Content-Type: application/json" \
  -d '{"model_path": "d:/ymir work/pc/warrior/armor.gr2", "texture_paths": [...], "class_name": "warrior"}'
```

**MSM Format:**
```
Group ShapeData01
{
  ShapeIndex
  {
    triangle_count
    vertex_offset
  }
  Model
  {
    model_path.gr2
  }
  SourceSkin
  {
    class_name_base
  }
}
```

### Material ID Preservation

During export, triangles are grouped by material ID. Each material group becomes a separate subset in the SMD file, ensuring Metin2 loads the correct `.dds` textures.

**Example material mapping:**
```json
{
  "0": "armor_body.dds",
  "1": "armor_spec.dds",
  "2": "armor_normal.dds"
}
```

---

## Advanced Features

### Pre-Export Validation

Before exporting, the validation pipeline checks:

- **Zero-Weight Vertices:** Auto-assigns unassigned vertices to nearest bone
- **Bone Count Limit:** Warns if >256 bones (Metin2 limit)
- **Weight Normalization:** Ensures all weights sum to 1.0
- **Protected Bones:** Prevents modification of socket bones

Run validation manually:
```javascript
import { validatePreExport } from '@/lib/validation'
const result = validatePreExport(vertices, bones)
if (!result.valid) {
  console.error('Export blocked:', result.errors)
}
```

### Animation Preview

1. Load an animation file (`.gr2` or `.smd` with frames)
2. Use the timeline at the bottom of the viewport
3. Press Play to see the mesh deform in real-time
4. Adjust speed (0.25x - 4.0x)
5. Enable Loop for continuous playback
6. Scrub through frames manually with the slider

### AI Reference Learning

Transfer weights from a reference armor to a new model:

1. Upload a reference model (already skinned)
2. Upload the target model (unskinned)
3. Click "Transfer Weights from Reference"
4. Backend uses KD-Tree to find vertex correspondences
5. Weights are transferred based on topological proximity
6. Result appears in viewport instantly

### Smart Topological Mirroring

Instead of simple X-axis mirroring, the system uses:

1. KD-Tree on backend to find topologically similar vertices
2. Considers surface distance, not just coordinate distance
3. Works even for asymmetric models
4. Enabled via "Smart Mirror" toggle in right panel

### Bind Pose Protection

Ensures the root bone (`Bip01`) has identical transform between reference and target:

1. Reference pose is captured before retargeting
2. If offset detected, all vertices are corrected to local bone space
3. Prevents misalignment after weight transfer

---

## Class-Specific Rigging

Different Metin2 classes have different bone structures:

| Class | Bones | Special Notes |
|-------|-------|---------------|
| Warrior_M | Standard | Balanced rig |
| Warrior_W | Standard | Female proportions |
| Ninja_M | Standard | Extra forearm bones |
| Ninja_W | Standard | Extra forearm bones |
| Sura_M | Standard | Extra tail bones |
| Sura_W | Standard | Extra tail bones |
| Shaman_M | Standard | Extra wing bones |
| Shaman_W | Standard | Extra wing bones |
| Wolfman_M | Extra | 30+ additional bones |

Select the target class in the top bar to load the correct base skeleton before retargeting.

---

## Troubleshooting

### Common Issues

**Problem:** Model appears distorted after weight painting
- **Solution:** Check weight normalization (sum should be 1.0 per vertex)
- Use "Normalize" brush mode to fix

**Problem:** Black textures in game
- **Solution:** Ensure material IDs are preserved during SMD export
- Check material mapping in export settings

**Problem:** Animation plays too fast/slow
- **Solution:** Adjust animation speed multiplier in player controls
- Check frame rate of source animation

**Problem:** AI weight transfer produces artifacts
- **Solution:** Ensure reference and target models have similar topology
- Use "Smooth AI Binding" after transfer

**Problem:** Export fails with "too many bones"
- **Solution:** Reduce bone count to 256 or fewer
- Use LOD simplification on the rig

**Problem:** Browser crashes on large models
- **Solution:** Enable mesh isolation to reduce rendered geometry
- Close other tabs to free memory

### Performance Tips

- Use **wireframe mode** for heavy models (>50k vertices)
- Enable **mesh isolation** for hard-to-reach areas
- Use **Undo** sparingly on large meshes (each undo stores state)
- Close unused animation files to free memory

### Debug Mode

Enable debug logging by setting `DEBUG=true` in environment:

```bash
DEBUG=true npm run dev
```

This outputs detailed information about:
- Vertex weight calculations
- KD-Tree queries
- Export pipeline steps
- API request/response data

---

## API Reference

### Frontend Store API

```typescript
// Get current state
const { selectedBone, meshData, brushMode } = useRiggingStore.getState()

// Update state
useRiggingStore.getState().setSelectedBone('bip01 spine')
useRiggingStore.getState().setBrushMode('smooth')

// Normalize all weights
const modified = useRiggingStore.getState().normalizeWeights()

// Undo last action
useRiggingStore.getState().undo()
```

### Backend API

```python
# Weight prediction
POST /ai_skin
{"vertices": [[x,y,z], ...], "nbones": 32}

# AI weight transfer
POST /api/v1/learn-weights
{"source_vertices": [...], "target_vertices": [...], "source_weights": [...], "bone_map": {...}}

# SMD to GR2 compilation
POST /api/v1/compile-gr2
{"smd_content": "...", "material_map": {"0": "tex.dds"}}

# MSM generation
POST /api/v1/generate-msm
{"model_path": "...", "texture_paths": [...], "class_name": "warrior"}

# Health check
GET /health
```

---

## License & Credits

- **Frontend:** React 18, Three.js, Zustand, TailwindCSS
- **Backend:** FastAPI, PyTorch, SciPy
- **Metin2:** NTLicense / Gravity Interactive
- **Granny 3D:** Granny SDK (for .gr2 compilation)

---

## Support

For issues, questions, or contributions:
- Check the `/docs` folder for detailed technical documentation
- Review `CHANGELOG.md` for recent updates
- Open an issue on the project repository

---

*Last updated: September 2026*
*Version: 1.0.0*