---
name: rendering
description: Native D3D11 rendering contract for the viewport
---

# rendering

Native D3D11 backend (`src/renderer.cpp`, `include/m2rig/renderer.hpp`).

## Contract
- Frame constant buffer is 128 B (32 floats: gWvp + gViewRot + gLightViewAndAmbient);
  every `Map(DISCARD)` rewrites all 32 via the cached lighting half
  (`beginScenePass` / `setSceneView` / `drawMesh*` / `drawLines*`).
- `GpuVertex` is 72 B, locked by `static_assert` (offsets 0/12/24/36/48/64:
  pos12+norm12+tan12+bitan12+col16+uv8). Tangent/bitangent are COPIED from
  canonical meshes (`mesh_views.cpp` builders, Wave 27) AND bound in the
  D3D11 input layouts (TANGENT@24/BITANGENT@36, static 6-elem + skinned
  8-elem); the vertex shaders pass them through, and `PsTexPbrNormal`
  consumes them (Slice C2: bind/unbind plumbing, unbound byte-identical
  to `PsTexPbr`).
  `computeTangents` runs at all import producers (SMD/FBX/LOD/samples).
- Solid rasterizer is `CULL_NONE`: SMD/FBX/GR2 bridge meshes have mixed
  winding, `CULL_BACK` made them invisible from the outside.
- `drawMeshTextured` falls back to untextured when no texture is bound
  (single global `activeTexture` slot; static-textured per-submesh
  routing structure exists, per-material uploads pending — no
  multi-material preview claimed).
- `setTextureMips` (authored-primary) beside `setTexture` (fallback);
  aniso-4x shared albedo sampler + linear data sampler.
- Offscreen viewport (production path): `ensureViewportTarget` /
  `beginViewportPass` / `endViewportPass` / `viewportSrv` / `readViewport`;
  composited via `ImGui::Image()` with UV `(0,0)->(1,1)` (D3D11 origin is
  top-left like ImGui — no V-flip). Aspect from LOGICAL `avail.x/avail.y`,
  target size from PHYSICAL `rect.w/rect.h`. Backbuffer `beginScenePass`
  path is kept for the headless smoke test + invalid-rect fallback only.
- `PbrMaterial` factors are consumed by the backend (`setPbrMaterial` into
  the b2 CB + `PsPbr`/`PsTexPbr`, Waves 25a/b); texture slots
  (normal/metallicRoughness/...) stay explicitly unsupported.
- Linear lighting (Step 2): `PsMain`/`PsTexSrgb`/`PsTexLinear` light in
  linear and encode sRGB on output (`SrgbToLinear`/`LinearToSrgb` in
  `kShaderSrc`). Albedo texels decode in-shader (`PsTexSrgb`); data maps
  stay raw (`PsTexLinear`, selected per texture by the `srgb` upload
  flag). Views stay UNORM: explicit UNORM_SRGB views fail creation on
  some runtimes (proven on the WARP test box), so hardware decode is
  deferred to the PBR wave pending hardware proof. Mip tails filter in
  gamma space (accepted tradeoff).
- `drawMeshFlat` (PsFlat display passthrough) is REQUIRED for
  Normals/Height/Weight/UV debug ramps (`drawSceneContents` routes them
  there); drawing them lit would wash them out under the linear pipe.
- PBR punctual (Wave 25a): `PsPbr`/`PsTexPbr` over shared `PbrLighting`
  (GGX + Schlick-GGX + Schlick Fresnel, metalness workflow, ambient
  placeholder till IBL); factors via `setPbrMaterial` into the b2 CB
  (PS-only, both passes); 4 PBR draws mirror the Blinn set incl. fallback
  discipline. Routed only to Solid/SolidWireframe (`App::usePbr`).
- PBR IBL (Wave 25b): procedural sky + init-time bake (CPU base,
  GGX prefilter chain, BRDF LUT, b3 SH irradiance CB) + split-sum
  specular replacing the ambient placeholder. HARD RULE: never sample a
  cube while rendering its mips (D3D11 unbinds the SRV at resource
  granularity — source/destination split required).
- Tangents: canonical meshes carry them (`computeTangents` at all import
  producers; SMD/FBX keep file normals), `GpuVertex` channels are copied
  + bound (Wave 27) and await the normal-map shader (Slice C2).
- Headless smoke: `tests/test_render.cpp` (WARP, 64x64) through
  solid/wire/overlay/textured/lines/X-ray + present, PLUS
  `offscreen_72B_viewport_pass` (Wave 27: production offscreen path with
  64x64 + 32x32 resize readback). DQS/deform upload and DPI-aspect cases
  stay open test gaps.
  `texture_srgb_view_and_flat_identity` pins the sRGB plumbing
  (srgb/linear flag, re-upload/release lifecycle, `generateMips=false`
  branch) plus hand-computed pixel values: flat `255,0,0`, sRGB-128
  `~78`, linear-128 `~116` (decode contrast), nomips == mipmapped.

## Entry points
- `Renderer::init/resize/beginScenePass/setSceneView/drawMesh/
  drawMeshWireOverlay/drawMeshTextured/drawLines/drawLinesXRay/present/
  readBackbuffer`,
  offscreen: `ensureViewportTarget/viewportSrv/beginViewportPass/
  endViewportPass/readViewport/frameStats/resetFrameStats`,
  textures: `setTexture/hasTexture/textureIsSrgb/setActiveTexture/releaseTexture`,
  meshes: `uploadMesh/hasMesh/releaseMesh`,
  skinning (Wave 24): `uploadSkinning/hasSkinning/releaseSkinning/
  setSkinningPalette` + `drawMesh[Textured/Flat/WireOverlay]Skinned`
  (VsSkinned, slot-1 stream, 256-matrix b1 CB; identity palette ==
  static draw bit-exact, pinned by `gpu_skinning_lbs`),
  `drawMeshTextured/drawMesh/drawMeshFlat/drawMeshWireOverlay`,
  `buildGpuVertices*/buildSkinVertices/buildGridLines/filterVisibleIndices`.

## Failure handling
Stop expansion on critical validation failure, reproduce the issue, fix root
cause, run regression tests, then continue.
