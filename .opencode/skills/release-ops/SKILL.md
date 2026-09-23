---
name: release-ops
description: Version single-source, package-windows.ps1 ZIP+SHA, changelog/guide truth-pass gate (verified counts)
---

# release-ops

Release cut checklist: version single-source, portable ZIP+SHA package,
changelog + user-guide truth-pass gate. Extends `build-ci` (presets,
CTest matrix, packaging gaps) + `docs-brain-sync` (truth-pass
checklist, drift classes). Counts below are VERIFIED against
`docs/AGENT_STATE.md:839` on the day of writing — re-verify at cut
time, never copy forward.

## Current truth

- Version single-source: `CMakeLists.txt:4` (`VERSION 0.10.0`) ->
  `M2RIG_VERSION` define (`CMakeLists.txt:148` for `m2rig_cli`, `:267`
  for the app) -> About dialog + `m2rig_cli --version`
  (`tools/cli/main.cpp:741`); `include/m2rig/app.hpp:28-32` documents
  the chain with a `"0.10.0"` fallback when built outside CMake.
- `scripts/package-windows.ps1:1-27` stages exe + CLI + guide into
  `dist/` and zips it (`Compress-Archive` at `:24`); SHA256 via
  `Get-FileHash` at `:25`; only `windows-debug/release` accepted
  (`:7-9`); `Data/Models` + `noesis/` bridges are deliberately NOT
  bundled (`:27`, see `docs/DEPENDENCIES.md`).
- Known packaging gaps (per `build-ci` skill Current truth): no
  versioned archive name, no `.sha256` sidecar file, no symbols, `dist/`
  not ignored — closing these is this skill's Target contract, not
  claimed truth.
- Gate counts VERIFIED (`docs/AGENT_STATE.md:839`): `ctest --preset
  windows-release` **12/12 green**, unit binary **137/137** (release AND
  debug). Note: "139/139" appears NOWHERE in `docs/AGENT_STATE.md`
  (searched) — any doc claiming 139 is drift, fix it per
  `docs-brain-sync` rule. Roadmap context at `:848`: 94 checks + 8 CLI
  suites; CLI has 16 verbs (`tools/cli/main.cpp:745-762`: `validate`,
  `validate-msm`, `validate-mse`, `info`, `smd2smd`, `smd2msm`,
  `autorig`, `lod`, `fbx2smd`, `gr22smd`, `orient`, `learn-from-asset`,
  `learn-from-gr2-dir`, `self-learn-transfer`, `self-learn-autorig`,
  `analyze-gr2-dir`) while the header at `:2` still lists 7 (drift #4).

## Target contract

- Cut gate (all must hold): `cmake --preset windows-release` clean,
  `ctest --preset windows-release --output-on-failure` 12/12 green AND
  unit binary count re-verified from the tail of `docs/AGENT_STATE.md`
  (currently 137/137 — update the number, never assume it).
- Truth-pass gate: `docs/USER_GUIDE.md` vs code (DQS/MSE/`.ani`/
  dock-persist lines per `docs-brain-sync`), `docs/CHANGELOG.md`
  `Unreleased` section drained, `tools/cli/main.cpp:2` header synced
  from `usage()` (16 verbs), no `C:\rigapp/dotnet/182` claims anywhere.
- Packaging: versioned archive name (`Metin2RiggingStudio-<ver>-win64.zip`),
  `.sha256` sidecar next to the ZIP, symbols staged, `dist/` ignored,
  clean `git status` after packaging.
- Honest boundary restated in the release notes: Noesis GR2 import dead
  (missing `granny2.dll`) — grnreader98 primary path; `Data/Models` and
  `noesis/` not bundled.

## Entry points

- `CMakeLists.txt` (`:4` version, `:148/:267` define), `include/m2rig/app.hpp`
  (`:28-32` chain), `tools/cli/main.cpp` (`:741 --version`, `:745-762` verbs).
- `scripts/package-windows.ps1`, `docs/AGENT_STATE.md` (counts),
  `docs/CHANGELOG.md`, `docs/USER_GUIDE.md` (`:181` packaging command),
  `docs/DEPENDENCIES.md` (bundling boundary).

## Test gate

- `release_counts_match_agent_state` (12/12 + re-verified unit count;
  fails on any drifted number, including a stale 139).
- `cli_header_lists_all_verbs` (header count == `usage()` verb count).
- `package_produces_zip_and_sha` (versioned name + `.sha256` sidecar +
  clean `git status`).
- `docs-check` green (per `docs-brain-sync` Test gate) on every release PR.

## Failure handling

A red gate stops the cut — no release with failing tests, drifted
counts, or unbundled-dependency surprises. Fix root cause, rerun focused
tests, then full regression (`ctest --preset windows-release`), then
re-cut. Never two MSBuilds on one build dir (PDB lock); `/W4 /WX /FS`
clean always.
