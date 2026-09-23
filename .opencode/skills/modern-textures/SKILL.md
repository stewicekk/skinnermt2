---
name: modern-textures
description: sRGB-correct multi-material texture pipeline (Waves 25, 27)
---

# modern-textures

Fixes the texture chain end to end: decode -> color space -> mips ->
samplers -> per-submesh binding -> cache. Biggest correctness item first:
today 0x `_SRGB` exists and every textured pixel is gamma-wrong.

## Current truth

- Decode BC1-3 base-mip only (`dds.hpp:2-5`, `dds.cpp:101-141`);
  authored mips discarded (`dds.hpp:17`).
- Single global `activeTexture` (`renderer.hpp:119`); `materials[0]` only
  (`app_gpu.cpp:113`); Materials panel edits all, viewport shows one.
- Sampler `LINEAR/WRAP/aniso-1` global (`renderer.cpp:253-263`).
- `GenerateMips` on UNORM (`renderer.cpp:737-739`) — gamma-space bug.
- Sync decode per `gpuDirty` on UI thread; 5x `exists()` per material per
  frame (`panels.cpp:1535-1540`); ancestor-walk resolve
  (`app_gpu.cpp:17-42`).
- `computeTangents` 0 callers; `uv1/color` channels declared, ignored.

## Target contract

- Albedo SRV `UNORM_SRGB`, data maps linear; sRGB-aware mip generation;
  `PsFlat` for Normals/Height/Weight/UV debug modes.
- `drawMeshRange` per `SubMesh` + SRV per `materials[i]` + material CB.
- Tangent: wired at import + copied to `GpuVertex` + `TANGENT vec4`
  input, or channels deleted — no paid-for zeros.
- Authored-mip upload; content-hash SRV cache; `geometryDirty` vs
  `materialDirty` split; aniso 8-16x + sampler cache; LRU 256-512 MB;
  2 s TTL probe cache; async decode with magenta placeholder (existing
  `frameTexturedFallback` path).
- BC4/5 + uncompressed next; BC6H/7 + KTX2 later; `xatlas` export-only;
  `MESH_UV_RANGE/OVERLAP/DEGENERATE` findings.

## Entry points

- `decodeDds/readDdsFile`, `setTexture/hasTexture/setActiveTexture`,
  `resolveTextureFile`, `App::refreshGpu`, `drawMaterialPanel`,
  `computeTangents`, `buildGpuVertices*`.

## Test gate

- `srgb_roundtrip`, `dxt3_nibble`, `edge_7x5`, `dx10_bc3`, `truncated_mip2_fails`,
  `tangent_consumed`, `multimaterial_ninja_preview`

## Failure handling

Keep `NOT_SUPPORTED` honest fails + DoS caps (`dds.cpp:88`,
`readDdsFile` 512 MB). `CULL_NONE` stays. Never touch the
`NoBackground` + offscreen compositing path for texture work.
