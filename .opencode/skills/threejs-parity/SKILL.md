---
name: threejs-parity
description: Native three.js-grade viewport parity (ACES/exposure, env-intensity, normal-map, per-submesh draws, orbit parity; explicit webview AGAINST)
---

# threejs-parity

Native D3D11 viewport reaches three.js-grade look/dev-experience WITHOUT
a webview: tone mapping, environment intensity, normal-mapped PBR,
per-submesh material draws and OrbitControls-grade camera feel — all in
the existing `Renderer` + `ArcballCamera` production path. Extends
`gltf-pipeline` (material intake) + `viewport-ux` (camera/router).
A webview (Electron/Cef/WebView2-hosted three.js) is explicitly AGAINST
this skill: no browser runtime, no JS bridge, no second renderer.

## Current truth

- Backend is native D3D11 only (`src/renderer.cpp`,
  `include/m2rig/renderer.hpp`; full contract in `rendering` skill).
  Offscreen production path composites via `ImGui::Image()` with UV
  `(0,0)->(1,1)` (no V-flip); aspect from LOGICAL `avail`, target size
  from PHYSICAL `rect`.
- Punctual PBR (25a) + IBL split-sum (25b) LANDED (`pbr-rendering`
  skill): `PsPbr`/`PsTexPbr`, `setPbrMaterial`, b2/b3 CBs, reference
  matched within 1 LSB. Linear lighting + sRGB encode live since Step 2.
- Single global texture slot: `impl_->activeTexture`
  (`src/renderer.cpp:397`, `setActiveTexture` at `:1571`);
  `drawMeshTextured` falls back to untextured when nothing is bound.
  Per-submesh material draws do NOT exist yet — `drawMeshRange` is a
  `pbr-rendering` Target-contract open item, not landed code.
- Tangent/bitangent GPU channels are zeros (canonical meshes carry
  tangents; no shader consumes them yet). There is NO ACES operator, NO
  exposure uniform and NO env-intensity uniform anywhere in
  `src/renderer.cpp` (verified by search — these are targets, not truth).
- Camera Wave-23 pinned (DPI orbit/pan/zoom, fmod yaw, pole guards,
  Front=+Z); residual gaps recorded in `viewport-ux`: any input cancels
  smoothing, linear yaw lerp, immediate ortho flip, stale near/far.
- Bridge boundary (honest): Noesis GR2 import dead (missing
  `granny2.dll`); primary path is grnreader98
  (`src/adapters/gr2_adapter.cpp`); game-client formats stay
  `NOT_SUPPORTED_DIRECTLY`, never silent identity converts.

## Target contract

- `ACESFilmicToneMapping` (or approved equivalent) + `exposure` uniform
  + `envIntensity` uniform scaling the IBL split-sum specular AND the SH
  irradiance ambient; all three pixel-pinned against CPU reference.
- Normal-map shader consuming the tangent channels: wire GPU tangent
  upload + tangent-consuming PS in the SAME commit (per `rendering`
  contract — `computeTangents` has no callers until that commit).
- Per-submesh draws: `drawMeshRange(key, first, count, materialKey)` +
  texture slots (albedo/normal/metalRough/AO/emissive); `CULL_NONE`
  stays until `fixWindingConsistent()` intake flips the default.
- Orbit parity with three.js OrbitControls: damped flights,
  shortest-arc yaw, `clipAuto` near/far, preset bookmarks — closing the
  four `viewport-ux` camera gaps without regressing Wave-23 pins.
- NEVER a webview: any proposal hosting three.js in a browser control
  is out of scope for this skill (use native `Renderer`).

## Entry points

- `src/renderer.cpp`, `include/m2rig/renderer.hpp`
  (`setPbrMaterial`, `setActiveTexture`, `drawMesh*/drawLines*`,
  offscreen `ensureViewportTarget/beginViewportPass/endViewportPass/
  readViewport`).
- `include/m2rig/camera.hpp` (`ArcballCamera`), `src/app/panels.cpp`
  viewport path (`drawViewportPanel/drawSceneContents/renderScene`).
- `Data/Models` real-asset ground truth; `tools/cli/main.cpp` verbs
  (`validate`, `info`); headless proof via `tests/test_render.cpp`
  (WARP, 64x64).

## Test gate

- `aces_exposure_matches_reference` (hand-computed sRGB values, same
  discipline as `texture_srgb_view_and_flat_identity`).
- `env_intensity_scales_ibl` (0.0 == punctual-only baseline within 1 LSB).
- `normal_map_consumes_tangents` (tangent-space perturb changes pixels;
  zero-tangent path unchanged).
- `per_submesh_draw_ranges` (two materials, one draw each, no fallback
  leak between ranges).
- `yaw_shortest_arc`, `overlay_router_priority`, `aspect_uses_logical`
  (existing `viewport-ux` pins stay green).

## Failure handling

Wave-23 camera pins + 25a/25b PBR reference values are load-bearing:
every parity PR re-runs them. Evidence beats proposals (Front/Back
revert is the model). Real commands only: `cmake --preset
windows-release`, `ctest --preset windows-release`, `m2rig_cli ...`.
