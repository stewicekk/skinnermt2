---
name: weight-table
description: Virtualized per-vertex weight table over existing set/remove/normalizeVertex + boneHistogram_
---

# weight-table

Per-vertex weight table (virtualized, filter/sort/bulk/CSV) built ONLY
on the existing, tested mutation API — no parallel weight store, no
second renormalization. Extends `viewport-ux` (weight-table v1 item,
input router, selection sets) + `spatial-index` (nearest-vertex pick
routing for table focus).

## Current truth

- Mutation API exists, undoable + lock-aware
  (`include/m2rig/app.hpp:363-367`): `setVertexInfluenceWeight(vertex,
  slot, weight)`, `removeVertexInfluence(vertex, slot)`,
  `normalizeVertexWeights(vertex)` — "<=4 + renormalized, mass never
  silent" per the header comment.
- `normalizeVertexWeights` impl (`src/app_state.cpp:1080-1094`): missing
  asset / OOB vertex fail explicitly (`PAINT`); locked-bone vertices
  FAIL with "unlock to normalize" (never fake-ok); undo snapshot pushed
  BEFORE repair; `repairVertexInfluences(..., kMetin2MaxInfluences)`
  does the <=4 clamp + renormalize.
- Read model: lazy `boneHistogram_` (`include/m2rig/app.hpp:455-458`,
  `boneHistogramDirty_`/`weightQualityDirty_` flags) rebuilt in
  `App::boneInfluenceCount` (`src/app_state.cpp:243-264`: assign zeros,
  count `weight > 0.0f` influences per bone, OOB ids fall to a rare
  direct scan at `:258-264`); pinned by
  `weights/histogram_counts_secondary_influences`
  (`tests/test_weights.cpp:964`).
- Editor today is a single-vertex slider (per `viewport-ux` skill — jump
  to weight 0 on first drag) while multi-select data
  (`app.hpp` selection sets) is real but single-primary. No table, no
  CSV, no bulk edit exist.

## Target contract

- Virtualized table (ImGui `ListClipper`-class windowing — never
  materialize all rows): one row per (vertex, bone) influence on the
  CURRENT asset; columns vertex / bone / weight; weight cell edits route
  to `setVertexInfluenceWeight`, deletes to `removeVertexInfluence`,
  row/bulk "normalize" to `normalizeVertexWeights` — zero new mutation
  paths, every edit undoable + lock-guarded by construction.
- Filter (by bone, by weight epsilon) + sort (vertex, bone, weight) +
  bulk select over `selectedBones`/sets (shared with `workspace-restore`
  multi-bone ops); CSV export/import with `;`-safe bone names and a
  header row; import replays through the same three API calls so locks,
  <=4 clamp and undo behave identically to manual edits.
- Focus sync both ways: picking a vertex (via `spatial-index`-routed
  nearest hit) scrolls the table; selecting table rows highlights the
  vertices in the viewport overlay. Table respects the `viewport-ux`
  input router (overlay-rect hit-test before orbit/pan).
- Performance: histogram reuse (`boneInfluenceCount`, no per-frame
  rescan); table never invalidates `boneHistogramDirty_` itself —
  mutations already do via `noteWeightsChanged`.

## Entry points

- `include/m2rig/app.hpp` (`:363-367` mutations, `:455-458` histogram),
  `src/app_state.cpp` (`:243-264` histogram rebuild,
  `:1080-1094` normalize with lock guard).
- `src/app/panels.cpp` viewport + Weights-panel surface (table host);
  `include/m2rig/spatial.hpp` (`KdTree` nearest-vertex focus queries).
- `Data/Models` real-asset row-count proof (transfer-scale 5040v-class
  mesh stays interactive under the clipper).

## Test gate

- `weight_table_edits_route_to_api` (cell edit == `setVertexInfluenceWeight`
  result incl. undo entry + lock failure path).
- `weight_table_csv_roundtrip` (export -> clear -> import replays to
  identical influences through the same three calls).
- `histogram_counts_secondary_influences` (exists — stays green; table
  reads never bypass the histogram).
- `table_virtualized_row_count` (5040v-class mesh: realized rows bounded,
  full count reported).

## Failure handling

No silent mass change: any bulk op that would drop influence mass fails
or reports per-vertex stats (same discipline as `repairVertexInfluences`
stats). Locked bones FAIL edits, never fake-ok. Table edits share the
paint undo stack until stroke-coalesced undo lands (`workspace-restore`).
