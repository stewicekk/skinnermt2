# Skill: Textured Viewport (DDS)

Native DDS + textured path (`src/dds.cpp`, `src/renderer.cpp` `drawMeshTextured`, `src/app/app_gpu.cpp`).

## Contract
- Decoder in core: DXT1/BC1, DXT3/BC2, DXT5/BC3 (incl. 1-bit alpha + both alpha ramps), mip-size validation, DX10 BC1-3 mapping, explicit rejections.
- Viewport: UV channel in 48 B vertex layout (static_assert offsets 0/12/24/40), textured pixel shader + linear sampler + SRV cache, untextured fallback; first-material DDS resolve (literal / exeDir / Data-Models / basename); Tex toggle (toolbar, View menu `T`, `T` key).
- Material edit marks `gpuDirty` so the new path re-resolves on next upload.
- Frame CB 128 B lighting applies to textured too (`t.rgb * col.rgb * d + spec`).
- Tests: `dds.rejects_garbage/decodes_dxt1/decodes_dxt5/reads_real_model_texture_header` (512x512 DXT3 live), `render.headless_frame_through_all_paths` (fallback + textured).

## Entry points
- `decodeDds/readDdsFile`, `Renderer::setTexture/setActiveTexture/drawMeshTextured`, `resolveTextureFile`, `drawMaterialPanel`.
