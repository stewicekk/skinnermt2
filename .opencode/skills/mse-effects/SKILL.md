---
name: mse-effects
description: MseRuntime::update overlay wiring + bone resolver + real-.mse golden (Effects tab)
---

# mse-effects

Takes MSE from parse-only to viewport-visible: `MseRuntime::update`
overlay wiring, attachment->bone resolver hardening, and a real-`.mse`
golden. Extends `gltf-pipeline` (MSE wiring item, `parseMse/MseRuntime`
entry) + `pbr-rendering` (overlay draw path discipline).

## Current truth

- Parse + CLI wired: `parseMse`/`readMseFile` + `validate-mse`
  (`tools/cli/main.cpp:747`); tests `mse/parse_stringify_roundtrip`
  (`tests/test_mse.cpp:37`), `malformed_input_terminates` (`:64`),
  `mde_rejects_bad_headers` (`:109`), `mde_parses_minimal_valid`
  (`:124`), `mde_insane_counts_fail_explicitly` (`:150`) — all read-verified.
- Runtime struct landed but UNWIRED: `MseRuntime` (`include/m2rig/mse.hpp:131-137`)
  with `update(dt, skel, bonePalette)` + `getActiveParticles()`; per
  `gltf-pipeline` skill, `MseRuntime::update` has 0 callers and there is
  no Effects tab / UI / CLI preview wiring.
- Resolver core exists inside `MseRuntime::update`
  (`src/mse.cpp:333-375`): attachment loop (`:336`), bone lookup by NAME
  via `skel.findByName` with palette bounds check, identity fallback for
  foreign skeletons (`:341-348`) — pinned by
  `mse/runtime_survives_palette_mismatch` (`tests/test_mse.cpp:160`,
  empty + short palettes, never OOB).
- Particles are REPRESENTATIVE-ONLY, honestly admitted: `src/mse.cpp:354-358`
  ("In a real implementation, this would maintain particle state across
  frames / For now, generate a few representative particles");
  `getActiveParticles` (`:377-385`) just concatenates instance lists.
  `MseInstance` carries `boneTransform/time/playing` (`mse.hpp:121-128`)
  but no persistent per-particle state, no loop/accumulator semantics.
- Honest boundary: `docs/USER_GUIDE.md` MSE section is stale for runtime
  (per `gltf-pipeline` skill); game-client emission stays preview-only,
  never a gameplay-behavior claim.

## Target contract

- Overlay wiring: per-frame `MseRuntime::update(dt, skeleton,
  bonePalette)` from the viewport loop + `getActiveParticles()` drawn
  through the `drawLines`/overlay path with `PsFlat`-class passthrough
  discipline (debug/overlay content is never lit — per `rendering` skill).
- Bone resolver hardening: name match -> identity-with-warning on
  unknown bones (never OOB, never silent drop — status line names the
  unresolved attachment); palette-length mismatch keeps the
  `runtime_survives_palette_mismatch` guarantee under per-frame update.
- Real-particle semantics replacing representative-only: persistent
  particle pool, `emitRate*dt` accumulator (no `min(...,50)` burst cap
  as steady state), `lifeTime`/`loop` honored, deterministic under fixed
  `dt` (seeded, no RNG drift between runs).
- Real-`.mse` golden: parse + 1.0 s fixed-`dt` update over a
  `Data/Models` skeleton, particle world positions pinned (identity-safe:
  foreign bones degrade to identity exactly as the mismatch test proves).
- Effects tab (viewport-ux-routed input, overlay-rect hit-test first)
  listing attachments/emitters with play/pause + time scrub.

## Entry points

- `include/m2rig/mse.hpp` (`MseRuntime:131-137`, `MseInstance:121-128`,
  `ParticleOverlay:113-119`), `src/mse.cpp` (`update:333-375`,
  `getActiveParticles:377-385`), `tests/test_mse.cpp` (`:160-179` guard).
- `tools/cli/main.cpp` (`validate-mse` verb at `:747`; future
  `mse-preview` dump verb rides the same CLI plumbing).
- `src/renderer.cpp` overlay draws + `src/app/panels.cpp` viewport loop
  (wiring targets — 0 callers today).

## Test gate

- `mse_golden_real` (real `.mse` + fixed-`dt` particle positions pinned).
- `runtime_survives_palette_mismatch` (exists — stays green under
  per-frame wiring).
- `mse_accumulator_deterministic` (same `dt` stream twice, bit-identical
  overlays; unlooped emitters stop past `lifeTime`).
- `mse_unresolved_bone_warns` (unknown attachment name -> identity +
  named warning, never OOB).

## Failure handling

Overlay draws never regress the lit path: `drawMeshFlat`-style
passthrough only for particles. Wiring lands WITH the golden in the
same wave — no unwired runtime extensions without a caller + test.
