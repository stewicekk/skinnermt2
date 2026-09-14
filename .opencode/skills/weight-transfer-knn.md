# Skill: Weight Transfer kNN (concepts + UI)

## Algorithm (same engine as `weight-transfer-engine`)
For each target vertex: k-nearest source vertices (k=3) ->
inverse-distance weights -> bone remap by name -> keep top-4 ->
renormalize (sum 1.0) -> mass reported.

## UI path
1. Import source model (SMD direct, FBX/GR2 via bridge).
2. Import target model.
3. Weights -> Transfer from... -> Quick (direct) or Self-train (remap
   optimizer with confidence report).
4. Validate -> export SMD/MSM (gate blocks on errors).

## Rules
- Destination locked bones keep their weights (restored post-transfer).
- Unmapped source bones are reported (`verticesUnmapped`), never invented.
- No GR2 export: results go to SMD/MSM; GR2 is bridge-only.

Legacy note: `RigApp.exe` OBJ/SMD dropdowns and GR2 export buttons do not
exist here.
