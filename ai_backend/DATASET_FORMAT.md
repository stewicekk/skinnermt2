Dataset format (JSON snapshots):
Each JSON file represents one mesh snapshot and contains:
{
  "vertices": [[x,y,z], [x,y,z], ...],
  "weights": [[w_b0,w_b1,...], ...]  -- same number of rows as vertices
}
Notes:
- weights rows must sum to 1 (or will be normalized during training).
- If different files have different bone counts, train_pipeline will pad/truncate to `--max_bones`.
- Prepare many snapshots (different meshes, poses, variations) to generalize the model.
Examples/convertors should be prepared based on your exporter;
the exporter in the package can be adapted to write this format.
