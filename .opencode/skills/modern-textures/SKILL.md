---
name: modern-textures
description: sRGB-correct multi-material texture pipeline (Waves 25, 27)
---

# modern-textures

sRGB-correct multi-material texture pipeline (Waves 25, 27, C2).

## Current truth (Wave 27 + C2 landed)

- Decode BC1-3 + BC4/BC5-UNORM, ALL declared mips (`dds.hpp/hpp`,
  per-level truncation fail); SNORM/uncompressed/cubemaps explicit fail.
- In-shader sRGB decode landed (Step 2: `PsTexSrgb`/`PsTexLinear` +
  `srgb` flag; UNORM views kept — SRGB views fail on WARP, proven).
- Authored-mip upload primary (`setTextureMips`, no GenerateMips);
  `setTexture` fallback for mipCount<=1/procedural.
- Tangents wired at import + copied to `GpuVertex` + TANGENT@24/
  BITANGENT@36 BOUND in both layouts + VS passthrough; `PsTexPbrNormal`
  consumes them (bind/unbind plumbing, unbound byte-identical).
- Full 12-variant range matrix (solid/textured/flat/overlay/skinned/
  PBR x range). Static-textured per-submesh routing structure in
  `drawSceneContents` (lazy probe + fallback); per-material UPLOADS
  pending (`refreshGpu` still `materials[0]`-only) — no multi-material
  preview claimed.
- Sampler aniso-4x shared + data sampler; `materials[0]`-only upload;
  sync decode per `gpuDirty`; `computeTangents` at all producers.
- Still open: content-hash SRV cache, LRU, `geometryDirty`/
  `materialDirty` split, 8-16x aniso, async decode + placeholder,
  per-submesh PBR data model, `MESH_UV_RANGE/OVERLAP/DEGENERATE`.

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

- `texture_srgb_view_and_flat_identity` (78/116 pins), `decodes_dxt1_8x8_
  two_mips`, `truncated_mip2_fails`, `decodes_bc4_ramp`, `decodes_bc5_rg`,
  `rejects_bc_snorm_dxgi`, `authored_mips_upload_idempotent_and_ranges`,
  `range_parity`, `normal_map_bound_vs_unbound`, `offscreen_72B_viewport_
  pass`, `gpu_tangents_nonzero`

## Failure handling

Keep `NOT_SUPPORTED` honest fails + DoS caps (`dds.cpp:88`,
`readDdsFile` 512 MB). `CULL_NONE` stays. Never touch the
`NoBackground` + offscreen compositing path for texture work.
