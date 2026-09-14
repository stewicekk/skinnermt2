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
pass), wire overlay (depth-biased), paint-mode brush circle overlay.

## Bone manipulation
ImGuizmo Translate/Rotate gizmo on the selected bone (Bone panel radio),
world->local via `parent.inverseRigid * world`, own XYZ euler extraction;
orbit/pick/paint gated while the gizmo is over/used; undoable.

Legacy note: `HelixViewport3D` / WPF controls do not exist here.
