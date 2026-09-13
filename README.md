# Metin2 Rigging Studio

Full-stack Metin2 armor skinning workspace: interactive Three.js viewport, manual weight painting, AI-assisted weight transfer, SMD/MSM export, animation preview, local project snapshots, and autosave.

## Repository layout

- `frontend/`: React 18 + TypeScript + Vite + Three.js + Zustand + Tailwind
- `ai_backend/`: FastAPI weight-transfer API, MSM generation, optional GR2 compiler bridge, collaboration server
- `docs/USER_GUIDE.md`: detailed Czech/English usage documentation

## Run frontend

```powershell
cd D:\devapp\skinnermt2\frontend
npm install
npm run dev
```

Open `http://localhost:5173`.

Production verification:

```powershell
cd D:\devapp\skinnermt2\frontend
npx tsc --noEmit
npm run build
```

## Run backend

```powershell
cd D:\devapp\skinnermt2\ai_backend
pip install -r requirements.txt
python server.py
```

The API listens on `http://localhost:8000`.

Set `VITE_API_URL=http://localhost:8000` for the frontend when needed.

## GR2 compilation

Direct GR2 writing is intentionally not faked. Configure a licensed local Granny compiler:

```powershell
$env:GRANNY_COMPILER="C:\tools\granny\compiler.exe"
```

The frontend sends validated SMD to `/api/v1/compile-gr2`; the backend executes the configured compiler and returns the generated GR2 for download. Without `GRANNY_COMPILER`, the endpoint returns HTTP 501.

## Core functional scope

- Sample symmetric warrior armor loads automatically
- SMD import/export with indexed vertices, bone influences, materials, and animation frames
- Brush Add/Subtract/Smooth/Normalize with radius and strength
- Axis symmetry with mirrored bone IDs and mirrored vertex lookup
- Heatmap/solid/wireframe viewport modes
- Command-pattern undo/redo with Ctrl+Z and Ctrl+Y
- IndexedDB autosave with automatic session restore
- Bone locking, bone hierarchy, sub-mesh visibility, H/Alt+H shortcuts
- Pre-export validation, MSM export, AI KD-tree transfer, animation preview
- Local project snapshots with versions
