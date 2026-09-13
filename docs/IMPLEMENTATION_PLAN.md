# Implementation Plan — Native Metin2 Rigging Studio

Follows `docs/REPOSITORY_AUDIT.md` and Master Specification §§60, 90–100.

## Phase 0 — Audit (done)

`docs/REPOSITORY_AUDIT.md` + this plan + compatibility matrix.

## Wave 1 — Build system + shell + canonical core (Priority 0–1, §92)

- `/CMakeLists.txt` + `CMakePresets.json` (`windows-debug`, `windows-release`, VS 2022 generator, C++20, `/W4 /WX`-adjacent warnings, portable layout).
- `include/m2rig/` + `src/`: `result.hpp` (explicit error model), `math.hpp` (Vec/Mat/Quat, row-major, D3D11-ready), `logging.hpp` (TRACE..FATAL, file `logs/metin2_rigging_studio.log` + in-app ring), `mesh.hpp` (positions/normals/tangents/UV0/UV1/colors/indices/submeshes/material refs/bone idx+wts/bounds), `skeleton.hpp` (Bone id/name/parent/children/local/global/inverse-bind/length/metadata; exact-name preservation), `skin_weights.hpp` (high-precision float, repair pipeline, influence reduction with reported mass), `profiles.hpp` (race/character skeleton profiles, mirror tables), `diagnostics.hpp` (limits, sanitizer).
- `src/app/`: Win32 + D3D11 + ImGui docking shell, dark theme, layout Top/Left/Center/Right/Bottom, all panels present (some show real data as modules land).
- `tests/`: dependency-free harness (`test_main.cpp` + `expect.hpp`), CTest registration.
- Build + run + logs verified on MSVC.

## Wave 2 — SMD + skeleton viewer (Priority 2, 4; §93)

- `smd.hpp/.cpp`: `SmdReader` (nodes with quoted names, skeleton multi-`time`, triangles with parent/normal/uv/linkcount+links, materials), `SmdWriter` (byte-identical-stable formatting, 6 decimals), `smdRoundTrip()` comparator reporting diffs.
- Skeleton hierarchy build, global/inverse-bind computation, bone viewport rendering (line/octahedron), selection, properties.

## Wave 3 — Weight paint + undo (Priority 5–6; §94)

- `weight_paint.hpp`: 13 tools (ADD..GRADIENT), brush props (radius/strength/falloff/pressure/depth/symmetry/connected/front-face/occlusion/bone-restriction), vertex/face/connected modes.
- `commands.hpp`: full command stack (all §32 commands), Ctrl+Z/Y, history panel.
- `symmetry.hpp`: X/Y/Z, weights/geometry/selection/pose mirroring, mirror-bone table, center-line handling.

## Wave 4 — Transfer engine (Priority 7; §95)

- `kdtree.hpp` (header-only, iterative, k-NN + radius), `geometry.hpp` (closest-point-on-triangle, barycentric, raycast), `weight_transfer.hpp`: Methods A–G + AUTO BEST pipeline (§13) + per-vertex confidence + preview (source/target/compare/apply/cancel + threshold) + TRANSFER REPORT (§15).

## Wave 5 — MSM (Priority 3; §96)

- `msm.hpp`: lexer preserving comments/whitespace/EOL, AST (`Group/ShapeData/ShapeIndex/Model/SourceSkin/unknown`), tolerant parser, AST-preserving editor, serializer, `ShapeDataCount` repair only when confidently derivable, validator.

## Wave 6 — Animation + skinning + deformation (Priority 8; §97)

- `animation.hpp`: timeline model (frame/FPS/loop/scrub), skeleton + mesh-deformation preview (CPU skinning reference + GPU palette path).
- Renderer: D3D11 GPU skinning (`bone palette → skinned pos/normal → material → PS`), CPU fallback toggle.
- `deformation.hpp`: pose suite (T/A/idle/walk/run/attack/damage/death/skill), stretch/collapse/explode/detach/clip detectors, DEFORMATION SCORE + issue map.

## Wave 7 — GR2 adapter (Priority 9; §98)

- `gr2.hpp`: `Gr2Probe` (magic/version/size/metadata without over-claiming), `Metin2GR2Reader/Writer` adapters mapping only verified structures to canonical model, `Gr2ExportBackend` (native-direct vs `GRANNY_COMPILER` bridge vs NOT_SUPPORTED_DIRECTLY), golden-manifest tests.

## Wave 8 — Project/validation/batch (Priority 10–11; §99)

- `project.hpp` (`.m2rig` versioned JSON schema v1 + migrations), autosave/crash-recovery/dirty-tracking, snapshots A/B/C + compare/restore, `dependency_graph.hpp` (GR2→skeleton→MSM→DDS→item refs, missing/broken/duplicate detection), `path_profiles.hpp` (pc/pc2/root/locale/…, no hardcoded layout), `validation.hpp` (central engine: 10 categories × 4 severities, id/category/severity/message/asset/location/repairAvailable), `repair.hpp` (preview-first safe repairs), `project_scanner.hpp` (client dir scan, read-only index).
- `tools/cli` (`metin2-rigging-cli.exe`): `validate`, `transfer`, `export`, `batch pipeline.json` (+ `--dry-run`), same core as GUI.

## Wave 9 — AI-optional + diagnostics + release (§100 + §81)

- `ai_bridge.hpp`: local-backend client (mirrors `/ai_skin`, `/api/v1/learn-weights` contracts), suggestion→validate→confidence→preview→approve→apply; deterministic fallback always available; no cloud requirement; ONNX hook point documented, not required.
- `docs/`: all 13 spec docs + CHANGELOG + DEVELOPMENT + AGENT_STATE, describing implemented behavior only.
- Release checklist execution: clean configure + build (debug+release) + full CTest + sample round-trips + packaged portable layout verification.

## Test strategy (§47)

Unit (math/weights/kdtree/msm-ast/smd-sections) → integration (smd round-trip, msm edit round-trip, transfer source→target→reimport compare) → compatibility (golden manifest when samples present, else procedural + JSON-snapshot fixtures) → performance (large-mesh transfer timing budget, viewport 60 FPS target) → corrupted-file fuzz (truncated/over-count/NaN guards).
