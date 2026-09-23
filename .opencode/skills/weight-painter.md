# Skill: Weight Painter (ImGui)

Native weight painting (`src/app/panels.cpp` `drawWeightPanel`).

## Brush model (`App::paintStroke`, `src/app_state.cpp`)
- Target = selected bone; modes Add/Subtract/Smooth/Normalize/Blur/Sharpen
  (`BrushMode`); radius/strength sliders; falloff Linear/Cos2/Smoothstep
  (`PaintFalloff`); always clamps to 4 influences + renormalizes.
- Interaction: enable Paint, Ctrl+drag on the mesh (ray-triangle hit =
  brush center); symmetry second pass paints the mirrored bone when the
  profile mirror map resolves it.
- Locked target bone blocks the stroke with a warning; undoable.

## Visualization
- Weights view mode = per-bone heatmap (blue->red) + legend in the panel.
- Quality readout: normalized %, unweighted %, invalid, max/avg influences.

## Flood / Prune / Auto-rig
- `floodBone` (rigid bind to one bone) + `pruneBone` (remove one bone),
  Bone-panel buttons, undoable, lock-aware, mass-reported.
- `autoRigMesh` (nearest segment 1/(d+eps)^2, topN, deterministic tie-break
  by bone id) + `App::autoRigFromSkeleton` (empty guards, undo, locked
  restore, validation); Weights-panel "Auto-rig from skeleton" button.
- Deformed Height parity: `buildGpuVerticesDeformed` supports Height ramp
  from deformed Y (regression `deformed_height_matches_solid_at_bind`).

Legacy note: WPF `WeightPainter.xaml` + ViewModel do not exist here.
