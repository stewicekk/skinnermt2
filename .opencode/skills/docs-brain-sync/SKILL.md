---
name: docs-brain-sync
description: AGENT_STATE truth, guide truth-passes, skill contracts, benchmarks (ongoing)
---

# docs-brain-sync

Single source of truth is `docs/AGENT_STATE.md`. Everything else
(`brain/README.md`, `USER_GUIDE.md`, skills, `MASTER_PROMPT.md`, CLI
header) is derived and must be linted against it in CI. Manual sync
caused all seven documented drifts — this skill ends that era.

## Current truth (drift table, all verified)

| # | Code truth | Doc claims | Fix owner |
|---|-----------|-----------|-----------|
| 1 | `GpuVertex` 72 B (`renderer.hpp:36`) | 48 B (`rendering/SKILL.md:14`) | FIXED in this program (skill refreshed) |
| 2 | CPU DQS preview exists (`app.hpp:107`) | "future work" (`USER_GUIDE:203-204`) | guide truth-pass |
| 3 | MSE parse+CLI wired | "No readers yet" (`USER_GUIDE:205`) | split mse-ok vs ani-roadmap |
| 4 | CLI 16 verbs (`main.cpp:37-56,742-760`) | 7 (`main.cpp:2` header) | sync header from `usage()` |
| 5 | Wave-23, 626 lines, 100/100+8/8 | "waves 1-14, 94/94" (`brain/README:3,15`) | bump pointer |
| 6 | 79 skill entries, stale `C:\rigapp` guard | "25 rewritten" (`AGENT_STATE:223-225`) | skill audit |
| 7 | "100/100" (`AGENT_STATE:561`) | "86/86" (`MASTER_PROMPT:3`) | counts from CTest |

Also: `BENCHMARKS.md` missing (only `benchmark.md` placeholder);
`benchmarking/SKILL.md` template-only; sync step human-only
(`brain-management/SKILL.md:9`); no skill front-matter validator.

## Target contract

- `docs-check` CI job: `sizeof(GpuVertex)` extracted to skill text;
  `usage()` -> `cli-reference.md` codegen (kills drift #4 class);
  `brain/README` vs `AGENT_STATE` header lint (kills #5 class);
  skill front-matter validator (`name` + `description` + contract +
  entry-points + failure-handling sections required).
- Release truth-pass checklist: guide vs code (DQS/MSE/`.ani`/dock-persist
  lines), changelog `Unreleased` section, version single-source
  (`CMakeLists VERSION` -> `M2RIG_VERSION` -> About + `--version`).
- `brain/BENCHMARKS.md` with per-wave numbers (fps, ms, MB, %); every
  perf claim cites binary+preset+asset (no "measured-by-construction").
- Skill audit: archive legacy `*.md` (79 entries), promote the 5 native
  guides + 10 new program skills, delete `C:\rigapp/dotnet/182` refs.
- New skills created in this program: `pbr-rendering`, `gpu-skinning`,
  `modern-textures`, `quaternion-anim`, `spatial-index`, `gltf-pipeline`,
  `viewport-ux`, `workspace-restore`, `build-ci`, `docs-brain-sync`
  (this file). Index them in `.opencode/skills/docs-skills.md`.

## Entry points

- `docs/AGENT_STATE.md`, `docs/CHANGELOG.md`, `docs/DEPENDENCIES.md`,
  `docs/USER_GUIDE.md`, `docs/MASTER_PROMPT.md`,
  `docs/UPGRADE_10X_BRAINSTORM.md`,
  `docs/SPECIALIZED_AGENTS_AND_SKILLS.md`, `brain/README.md`,
  `brain/BENCHMARKS.md`, `.opencode/skills/*/SKILL.md`.

## Test gate

- `docs-check` green on every PR touching `src/`, `include/`,
  `tools/cli` or any `SKILL.md`.

## Failure handling

Docs never override code: on conflict, code wins and the doc is fixed
the same day (the Wave-23 Front/Back revert is the model — evidence
beats proposals). Never silently drop out-of-scope items: document with
file:line like Wave-18 did.
