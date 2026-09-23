---
name: gpu-skinning
description: GPU LBS/DQS skinning palette + vertex stream (Wave 24, 28)
---

# gpu-skinning

Moves skinning from CPU deform + full re-upload (`src/app/app_gpu.cpp`)
to a GPU palette path. The CPU builders stay as headless/test fallback —
`src/mesh_views.cpp` is dependency-free and is the permanent seam.

## Status: LBS LANDED (Wave 24), DQS-GPU deferred

- LBS: `SkinVertex` stream (32 B, slot 1) + `VsSkinned` + 256-matrix CB
  (b1) + `uploadSkinning/hasSkinning/releaseSkinning/setSkinningPalette`
  + `drawMesh[Textured/Flat/WireOverlay]Skinned`. `refreshGpu` uploads
  bind + skin once; `drawSceneContents` sets the per-frame palette
  (`bindInverse * currentGlobal`) — no per-frame CPU deform + re-upload.
  Identity palette reproduces the static draw bit-exactly (pinned).
- CPU-DQS toggle path unchanged (deformed verts, no skin stream — the two
  can never double-deform; `hasSkinning` gates). GPU-DQS palette is
  explicit follow-up, not silent debt.
- Author-8/export-4 NOT done (still ≤4 everywhere) — separate item.

## Current truth

- CPU palettes: `buildSkinningPalette` (`skin_weights.cpp:866-875`),
  `buildDqsPalette` (`:950-960`, antipodal fix `:975-998`).
- CPU deform: `deformVertex/deformNormal[Dqs]` (`:884-910,962-1072`).
- VS has no bone inputs (`renderer.cpp:36-46`); `useDqs` toggle
  (`app.hpp:107`); canonical bind data never touched (render-only).
- Authoring limit `<=4` enforced at paint/transfer/repair/export with
  `removedMass` reporting (`brain/README.md`); `maxInfluences` is plumbed
  as a parameter everywhere.

## Target contract

- Vertex channels: `BLENDINDICES (4x u8) + BLENDWEIGHT (4x fp)` = 8 B.
- Palette: `StructuredBuffer<float4x4> gBones (<=256)` LBS first;
  dual-quat `float2x4` path second; LBS/DQS switch as CB bit.
- Palette upload 1x/frame (`UpdateSubresource`); `uploadMesh` only on
  topology change; deform toggle keeps preview parity.
- Author-8 / export-4: lift `PaintParams::maxInfluences`
  (`skin_weights.hpp:99`) + transfer/auto-rig defaults to 8; clamp to 4
  only at the SMD/`.ani` export gate with existing mass reporting.

## Entry points

- `buildSkinningPalette/buildDqsPalette/deformVertex[Dqs]/deformNormal[Dqs]`,
  `buildGpuVerticesDeformed[Dqs]`, `App::refreshGpu`, `useDqs`.

## Test gate

- `palette_bind_identity` (bind pose -> identity deform)
- `lbs_dqs_rigid_parity` (diff <1e-4 on rigid binds)
- `gpu_cpu_parity_sample_armor`, `export4_clamp_mass_reported`

## Failure handling

CPU fallback must stay pixel-identical for tests. Never break the
`<=4` game-client gate. Frozen: `M^-1RM`, `local*parent`, bind snapshot
discipline (rebuild overwrites inverseBind — snapshot is mandatory).
