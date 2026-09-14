# Changelog

## 0.10.0 (2026-09-13)
- Textured viewport: dependency-free DDS decoder (DXT1/3/5, verified on a
  real 512x512 game texture), UV vertex channel, textured pixel shader +
  sampler, Tex toggle (toolbar, View menu, T key), missing-texture
  fallback to solid shading.
- Flood/Prune group ops over the selected bone (undoable, lock-guarded,
  mass-reported) with Bone-panel buttons.
- Async bridge imports (`startBridgedImport` + per-frame `pollBridgeImport`,
  UI stays live, import buttons gated, running-time indicator); shared
  `runBridgeChain` worker for grnreader/Noesis.
- Tests 51/51 + 6/6 suites green (`/W4 /WX`, release).

## 0.9.0 (2026-09-13)
- Waves 1-10 complete: core, D3D11 viewport (7 modes, X-ray, wire overlay,
  UV, heatmap), ImGuizmo manipulators, CPU skinning preview + timeline
  transport, SMD/MSM native pipeline, grnreader98 + Noesis bridges,
  weight paint/transfer/self-train/symmetry/locks, batch export, submesh
  isolation, `.m2rig` workspaces, headless `m2rig_cli`, CI.
- 41/41 checks + 5 CLI suites green (`/W4 /WX`, debug + release).
- 14 race/gender skeleton profiles; real-data verified (90-bone ninja,
  37-bone warrior).
- Skills refreshed to native reality; `DEPENDENCIES.md` added.

## 0.1.0
- Initial native project skeleton (CMake, core lib, ImGui shell).
