# Brain — Metin2 Rigging Studio

Single source of truth: `docs/AGENT_STATE.md` (waves 1-29 + UI S1-S4; Wave 15 fix pass in `docs/CHANGELOG.md` Unreleased).
Root `AGENT_STATE.md` (if present) is an untracked duplicate — do not treat it as authoritative.

## Verified decisions (see code, not guesses)
- Core is zero-third-party (`m2rig_core`); exe links pinned ImGui (`367b2c2`) + ImGuizmo (`18cef5e0`) + OpenFBX (`4d4a45a0`).
- Error model `Result<T>` (`ok`/`fail`, `succeeded()`); member `ok()` is deleted.
- `<=4` influences/vertex enforced at paint/transfer/repair/export; removed mass always reported.
- GR2/FBX via bridge only (`NOT_SUPPORTED_DIRECTLY` for native GR2 emit); grnreader98 primary for GR2, native OpenFBX first for FBX, Noesis fallback.
- Viewport `NoBackground` is load-bearing; Frame CB is 128 B with cached lighting half.
- Gizmo WORLD with `inverseGeneral` + scale-strip for Rotate; lock-checked before undo.
- `.m2rig` stores metadata + locks by name; mesh weights are session data.

Last verified: 2026-09-23 (release: 162/162 cases incl. quat/compression/glTF + 15/15 suites green; debug 162/162 + 15/15 green), v0.10.0 + Unreleased Waves 15-29 (earlier waves: see AGENT_STATE; Wave 24: GPU skinning LBS; Wave 25a: PBR punctual backend; Wave 25b: IBL irradiance + prefilter + BRDF LUT; Wave 26: KD-tree spatial index + UI unification slice; Wave 27: DDS BC1-5 multi-mip + renderer Slice C; Wave 28: quaternion sampling + compression, .ani export-only pinned; Wave 29: bridge closeout + FBX dedup + glTF 29a import; UI S1-S4 honesty gates + toolbar/menu/keyboard/dead-sweep. Steps 1-4: folder-dialog Unicode fix + utf8 core module + BENCHMARKS.md baseline + bridge hygiene + triage flag).
