---
name: texture-cache
description: FNV-1a content-hash SRV dedup + 256MB LRU + stats + TTL probe cache (Wave 31)
---

# texture-cache

Dedup + eviction slice of the texture pipeline: content-hash SRV
sharing, bounded LRU, cache stats, and a TTL probe cache for material
file checks. Extends `modern-textures` + `pbr-rendering`; `rendering`
stays the base contract.

## Current truth (ALL LANDED, Round C)

- FNV-1a content-hash SRV dedup: key = hash(bytes) + w + h + srgb +
  mipCount; hits share the view (refcounted), `releaseTexture`
  drops refs (zero destroys) + clears active/normal bindings;
  pixels identical.
- 256 MB cap: live refs never evicted; pressure falls back to
  dedicated uncached upload (never fails valid uploads); cumulative
  `TextureCacheStats{entries,bytes,hits,misses}`; eviction ORDER
  honestly unpinned (would need >256 MB test allocs).
- 2 s TTL probe cache on the Materials panel path (TU-local,
  resolve uncached); per-material uploads (`#mat`/`#nmat` keys)
  complete the multi-material preview.
- Sampler cache (aniso-4x + linear data) landed earlier.

## Target contract (open)

- LRU eviction-order pin (needs test-sized cap override, not
  256 MB allocs); 8-16x aniso; async decode + placeholder;
  `geometryDirty`/`materialDirty` split.

## Entry points

- `include/m2rig/renderer.hpp` (`setTexture/setTextureMips/
  hasTexture/setActiveTexture/releaseTexture/textureIsSrgb`,
  `bindPbrNormalMap/clearPbrNormalMap`, `PbrMaterial`,
  `drawMeshRange` family), `src/renderer.cpp` (`Impl::textures`,
  sampler cache `:441-447`, upload `:1631-1672`,
  `textureIsSrgb` `:1765`), `include/m2rig/dds.hpp` +
  `src/dds.cpp` (`decodeDds/readDdsFile`, 512 MiB cap),
  `src/app/app_gpu.cpp` (`refreshGpu`, `resolveTextureFile`
  search), `src/app/panels.cpp` (`drawSceneContents` routing,
  `drawMaterialPanel`, scan TTL `:2308-2315`),
  `src/fbx/fbx_reader.cpp:97-120` (FNV-1a pattern to reuse).

## Test gate

- Real (must stay green): `render.texture_srgb_view_and_flat_
  identity`, `render.authored_mips_upload_idempotent_and_ranges`,
  `render.range_parity`, `render.normal_map_bound_vs_unbound`,
  `render.offscreen_72B_viewport_pass`
  (`tests/test_render.cpp:141, 1125, 1302, 1531, 1025`);
  `dds.decodes_dxt1_8x8_two_mips`, `dds.truncated_mip2_fails`,
  `dds.decodes_bc4_ramp`, `dds.decodes_bc5_rg`,
  `dds.rejects_bc_snorm_dxgi`
  (`tests/test_dds.cpp:107, 158, 255, 317, 377`).
  Commands: `cmake --preset windows-release` +
  `cmake --build --preset windows-release` +
  `ctest --preset windows-release --output-on-failure`
  (same for `windows-debug`); CLI spot: `m2rig_cli validate
  in.smd`, `m2rig_cli smd2gltf in.smd out.glb`.
- Real (green): `texture_cache_dedup_and_accounting`
  (hash dedup, distinct bytes, release-drops-ref, srgb-in-key,
  authored dedup) + all Step-2/C/C2 pins above.

## Failure handling

- Never share across the `srgb` flag (albedo vs data decode
  differs in-shader); never evict a view bound in the current
  pass; keep the `NOT_SUPPORTED` + DoS caps honest (`dds.cpp`
  512 MiB, `diagnostics.hpp:11`). `CULL_NONE` stays. Never touch
  the `NoBackground` + offscreen compositing path for cache work.
  Frozen: Y-up, `M^-1RM`, `local*parent`, WARP smoke green.
