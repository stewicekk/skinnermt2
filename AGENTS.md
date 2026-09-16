# Base44 Dev Environment — Metin2 Rigging Studio

## What this project is

A native C++20 Windows desktop app (DirectX 11 + Dear ImGui + Win32) for
Metin2 armor rigging/skinning. The native app **cannot run on Linux/Docker** —
it requires Windows + DirectX 11.

## What runs in the Base44 preview

The **archived React prototype** (`_archive/frontend/`) — a Vite 5 + React 18 +
Three.js + Tailwind web app — is the only web-servable component. It provides a
3D viewport, bone tree, weight painting, SMD import/export, MSM export, and
local project persistence (IndexedDB). It is served on **port 3000**.

The **Python FastAPI backend** (`ai_backend/server.py`) runs on **port 8000**
and provides AI weight transfer (`/api/v1/learn-weights`), GR2 compile
(`/api/v1/compile-gr2`), and MSM generation (`/api/v1/generate-msm`). The
frontend calls it via `VITE_API_URL`. Most frontend features work without it.

## Architecture (dev mode)

- `web` service: `node:20-slim`, bind-mounts `_archive/frontend/`, runs `npx vite`
  (dev server with HMR). Port 3000 → 5173.
- `ai_backend` service: `python:3.10-slim`, bind-mounts `ai_backend/`, runs
  `uvicorn server:app --reload`. Port 8000.

## Key files modified for Base44

- `_archive/frontend/vite.config.ts` — added `server.host: true`,
  `server.allowedHosts: true`, `server.strictPort: true` so Vite accepts the
  preview's external hostname.
- `ai_backend/server.py` — CORS set to `allow_origins=["*"]`,
  `allow_credentials=False` so the preview origin can call the API.
  (Frontend does not use cookies/credentials.)

## No external secrets required

The backend's GR2 compile endpoint optionally reads `GRANNY_COMPILER` (path to
a local Granny compiler binary). Without it, that endpoint returns 501 — all
other features work. No credentials are needed to boot.

## Verifying the app

```bash
docker compose -f docker-compose.base44.yml up -d --build
docker compose -f docker-compose.base44.yml ps
curl -s http://localhost:3000 | head -5       # frontend HTML
curl -s http://localhost:8000/health          # backend health
```

The frontend loads a sample armor model automatically on first visit.
