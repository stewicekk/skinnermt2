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
- FBX pin `4d4a45a0` (`CMakeLists:62-63`); no anim import (`:419`); soup
  no-dedup (`fbx_reader.cpp:325-326`); materials name-only (`:243-263`);
  Kaydara/Wolfman fall to Noesis; axis trust-but-verify 2x cost (`:348-418`).
- GR2 bridge-only; grnreader98 hardcoded path+flags (`gr2_adapter.cpp:222-240,
  :281`); Noesis shared-tmp race (`app_state.cpp:1337-1338`); Noesis GR2
  dead (no `granny2.dll`); `extractWeightsFromGr2ViaBridge` stub (`:103-119`);
  `gr2_deep_parser.cpp` scaffold outside all import paths (rewrite, not extend).
- MSM export-only; indent-dialect bet (`msm_ast.hpp:53-54`); `bool` parse
  (`msm_ast.cpp:197-214`); `rawText` never populated.
- MSE parse+6 tests+`validate-mse` CLI exist; UI/viewport wiring missing;
  `MseRuntime::update` 0 callers; `USER_GUIDE:205` stale for MSE.
- Coordsys Wave-22 pinned (`M^-1RM`, frames too, `orient` gate); XUp
  explicit-fail + detect-nullopt by design; `ZUp_YBackward` winding gap.
- CLI 16 verbs / header lists 7 (`main.cpp:2` stale) / CTest 8.

## Target contract

- Bridge hygiene: unique Noesis tmp; `-a -t` documented; stub renamed or
  implemented; header + guide refreshed; missing ctests.
- Native GR2 (clean-room from `29DE6CC0` hex + `grnreader98 -a` ground
  truth; golden bone-by-bone) -> primary; grnreader98 fallback; NEVER link
  `granny2.dll`; emit stays `NOT_SUPPORTED_DIRECTLY`.
- FBX: anim channels + dedup + textures/PBR + pin refresh policy, or
  ufbxi swap (FBX SDK rejected without Autodesk-exact requirement).
- glTF 2.0 via `cgltf`: indexed meshes, <=4 JOINTS/WEIGHTS + inverse-bind,
  quaternion samplers, Y-up (retires arbitration), PBR materials;
  `gltf2smd/smd2gltf` CLI.
- meshopt first (single-header), Draco on demand; `msm2smd` + braces
  authority + `Result` parse; MSE Effects tab + `update` wiring + real
  golden; `.ani` prove-or-retire; USD read-only preview (tinyusd) last.

## Entry points

- `parseSmd/writeSmd/diffSmd`, `FbxReader`, `gr2_adapter.*`,
  `bridge_process.*`, `Gr2DeepParser` (scaffold), `parseMsm/buildMsmExport/
  validateMsmDoc`, `parseMse/MseRuntime`, `applyConversionProfile/
  diagnoseOrientation`, `tools/cli/main.cpp` verbs.

## Test gate

- `gr2_golden_warrior_m`, `gltf_roundtrip_ninja`, `fbx2smd` ctest suite,
  `msm_brace_nesting`, `mse_golden_real`, `ani_roundtrip_or_deleted`.

## Failure handling

Game-client formats stay honest: explicit `NOT_SUPPORTED_DIRECTLY`,
never silent identity conversions (XUp pattern), `orient` SANE gate on
imported rigs. Frozen: Y-up canonical, `M^-1RM`, `local*parent`.
