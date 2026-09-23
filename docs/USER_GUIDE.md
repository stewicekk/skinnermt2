# Metin2 Rigging Studio — User Guide

## Overview

Professional Metin2 armor skinning workstation. Native C++ desktop app
(D3D11 + ImGui) with a headless `m2rig_cli` batch tool. The `ai_backend/`
folder is a tracked experimental reference, not required by the native
app (core links nothing third-party).

## Quick Start

### Native Desktop (the app)
```powershell
cmake --preset windows-release
cmake --build --preset windows-release
.\build\release\Release\Metin2RiggingStudio.exe
```
The app auto-loads a sample armor at startup (already framed and
validated). Import real models via `Project > Import FBX/GR2 (bridge)...`,
then press `F` to frame.
The dock layout is persisted in `config/imgui.ini`; delete that file to let
the application create the default workstation layout again.

## Core Workflow

1. **Load model** — `Project > Import FBX/GR2 (bridge)...` (native OpenFBX
   first, Noesis `?cmode` fallback, grnreader98 primary for GR2) or SMD via
   `Project > Import SMD...`, or `Project > Load sample armor` for the
   offline sample.
2. **Frame it** — press `F` (or `Frame (F)` in the viewport) so the camera,
   clip planes and grid fit the model (works for 1-unit samples and
   500-unit FBX rigs).
3. **Inspect skeleton** — Bone tree in left panel (root opens by default),
   click a bone or click it in the viewport to select.
4. **View modes** — keys `1-7` (Solid, Wireframe, Solid+Wire, Normals,
    Height, Weights, UV) + `T` textured (first-material DDS), `G` grid,
    `B` bones, `X` X-ray, `W` wire overlay, `D` deform preview
    (toolbar checkbox, needs 2+ frames to matter), `DQS` toggle next to
    it (dual-quaternion preview, requires Deform ON).
    Toolbar also holds Undo/Redo (`Ctrl+Z/Y`) and Validate shortcuts.
5. **Gizmo** — Bone panel radio Translate/Rotate/Scale (WORLD); drag the
   manipulator in the viewport (always drawn while a bone is selected).
   Dragging a locked bone is blocked with a warning; every drag is one
   undo step and re-runs validation on release. The scene pass is clipped
   to the visible panel, including during resize/DPI transitions, so the
   model and environment cannot draw over docked panels.
6. **Paint weights** — select a bone, enable Paint, `Ctrl/Shift+drag` on the
   mesh (the brush ring turns red while no bone is selected). Weights view
   shows the per-bone heatmap + legend. Flood/Prune live in the Bone panel
   and Auto-rig in the Weights panel + the toolbar "Auto-rig" button (all
   undoable, respect bone locks `[L]`; painting a locked bone or with no
   verts in radius is refused with an honest status instead of a fake
   success).
7. **Mirror / Transfer** — X/Y/Z symmetry + Quick (kNN k=3) or Self-train
   transfer from another loaded asset; locked bones are preserved.
8. **Validate** — `Project > Run validation` (≤4 influences, normalization,
   socket-deform warnings); export is gated on errors.
9. **Animate (optional)** — Timeline panel: `Add key` captures the current
    pose (gizmo-posed bones included) at the frame cursor; scrub/play
    samples the clip with quaternion (slerp) interpolation converted at
    the euler boundary, so wide twists take the short arc; `Bake clip`
    writes keys to SMD frames (replaces imported frames, then exports
    with them; bake is byte-identical to preview by construction). Keys
    are in-session only (not saved to `.m2rig` yet).
10. **Export** — SMD / MSM (shared GUI+CLI writer) / FBX + ANI + GR2 via
    bridge / GR2→FBX (all reachable from `Project` menu AND the Export
    panel), LOD-decimated SMD
   (`Export LOD SMD...` with keep-ratio slider), or batch-export all
   assets. LOD decimates a copy (live mesh, undo and isolation untouched),
   keeps ≤4 normalized influences per merged vertex, and re-runs the
   export gate.

## Brush Tools

| Tool | Action |
|------|--------|
| Add | Increase weight for selected bone |
| Subtract | Decrease weight |
| Smooth / Blur / Sharpen | Soften / broaden / contrast weights |
| Normalize | Force sum = 1.0 per vertex |
| Flood (Bone panel) | Bind whole mesh to the selected bone |
| Prune (Bone panel) | Remove the selected bone from every vertex |

## View Modes

- **Solid** — Lit rendering (+ `Tex` for DDS textures, + `PBR` for
  Cook-Torrance shading with metallic/roughness/AO factors)
- **Wireframe** — Edge view
- **Solid + Wire** — Solid with depth-biased overlay
- **Normals / Height / UV** — Debug colorings (UV shows checker)
- **Weights** — Per-bone heatmap (blue→red) + legend
- The viewport **Shading** button opens the same switches as a popover
  (modes + X-ray/Tex/PBR/overlay/Grid/Bones + Paint); one state, two
  access points.

## Export Formats

- **SMD** — Skeletal mesh (ASCII `version/nodes/skeleton/triangles`)
- **MSM** — Metin2 mesh (`Group` AST, Inspector panel + `validate-msm`)
- **GR2** — Granny 3D **bridge only** (grnreader98 primary `*.gr2 -a`,
  Noesis fallback; native emit is `NOT_SUPPORTED_DIRECTLY`)
- **glTF 2.0** — import only (`m2rig_cli gltf2smd in.glb/.gltf out.smd`:
  indexed meshes, ≤4 skin weights, inverse-bind, PBR factors; animations,
  Draco/meshopt and export are explicit `NOT_SUPPORTED_YET`)

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| F | Frame all |
| G / B / X / W / P / D / T | Grid / Bones / X-ray / Wire overlay / Paint / Deform / Textured |
| 1-7 | View modes |
| Space / Left / Right | Play-pause / step timeline (needs 2+ frames) |
| Ctrl/Shift+drag | Paint weights (needs Paint mode + bone) |
| Drag / Right-Middle-drag / Wheel / Click | Orbit / pan / zoom / select bone (click: <5px, no modifiers) |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |

## Weight Transfer

1. Load the reference SMD (already skinned) and the target SMD
2. Weights panel → "Transfer from..." → Quick (kNN, k=3 — fast, uses the
   current bone map) or Self-train (optimizes the bone map first —
   slower, better fit for mismatched skeletons)
3. Locked bones (`[L]`) keep their weights; removed mass is reported
4. Headless equivalent for the auto-rig path: `m2rig_cli autorig`

## Headless CLI (`m2rig_cli`)

Same core as the desktop app (no D3D/ImGui). Exit codes: 0 ok,
1 usage, 2 IO/parse, 3 validation/export-blocked, 4 write failure.

```powershell
.\build\release\Release\m2rig_cli.exe validate tests\data\two_bone.smd [--profile pc_warrior_m]
.\build\release\Release\m2rig_cli.exe validate-msm tests\data\sample.msm
.\build\release\Release\m2rig_cli.exe info tests\data\two_bone.smd
.\build\release\Release\m2rig_cli.exe smd2smd tests\data\two_bone.smd out.smd
.\build\release\Release\m2rig_cli.exe smd2msm tests\data\two_bone.smd out.msm
.\build\release\Release\m2rig_cli.exe autorig tests\data\two_bone.smd out.smd
.\build\release\Release\m2rig_cli.exe lod tests\data\two_bone.smd out_lod.smd --ratio 0.5
.\build\release\Release\m2rig_cli.exe fbx2smd Data\Models\ninja.fbx out.smd
.\build\release\Release\m2rig_cli.exe gltf2smd in.glb out.smd
.\build\release\Release\m2rig_cli.exe gr22smd Data\Models\warrior_m.gr2 out.smd
.\build\release\Release\m2rig_cli.exe orient out.smd
```

`fbx2smd`/`gltf2smd` need their optional libraries at build time
(`M2RIG_WITH_OPENFBX` / `M2RIG_WITH_CGLTF`, on by default); without a
checked-in fixture they stay build-only verbs, not ctest suites.

`autorig` binds every vertex to the nearest skeleton bones (same
deterministic engine as the GUI "Auto-rig from skeleton" button), runs the
standard repair + export gate, and prints the bind stats.

`gr22smd` is the headless twin of the GUI "Import GR2 via grnreader98"
path (same conversion profile, repair and export gate; needs
grnreader98 under `Data/resources/Convert gr2 to mesh/`).

`orient` prints automated transform diagnostics (world-space joints vs
mesh bounds: head/feet order, mesh overlap, scale advisory, rigid-bind
distance) and exits 0 when sane, 3 when a blocking finding exists.
Run it after every GR2/FBX import: a rotated 90/180°, mirrored, scaled
or detached asset is reported instead of silently entering the session.

## Bone Transform Editing

The Bone panel edits the selected bone's LOCAL transform numerically:
Position XYZ, Rotation XYZ (degrees in the UI, radians in core) and
Scale XYZ. Every edit pushes undo, rebuilds the skeleton, revalidates
and refreshes the viewport; locked bones refuse edits, non-finite input
is rejected and near-zero scale is refused (singular bind inverse).
Helpers: Frame bone (camera target to the joint), Select mirror
(profile L/R counterpart), Copy/Paste transform, Reset to bind
(frame 0, needs an imported animation). The viewport gizmo offers
Translate/Rotate/Scale in World or Local orientation (Bone panel radios);
all gizmo drags are undoable and lock-checked.

## Workspace

- Manual `Project > Save/Load workspace...` (`.m2rig` JSON: asset refs,
  locks by bone name, brush, camera mode, validation snapshot).
- Loading reimports asset sources that still exist on disk (SMD/FBX/GR2
  via the normal import path, live session data is never clobbered);
  a workspace whose sources are gone fails honestly and keeps the
  current session instead of blanking the viewport.
- Autosave every 5 minutes (configurable, `0` = off) to
  `projects/autosave.m2rig`; on crash the next launch offers recovery,
  on a clean exit the app simply restarts on the sample armor.

## Packaging

Create a portable executable package after a release build:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 -Preset windows-release
```

The ZIP contains the desktop app, CLI and documentation. Game samples,
Noesis and grnreader98 are not bundled (they are git-ignored local
binaries, so a fresh clone does not have them); point the bridge at your
local copies when using the FBX/GR2 workflow.

## Validation Rules

Errors block export, warnings do not.

- Errors: empty mesh/skeleton, bad indices or submesh ranges, NaN/Inf or
  negative weights, unknown bone ids, >4 influences, bad parents/roots
- Warnings: unnormalized weights (tolerance 1e-2), socket bones carrying
  deform weights, over-budget triangle counts, non-manifold edges,
  isolated vertices
- Socket bones (`equip_left/right`, `stip`, `saddle` on mounts) are
  protected by opt-in locks (`Lock sockets`, per-bone `[L]` checkbox;
  locks are per-asset and persist by bone name in `.m2rig`), not by
  export enforcement
- Topology census on every validation (`MESH_TOPOLOGY` info line:
  boundary / manifold / non-manifold edges, isolated verts)

## Known Limitations

- GR2 native parsing not implemented (bridge only: grnreader98 primary,
  Noesis fallback; native GR2 emit is `NOT_SUPPORTED_DIRECTLY`)
- Deformation preview is GPU skinning (LBS palette, per-frame deform with
  no mesh re-upload) over the timeline transport (play/pause/step); the
  CPU deform path stays as fallback and a dual-quaternion (DQS) preview
  toggle; DQS on GPU is future work
- `.mse` / `.mde` parse + validate + CLI (`m2rig_cli validate-mse`) are
  wired; the viewport effects overlay is roadmap. No native `.ani` reader
  yet (AniDocument is export-only; roadmap: prove-or-retire)
- Dock layout persists in `config/imgui.ini` (delete it to restore defaults)
