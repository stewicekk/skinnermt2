# Skill: Gizmo Manipulators (ImGuizmo)

Native bone manipulators (`src/app/panels.cpp` viewport gizmo block, `include/m2rig/math.hpp`).

## Contract
- ImGuizmo pin `18cef5e0` (sources under `src/`, FetchContent populate-only, `/W3 /WX-`, `M2RIG_WITH_GIZMO`).
- Translate/Rotate/Scale trio, WORLD mode, Op radio in Bone panel (`GizmoOp`).
- World->local via `parent.inverseGeneral * world` (handles scaled parents; `inverseRigid` assumes scale 1).
- Rotate strips scale/shear before `eulerXyzFromRotation` (pure-rotation assumption); Translate never touches euler.
- Scale via parent world-scale lengths, guarded divide (`>1e-9`), no empty snapshots.
- Lock-checked BEFORE `pushUndoSnapshot`; blocked drag sets `wasBlocked`, release reports warning (never fake success).
- Undo snapshot carries weights+pose+scale (cap 50); `rebuildSkeletonRuntime` + `gpuDirty` + `runValidation` on apply.
- Gating: orbit/pick/paint/hover/pan/zoom blocked while `IsUsing` (pan/zoom) or `IsOver/IsUsing` (orbit/pick/paint/hover).

## Entry points
- `drawViewportPanel` gizmo block, `drawBoneProperties` Op radio + lock checkbox + Flood/Prune.
- Math: `Mat4::inverseGeneral`, `Mat4::eulerXyzFromRotation`, `Mat4::rotationEulerXyz` (round-trip tested).
