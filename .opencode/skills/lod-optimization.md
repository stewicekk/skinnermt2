# Skill: LOD Optimization (proposal)

STATUS: proposal only — no decimation code exists in `src/` (honest audit:
mesh has no adjacency/half-edge structures yet, and costume meshes are
near-certainly non-manifold, so naive decimation would crack seams).

## What exists instead
- Triangle budget report per submesh + total (`MESH_BUDGET`, 10k hero
  guideline) in `src/mesh.cpp`.
- Weld-cell near-duplicate report (`MESH_NEAR_DUP`, report-only).
- Submesh isolation checkboxes (viewport-only culling).

## Proposal (if implemented)
1. Build edge-hash adjacency first (open-edge/non-manifold detection).
2. Quadric-error-metric edge collapse, skinning-aware (blend ≤4 +
   renormalize + mass report), material-split respecting.
3. Preview + compare in viewport before applying; undoable via
   `App::pushUndoSnapshot`.

Legacy note: C# `LodGenerator` / QEM / atlas code does not exist here.
