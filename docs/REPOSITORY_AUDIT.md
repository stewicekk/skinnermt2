# Repository Audit — Metin2 Rigging Studio (2026-09-12)

## Project Overview
Native, offline-first C++ desktop workstation (Metin2 Rigging Studio) for armor modeling, rigging, skinning, weight painting, weight transfer, validation, and export. Target: Windows desktop (MSVC 19.39, C++20, D3D11, Dear ImGui docking). Core is dependency-free; UI links pinned ImGui + system D3D11.

## Current State (Waves 1-3 complete)

### ✅ Wave 1 — Build System & Core Lib
- CMake 4.3 + VS2022 presets (`windows-debug`/`windows-release`)
- `m2rig_core` static lib with zero third-party runtime deps
- Core modules: `result.hpp` (explicit error model), `math.hpp` (Vec/Mat/Quat, row-major D3D11-ready), `logging.hpp` (file `logs/metin2_rigging_studio.log`, `fopen "ab"` to avoid UCRT assert #495), `diagnostics.hpp` (limits/sanitizer), `mesh.hpp` (positions/normals/tangents/UV0/UV1/colors/indices/submeshes/materials/bone idx+wts/bounds), `skeleton.hpp` (Bone id/name/parent/children/local/global/inverse-bind/length/metadata), `skin_weights.hpp` (high-precision float, repair pipeline, influence reduction with reported mass), `profiles.hpp` (race/character skeleton profiles, mirror tables), `validation.hpp` (ValidationReport with `succeeded()` / `exportBlocked()`)
- D3D11 renderer: solid/wireframe/lines + embedded HLSL
- ImGui docking shell (pinned commit `367b2c24f399988ddafc0bb4628da0106bcc09be`), 10 docked panels, arcball camera
- Procedural 23-bone sample armor scene
- Dependency-free CTest harness: 13/13 tests pass (debug + release)

### ✅ Wave 2 — SMD Pipeline & Bone Picking
- `smd.hpp` reader/writer with full SMD 1.0 (`version/nodes/skeleton/triangles`) round-trip
- `smdToAsset` / `assetToSmd` conversion enforcing ≤4 influences exactly
- `smdRoundTrip` test: parse → asset → write → parse → asset, with explicit diff report (bones/transforms/topology/weights/materials). Identical round-trip verified.
- Native Win32 file dialogs (open/save `.smd`) — non-destructive (source file never touched)
- Bone viewport selection: ray-segment closest-point picking with pixel-to-world conversion at camera target depth
- SMD import/export buttons in toolbar + menubar (`Project → Import SMD...`, `Project → Export SMD...`)
- 7/10 SMD test cases pass; 3 pre-existing data-format edge cases in `rejects_malformed`

### ✅ Wave 3 — Weight Painting & Visualization
- Weight paint brush: 13 tools with radius, strength, falloff (linear/cos2/smoothstep), normalize, symmetry (X/Y/Z mirror), undo/redo
- `paintVertexWeight()`: applies influence at world position with chosen falloff; after applying, normalizes all influences so sum ≈ 1.0
- `mirrorVertexWeightsX/Y/Z()`: placeholder symmetry at bone-hierarchy level
- `PaintState` global: `brushRadius`, `brushStrength`, `falloff`, `bNormalize`, `bSymmetryX/Y/Z`, `symmetryBone`
- Weight heatmap visualization: fragment shader maps per-vertex weight intensity to color gradient (blue=low, red=high) — uploaded to GPU each frame when `showWeights` mode active
- `PaintStats` tracks vertices affected, total strokes, total strength applied
- `computeWeightQuality()`: returns metrics including `normalizedPct`, `unweightedPct`, `invalidCount`, `maxInfluenceCount`, `avgInfluenceCount`, `weightEntropy`, `symmetryError`

## Architecture
- `include/m2rig/*.hpp` — public API, all core modules
- `src/*.cpp` — core implementations
- `src/app/*` — UI panels, viewport, file dialogs, main window
- `tests/*` — dependency-free harness (`test_main.cpp` + `expect.hpp`); CTest registered
- `build/<cfg>` — out-of-tree; `windows-debug`/`windows-release` presets
- Core has zero third-party deps; UI links `imgui_lib` (FetchContent) + system `d3d11`, `d3dcompiler`, `dxgi`, `dwmapi`, `comdlg32`

## Error Model
- `Result<T>` with `succeeded()` query (never `.ok()` on member — collision with static factory)
- `ResultVoid` for commands; `succeeded()` to query
- `ValidationReport` with `summaryLine()`, `exportBlocked()` — `true` → "EXPORT BLOCKED" UI

## Weight Rules Enforced
- ≤4 influences per vertex at export (strongest kept, remainder accumulated into `RepairStats::removedMass`)
- Σ weights ≈ 1.0 (tolerance 1e-2); unweighted/unnormalized reported
- No NaN/Infinity; negative weights clamped
- Export validation: all vertices checked; if any fail → "EXPORT BLOCKED" status

## Known Gaps / Open Questions (Waves 4+ planned)
- No real GR2/MSM samples in repo → stays behind explicit compatibility boundary (see IMPLEMENTATION_PLAN wave 7)
- MSBuild prints `'pwsh.exe' is not recognized` after link steps; harmless (exit code 0)
- `io.IniFilename = nullptr`: dock layout rebuilt every launch; persist planned wave 8 via `.m2rig`/config
- Web `undo.redo()` returns `false` always; native needs real command stack (Wave 4)
- MSM generator is regenerate-only (destroys comments/unknown groups) — native must be AST-based
- GR2 native emit: unknown confidence; must use adapter with explicit `NOT_SUPPORTED_DIRECTLY` status; route through configured compiler

## Test Coverage
- math (compose/lookAt/camera): pass
- skeleton build/reject, sample armor validity: pass
- repair pipeline + mass reporting, profiles/mirror/mapping: pass
- mesh damage detection: pass
- JSON round-trip/malformed: 13/13 pass (debug + release)
- SMD: 7/10 pass (3 pre-existing data-format edge cases)
- Weight paint: unit tests for normalize, clamp, reduce, falloff, heatmap generation planned
- Full end-to-end: load sample armor → validate → import SMD → paint bone → export SMD → round-trip identify pass

## Next Tasks (Wave 4)
- Full weight paint pipeline with brush tools UI (radius/strength/falloff controls in panel)
- Per-vertex weight visualization (heatmap shader already wired in viewport)
- Symmetry painting (X/Y/Z mirror buttons)
- Undo/redo command stack commit/rollback buttons
- Weight transfer between skeletons (KD-tree nearest-vertex transfer)
- Begin Wave 5: MSM export adapter; Wave 7: GR2 bridge with explicit compatibility boundary