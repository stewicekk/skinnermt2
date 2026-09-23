---
name: build-ci
description: CMake, presets, CI matrix, test harness scale-out, perf budgets, security (ongoing)
---

# build-ci

Keeps the build honest and fast while features expand. Rule: no wave may
lengthen clean configure+build+test beyond the recorded budget, and every
wave must leave both presets green.

## Current truth

- Floor CMake 3.21 (`CMakeLists:1`), presets schema v6, only
  `windows-debug/release` (VS2022 x64); MSVC-only flags (`:19-21`);
  `/W4` + conditional `/WX` first-party, `/W3 /WX-` third-party
  (`:51-56,81,200,227`) — load-bearing, keep.
- FetchContent pins ImGui `367b2c2` + ImGuizmo `18cef5e0` + OpenFBX
  `4d4a45a0` (`:62-63,178,206`), no `URL_HASH`/offline/SBOM; `_deps`
  staleness = manual delete; 0x vcpkg/conan.
- `m2rig_core` STATIC 25 TUs, zero third-party; `m2rig_fbx` adapter;
  tests single `m2rig_tests` binary (9 TUs, 100 cases) + 8 `cli-*` CTests
  (`:98-170`); `fbx2smd` build-only; `gr22smd/orient/learn-*` 0 CTest.
- Debug full-run >30 min: intentional CRT heap-check triage
  (`test_main.cpp:15-16,29-33`, "REVERT after triage"); release 9/9
  green; debug CLI 8/8 green; debug mega-binary not claimed green.
- CI `windows-2022`, 2 jobs, no cache/shard/retry/artifacts/SBOM/sign
  (`.github/workflows/ci-windows.yml:1-18`); PDB-lock documented only.
- `package-windows.ps1` no versioned name/sha-sidecar/symbols; `dist/`
  not ignored. `Result<T>` model (`result.hpp:29` deleted `ok()`) solid;
  no `[[nodiscard]]`; validation no stable codes/SARIF; logger no
  rotation. 0x sanitizer/fuzz/bench; `BENCHMARKS.md` missing.

## Target contract

- Floor 3.28, schema v8+, Ninja + VS presets, `windows-2025` +
  `ubuntu-24.04-headless` (core+CLI+null-RHI after RHI wave), ccache,
  `ctest --parallel --timeout`, artifacts (ZIP+logs).
- vcpkg manifest (or CPM+`URL_HASH`) + SBOM + Dependabot rev-bumps.
- `M2RIG_HEAP_TRIAGE` option default OFF (fast path <2 min), nightly
  full-heap job; suite-per-CTest sharding; wire `gr22smd/orient/fbx2smd`
  + self-learn verbs; `fbx2smd` ctest suite.
- ASan/UBSan job; libFuzzer targets on `smd/msm/mse/dds` with seed corpus
  from `tests/data/*`; streaming budget caps; SARIF validation export.
- `tracy` opt-in + `ctest -L perf` budgets (ninja LOD 3208->1603 0.14 s
  baseline); AVX2 kNN/deform behind flag with before/after numbers.
- `Result` + `[[nodiscard]]` + `and_then` (hard-rule intact); packaging
  with versioned name + `.sha256` + symbols + manifest.

## Entry points

- `CMakeLists.txt`, `CMakePresets.json`, `.github/workflows/*`,
  `scripts/package-windows.ps1`, `tests/expect.hpp + test_*.cpp`,
  `tools/cli/main.cpp`, `diagnostics.hpp` limits.

## Test gate (this skill's own)

- `ci_matrix` (win dbg+rel + linux headless) green; `docs-check` green;
  `perf_budgets` hold; clean `git status` after packaging (`dist/`
  ignored).

## Failure handling (failure protocol, binding)

Critical build/test fail -> STOP feature expansion, reproduce, isolate
root cause, fix, rerun focused tests, then regression validation. Never
two MSBuilds on one build dir (PDB lock); `/W4 /WX /FS` clean always.
