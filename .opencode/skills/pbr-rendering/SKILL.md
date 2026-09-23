---
name: pbr-rendering
description: Modern PBR rendering backend + RHI abstraction for the viewport (Waves 24-25)
---

# pbr-rendering

PBR track for the D3D11 backend (`rendering` stays the base contract).
Punctual backend LANDED in Wave 25a (`PsPbr` + material binding with
pixel-proof tests); IBL + RHI abstraction are the open items below.
Never claim IBL output until irradiance/prefilter/BRDF-LUT land tested.

## Current truth (do not re-derive, do not contradict)

- `GpuVertex` 72 B (`renderer.hpp:36-42`); tangent/bitangent still zeros
  on GPU (canonical mesh carries tangents since 25a; GPU copy in Wave 27);
  layout skips them (`renderer.cpp`).
- Frame CB 128 B, full `Map(DISCARD)` per draw via `cachedSecondHalf[16]`
  (`renderer.cpp:131-134`).
- `CULL_NONE` load-bearing (`renderer.cpp:296-302`).
- Offscreen production path: logical `avail` aspect, physical `rect`
  target, UV `(0,0)->(1,1)` — no V-flip.
- `PbrMaterial` consumed by `setPbrMaterial` (punctual factors);
  linear lighting + sRGB encode live since Step 2.

## Status: punctual + IBL LANDED (Waves 25a/b)

- Landed 25a: `PsPbr`/`PsTexPbr` (GGX + Schlick-GGX + Schlick Fresnel,
  metalness workflow, ambient placeholder), `PbrMat` b2 CB + neutral
  defaults, `setPbrMaterial`, 4 PBR draws with fallback discipline,
  per-asset factors + `usePbr` toggle (Solid/SolidWireframe only).
- Landed 25b: IBL — procedural sky (no assets) + `ibl.hpp/ibl.cpp`
  (evalSky, SH project, half codec, all unit-pinned) + init-time bake
  (CPU base, GGX prefilter chain, BRDF LUT, b3 irradiance CB) +
  split-sum specular in `PbrLighting` (ambient placeholder DELETED).
  Reference-matched within 1 LSB: dielectric 156, metallic 107,
  mirror-sun 255. HARD RULE learned: never sample a cube while rendering
  its mips — D3D11 unbinds the SRV at resource granularity even across
  disjoint subresources (source/destination split required).

## Target contract (open items)

- `IRenderer` interface + `D3D11Renderer : IRenderer` (`rhi/`); DXC SM 6.x;
  `rhi-null` for headless Linux CI.
- Per-submesh material draws + texture slots (albedo/normal/metalRough/AO/
  emissive) + `uvOffset/Scale/Rotation` (rides Wave 27 with `drawMeshRange`).
- CB split `b0 frame (1x/pass)` + `b1 draw (WVP)`; lighting dirty-flag.
- `R16F` offscreen + MSAA 4x resolve + `ACES + FXAA`; failure-atomic
  resize with half-res OOM fallback + toast.
- Intake `fixWindingConsistent()` (Wave-17 topology) then `CULL_BACK`
  default + per-material `doubleSided`.

## Entry points

- New: `rhi::IDevice`, `D3D11Renderer`, `setPbrMaterial`,
  `drawMeshRange(key, first, count, materialKey)`, `fixWindingConsistent`.
- Reused: all `rendering` entry points (unchanged signatures).

## Test gate (punctual 25a + IBL 25b: LANDED)

- 25a hand-verified (pre-IBL history): metal 98 / dielectric 118 / sharp
  255 vs broad 151 / greybase 86 / AO ordering / emissive lift /
  textured 58 / skinned twins.
- 25b reference-matched within 1 LSB: dielectric 156, metallic 107,
  mirror-sun 255 (+r>150 tripwire), textured/skinned twins. Geometry must
  place H≈N to discriminate roughness (off-lobe roughness correctly
  yields ~117; recorded in the test).
- Still open: `offscreen_72B_viewport_pass`, `cull_back_ninja_visible`.

## Failure handling

Stop expansion on critical validation failure, reproduce, root-cause,
fix, focused tests, regression (`ctest --preset windows-release`), then
continue. Frozen: Y-up, `M^-1RM`, `local*parent`, WARP smoke green.
