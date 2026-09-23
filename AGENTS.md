# AGENTS.md — Metin2 Rigging Studio (native)

## Mandatory reading order
1. `brain/README.md` — verified decisions + single source of truth (`docs/AGENT_STATE.md`).
2. `docs/AGENT_STATE.md` — wave log, architecture, test coverage (authoritative; root
   `AGENT_STATE.md` duplicates are untracked and must not be treated as truth).
3. Relevant files in `brain/` before major decisions.
4. `.opencode/skills/docs-skills.md` — skills index + conventions.

## Mandatory behavior
- Inspect existing implementation before creating new abstractions.
- Never use fake success, placeholder production functionality, or undocumented assumptions.
- Preserve working behavior and source assets.
- Add regression coverage for changed behavior (`tests/`, `ctest --preset`).
- Report evidence (`file:line`, test output), not guesses.

## Build / test
- `cmake --preset windows-release` + `cmake --build --preset windows-release`
- `ctest --preset windows-release --output-on-failure` (same for `windows-debug`)
- Never run two MSBuilds on one build dir (PDB lock); `/W4 /WX /FS` clean.
## Failure protocol

If a critical build or test fails: stop feature expansion, reproduce, isolate the
root cause, fix it, rerun focused tests, then run regression validation.

## Camera/Viewport Issues Found (2026-09-19) — fixed in Wave 23 (2026-09-21)

| Issue | File | Lines | Fix |
|-------|------|-------|-----|
| Yaw clamp breaks continuous 360° | `camera.hpp` | 216-223 | Fold by full turns via `fmod` only (no visual jump). Pinned by `camera_orbit_clamp_and_pan_scale` + new `camera_orbit_applies_dpi_scale`. |
| DPI scale ignored in orbit/pan/zoom | `camera.hpp` | 193-195, 234-235, 240 | Multiply `dx/dy` by `dpiScaleX/Y` in `orbit()`, `pan()`. Scale `wheel` in `zoom()`. |
| Aspect ratio mismatch on multi-DPI | `panels.cpp` | viewport/box-select/labels | Use logical `avail.x/avail.y` for aspect, not physical `rect.w/rect.h`. (`renderScene` backbuffer path keeps physical pixels.) |
| Overlay buttons block orbit start | `panels.cpp` | 2060ff | Removed `SetNextItemAllowOverlap()`; drag state tracked globally (`btnDown` + viewport-rect hover). |
| Upside-down model (extra) | `panels.cpp` | offscreen `AddImage` | UV `(0,0)->(1,1)` — D3D11 origin is top-left like ImGui; the V-flip inverted every frame. |
| Front/Back swap + grid -Z (reverted) | `camera.hpp:312`, `mesh_views.cpp:185` | — | Solution-judge: character faces +Z (`samples.cpp:31-33`), so Front=`{0,0,1}` was correct; +XYZ triad kept. |
| Top/Bottom exact poles (extra) | `camera.hpp` | 316-317 | 0.001 tilt guard — exact +-pi/2 degenerates `lookAt`. Pinned by new `camera_apply_preset_view_mapping`. |
