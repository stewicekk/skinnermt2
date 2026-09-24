# Brain — Metin2 Rigging Studio

Single source of truth: `docs/AGENT_STATE.md` (waves 1-29 + UI + Round A/B; Wave 15 fix pass in `docs/CHANGELOG.md` Unreleased).
Root `AGENT_STATE.md` (if present) is an untracked duplicate — do not treat it as authoritative.

## Verified decisions (see code, not guesses)
- Core is zero-third-party (`m2rig_core`); exe links pinned ImGui (`367b2c2`) + ImGuizmo (`18cef5e0`) + OpenFBX (`4d4a45a0`).
- Error model `Result<T>` (`ok`/`fail`, `succeeded()`); member `ok()` is deleted.
- `<=4` influences/vertex enforced at paint/transfer/repair/export; removed mass always reported.
- GR2/FBX via bridge only (`NOT_SUPPORTED_DIRECTLY` for native GR2 emit); grnreader98 primary for GR2, native OpenFBX first for FBX, Noesis fallback.
- Viewport `NoBackground` is load-bearing; Frame CB is 128 B with cached lighting half.
- Gizmo WORLD with `inverseGeneral` + scale-strip for Rotate; lock-checked before undo.
- `.m2rig` stores metadata + locks by name; mesh weights are session data.

Last verified: 2026-09-24 (release: 170/170 cases + 17/17 suites green; debug 17/17 green), v0.10.0 + Unreleased Waves 15-29 + Round A/B (C2 ranges + normal-map, `smd2gltf` emit, per-submesh structure, MSE tab, msm shell, G4 + `test_app`, weight table v1, layout presets + live reset, winding fix, static-prop advisory; audit GO-WITH-NOTES incorporated).
