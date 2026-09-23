# Skill: Viewport 3D Control

Native D3D11 viewport (`src/renderer.cpp`, `drawViewportPanel` +
`renderScene` in `src/app/panels.cpp`).

## Camera
- Left-drag orbit, right/middle-drag pan, wheel zoom, `F` frame-all
  (`ArcballCamera`, `include/m2rig/camera.hpp`); Front/Top/Left presets;
  Persp/Ortho toggle.
- Bone picking: click selects nearest bone segment (`pickBoneAt`);
  hover highlights + tooltip; paint mode uses Ctrl+drag on the mesh
  surface (`pickMeshPoint`, ray-triangle).

## View modes (Toolbar Combo + View menu + keys 1-7)
Solid, Wireframe, Solid+Wire, Normals, Height, Weights (per-bone heatmap),
UV (uv0 + checker). Extras: grid, bones, X-ray bones (depth-off line
pass), wire overlay (depth-biased), paint-mode brush circle overlay,
textured sampling (Tex toggle / T key, first material DDS, solid fallback
when the texture is missing).

## Projection (Wave 15, right-handed for D3D [0,1])
`lookAt` is RH (camera looks down `-z`, view z negative in front);
`perspectiveFov`/`orthographic` map `-near -> 0`, `-far -> 1` with `w = -z`
(`m23 = -1`). `vp = view * proj` (row-vector). Picking unprojects NDC
`z = 0/1` through the same `inverseGeneral` matrix. Guard:
`camera_frames_sample_in_ndc` + `camera_frames_big_aabb_radius100_in_ndc`.

## Camera limits + adaptive scene (Wave 15.1)
`ArcballCamera::framedRadius` + `updateClip()`: `near = d*0.05`,
`far = max((d+2r)*1.5, d+12, 200)`; zoom clamps follow the framed model
(`maxDistance`/`maxOrthoHeight`, absolute caps 10000/5000). `frameAabb`
stores radius + clamps + updates clip, so 1-unit samples and 500-unit FBX
rigs both stay inside the frustum.
Grid is adaptive (`half = clamp(r*1.5, 5, 500)`, `step = half/10`,
axes `half*0.3`); joint crosses are screen-aware
(`wpp*6` clamped to `r*0.002..r*0.05`).

## Bone manipulation
ImGuizmo Translate/Rotate/Scale gizmo on the selected bone (Bone panel radio,
WORLD), world->local via `parent.inverseGeneral * world` (handles scaled
parents), own XYZ euler extraction with scale-strip for Rotate; guarded
divide for Scale; orbit/pick/paint/pan/zoom gated while the gizmo is used;
lock-checked before undo push (blocked drag reports warning, no fake
success); undoable (weights+pose+scale). Gizmo block runs only while the
viewport is hovered (`BeginFrame` gating); radio alone does not draw.

## Viewport truth (Wave 14-15)
- `ImGuiWindowFlags_NoBackground` is load-bearing: D3D scene renders before
  `ImGui::Render`, opaque WindowBg would hide grid/mesh/bones/gizmo.
- `PassthruCentralNode` must be on BOTH `DockBuilderAddNode` and
  `DockSpaceOverViewport`: the builder node without it paints an opaque
  central bg over D3D (black viewport incl. grid).
- Fullscreen `InvisibleButton` needs `SetItemAllowOverlap`, otherwise the
  overlay buttons (Frame/Persp/Front/Top/Left) never get hover/click.
- Frame CB is 128 B (gWvp + gViewRot + gLightView); every Map rewrites all
  32 floats via cached lighting half (`Renderer::setSceneView` + draw*).
- Pixel proof: `Renderer::readBackbuffer` + render-test assertion
  (`>200 non-clear px` on 64x64) guards against silently black draws.
- `F` framing is gated on `!WantTextInput`; pan/zoom gated on `!gizmoUsing`.
- Textured (T hint in View menu) samples first-material DDS, solid fallback.

Legacy note: `HelixViewport3D` / WPF controls do not exist here.
