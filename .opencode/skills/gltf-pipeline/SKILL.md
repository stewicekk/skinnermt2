---
name: gltf-pipeline
description: glTF 2.0 interchange, native GR2 reader, FBX hardening, bridge hygiene (Wave 29)
---

# gltf-pipeline

One pipeline wave covering every format gap. Order is load-bearing:
hygiene -> GR2 reader -> FBX -> glTF -> compression -> MSM inverse ->
MSE wiring -> `.ani` verdict -> coordsys cleanup -> USD preview.

## Current truth

- SMD strongest: `Version 1` tolerance (`smd.cpp:199-206`), rigid
  fallback (`:511-517`), sparse-id remap (`:471-488`, `:604-615`), xref
  hard errors (`:303-342`), <=4 writer gate (`:437-464`), round-trip diff
  (`:640-717`). Debt: `snprintf`-key dedup (`:540-552`), whole-file fail
  on 1 bad float (`:135-147`).
- FBX pin `4d4a45a0`; no anim import; exact-hash indexed dedup landed
  (ninja 3208 tris / 90 bones, RepairStats surfaced); materials
  name-only; Kaydara/Wolfman fall to Noesis; axis trust-but-verify.
- GR2 bridge-only; grnreader98 hardcoded path+flags; Noesis tmp race
  FIXED (Steps 3-4: PID+counter+stem); per-call log + extractor hygiene
  + fail-closed imports (Wave 29 closeout); Noesis GR2 dead;
  `extractWeightsFromGr2ViaBridge` explicit-unsupported probe;
  `gr2_deep_parser.cpp` scaffold (rewrite, not extend).
- MSM export + `msmToSmd` geometry-shell core (bones/binds/materials,
  triangles empty by honesty, tested); NO `msm2smd` CLI verb (a gated
  verb could never exit 0 — deferred to MDE geometry).
- MSE parse+tests+`validate-mse` CLI + Effects tab (open/tree/play/
  scrub, 200-particle overlay tick — first `MseRuntime::update`
  caller); emission stateless/representative by design.
- Coordsys Wave-22 pinned (`M^-1RM`, frames too, `orient` gate); XUp
  explicit-fail; `ZUp_YBackward` winding FIXED (det<0 index swap,
  tested); STATIC_PROP advisory (Warning, non-blocking) for
  <=2-joint + >=1000-vert rigs.
- glTF 29a import + 29b `smd2gltf` emit LANDED (cgltf `85cd6238`,
  `M2RIG_WITH_CGLTF`; `.glb` only; animations/meshopt/draco/morphs
  explicit `NOT_SUPPORTED_YET`).
- CLI 18 verbs / 17 ctest suites (incl. orient-two-bone + 4 missing-
  input negatives).

## Target contract

- Bridge hygiene: unique Noesis tmp + per-call log + extractor
  hygiene + fail-closed imports (all landed); `-a -t` documented;
  missing ctests landed (orient-two-bone + 4 negatives).
- Native GR2: stays OUT OF SCOPE (clean-room months, no spec);
  diagnostics-only future; NEVER link `granny2.dll`; emit stays
  `NOT_SUPPORTED_DIRECTLY`.
- FBX: dedup landed; anim channels + textures/PBR + pin refresh open.
- glTF: import + emit landed; sampler emission (from baked clips),
  `.gltf`+external `.bin`, draco/meshopt decode open.
- `msm2smd` verb: needs MDE geometry source; MSE sim depth;
  `.ani` stays export-only (bytes pinned).

## Entry points

- `parseSmd/writeSmd/diffSmd`, `FbxReader`, `gr2_adapter.*`,
  `bridge_process.*`, `Gr2DeepParser` (scaffold), `parseMsm/buildMsmExport/
  validateMsmDoc`, `parseMse/MseRuntime`, `applyConversionProfile/
  diagnoseOrientation`, `tools/cli/main.cpp` verbs.

## Test gate

- `smd2gltf_roundtrip_sample_armor`, `smd2gltf_rejects_overlimit_
  explicit`, `roundtrip_cube_skin`, `rejects_nonindexed_explicit`,
  `rejects_meshopt_draco_explicit`, `msm_to_smd_shell_*` (3),
  `coordsys_zup_ybackward_winding`, `orient_static_prop_advisory`,
  `resolve_draw_path_routing_matrix`.

## Failure handling

Game-client formats stay honest: explicit `NOT_SUPPORTED_DIRECTLY`,
never silent identity conversions (XUp pattern), `orient` SANE gate on
imported rigs. Frozen: Y-up canonical, `M^-1RM`, `local*parent`.
