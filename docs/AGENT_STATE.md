# Agent State — Metin2 Rigging Studio (native)

Last updated: 2026-09-13 (waves 1-11, 41/41 checks + 6 CLI suites green,
release clean, v0.9.0).

## Completed systems

- Wave 1: CMake 4.3 + VS2022 presets (windows-debug/release), `m2rig_core`
  static lib (result/math/logging/diagnostics/validation/mesh/skeleton/
  skin_weights/profiles/json/samples/app_state/mesh_views), D3D11 renderer
  (solid/wireframe/lines, embedded HLSL), ImGui docking shell (pinned
  docking commit 367b2c2), 12 panels incl. Toolbar/Weights/Export/Timeline,
  arcball camera, procedural 23-bone sample armor scene, dependency-free
  CTest harness.
- Wave 2: `smd.hpp` reader/writer + round-trip diff + SMD import/export
  wiring (native Win32 file dialogs, Export panel) + bone viewport selection
  (ray-segment closest-point picking) + skeleton frame timeline preview.
  Cross-reference validation added: dangling parent/pose/triangle bone ids
  are hard errors (duplicate ids rejected too).
- Wave 3: real weight paint engine (Add/Subtract/Set/Smooth/Blur/Sharpen/
  Normalize/Prune) with distance x falloff (Linear/Cos2/Smoothstep),
  per-vertex ≤4 clamp + renormalize + dropped-mass accounting; weight
  heatmap viewport mode (blue->red jet ramp per selected bone); paint mode
  in viewport (Ctrl+drag paints on ray-tri hit surface, plain drag orbits).
- Wave 4: transactional Undo/Redo (full influence snapshots, cap 50, Undo/
  Redo buttons + menu) + kNN weight transfer engine (inverse-distance blend
  over k=3 nearest, bone remap by name, repair to ≤4, stats with avgDist).
  Transfer UI: Weights panel "Transfer from..." popup over loaded assets.
- Wave 5: AST `.msm` parser (`Group <Name>` syntax, `//` + `/* */`
  comments, reordered fields, unknown blocks) + honest stringifier +
  real MSM exporter (ShapeData/ShapeIndex/Model/SourceSkin from canonical
  asset). Export MSM button enabled.
- Wave 6: auto-repair pipeline verified (clamp -> merge dups -> sort ->
  reduce -> normalize -> validate), never-silent mass reporting
  (`RepairStats::removedMass`), export gate blocks on errors.
- Wave 7: Granny3D/Noesis bridge with explicit NOT_SUPPORTED_DIRECTLY
  boundary (`exportGr2ViaBridge`, `extractWeightsFromGr2ViaBridge`,
  `isValidGr2Container`, `defaultGr2BridgeConfig`); Universal extractor
  (`SmdWeightExtractor` native + `NoesisBridgeExtractor` for FBX/GR2);
  UI: Import FBX/GR2 (bridge) + Export GR2 (bridge) with honest warnings.
  Verified live: `noesis/Noesis.exe ?cmode Data/Models/ninja.fbx` exports a
  real 90-bone / 2070-vert SMD; `ninja.gr2` reports "Unknown" file type
  (GR2 Python plugins need the absent `granny2.dll`), so GR2 import
  honestly fails over to the sibling-FBX workflow. Bridge probes
  `noesis/Noesis.exe`, `noesis/Noesis64.exe`, legacy `external/noesis/`.
  `isValidGr2Container` accepts V1 `gr2\0` and real V2 `29 DE ..`
  (all 31 Data/Models GR2 are `29de6cc0`, zero ASCII Bip01, so native GR2
  parsing correctly stays out of scope).
- Wave 8: `.m2rig` JSON workspace persistence (`saveWorkspace`/
  `loadWorkspace`, `saveCurrentWorkspace`/`restoreWorkspace`, per-asset
  refs + viewport + brush + validation snapshot); UI: Save/Load workspace
  buttons + menu entries.

## Hardening pass (2026-09-13, hive parallel research)

- Exact Metin2 bones: profiles keep the 23-bone required core and add the
  verified optional set from the real 90-bone dump (Spine2/3, fingers 0-4
  + segments + nubs, toes, ponytail chain, HeadNub, armor sockets
  B_*/R_*/L_*/F_*/Bone*, Tail1/2); finger/toe mirror pairs added;
  `validateSocketDeformUse` warns when equip_*/stip carry deform weights
  (wired into `runValidation`). Real-data test
  `smd.parses_real_ninja_bridge_excerpt` (offline fixture
  `tests/ninja_excerpt.inc`, 90 bones, sockets resolve, no missing core).
- Self-training transfer (`transferWeightsSelfTraining`): deterministic
  coordinate descent over the bone remap minimizing
  J = 0.5*symErr + 0.3*unmapped + 0.15*avgDist/D + 0.05*removedMass, donor
  table built once, per-bone confidence, deterministic order, max 25
  iters; UI offers Quick + Self-train per source asset.
- Modern UI: full dark theme (rounding, spacing, scrollbars, tabs,
  accent), bone search box, weight heatmap legend, paint brush circle
  overlay, export pre-checklist, Undo/Redo correctly disabled via
  canUndo/canRedo, material DDS existence probe (literal path +
  Data/Models basename), viewport clear matches theme.

## Current architecture

- `include/m2rig/*.hpp` public API (+`ast/`, `adapters/`, `extractors/`,
  `workspace/`), `src/*.cpp` core, `src/app/*` exe-only UI
  (`panels.cpp`, `app_gpu.cpp`, `file_dialog.cpp`, `main.cpp`),
  `src/renderer.cpp` D3D11 backend, `src/mesh_views.cpp` dependency-free
  GPU-view builders (Solid/Normals/Height/Weight + grid) shared by core
  tests and the backend. Core has zero third-party deps; exe links pinned
  ImGui + system D3D11. `App::refreshGpu` lives in the exe target so the
  test harness never links D3D translation units.
- Error model: `Result<T>` (`ok()`/`fail()` factories, `succeeded()`
  query, `operator bool`). Member `ok()` is deleted; never reintroduce.
- MSVC notes: `/W4 /WX /FS` clean; never two MSBuilds on one build dir
  (PDB lock); FetchContent `_deps` staleness -> delete
  `build/<cfg>/_deps/imgui-*` + `imgui_lib.dir`; log file `"ab"`
  (UCRT assert #495); `pwsh.exe not recognized` after link is harmless.
- ImGui docking branch + `imgui_internal.h` for DockBuilder.

## Verified format facts

- SMD 1.0 `version/nodes/skeleton/triangles` round-trip identical
  (bones/transforms/topology/weights/materials diffed explicitly).
- ≤4 influences/vertex enforced at paint, transfer, repair and export;
  removed mass always reported, never silent.
- Bip01 family: 23-bone required core, exact names in `samples.hpp`;
  verified optional set (Spine2/3, fingers, toes, ponytail, armor sockets)
  from the live 90-bone Noesis dump in `src/profiles.cpp`.
- MSM: AST parse/stringify round-trip tested; export writes real groups.
- GR2/FBX: bridge-only, explicit status; no native emit claims.
- Workspace `.m2rig`: save/load round-trip tested.

## Known bugs / open questions

- No real GR2 samples in repo -> GR2 stays behind bridge boundary.
- `io.IniFilename = nullptr`: dock layout rebuilt each launch (workspace
  persists selection/state, not ImGui docking itself yet).
- Mesh deformation preview for animation frames is skeleton-only (wave 6
  GPU skinning palette is future work per roadmap waves 11-15).

## Characters pass (2026-09-13, Data/resources research)

- `gr2_srcs/` at repo root is present but EMPTY — nothing to integrate
  from it; the real GR2 toolchain lives in
  `Data/resources/Convert gr2 to mesh/` (grnreader98 + granny2.dll).
- grnreader98 is now the PRIMARY GR2 path (`findGrnReader`,
  `convertGr2ToSmdViaGrnReader`, `App::importBridgedFile` tries it first):
  documented batch syntax `<gr2> -a` (all submeshes + weights + skeleton,
  no prompts), staged copy in temp (source tree stays clean), SMD text
  parsed by the native parser. Verified live on `warrior_m.gr2`
  (37 bones, 7426 tris, face + easter2020 costume submeshes) and
  `sura_m.gr2` (36 costume bone bindings). Parser hardened for its
  dialect (`Version 1` capital-V header tolerated).
- Noesis stays the FBX path (verified) and the GR2 fallback.
- Male/female matrix from `00-BONES` (pc vs pc2 folders): 8 gender
  profiles (`pc_{warrior,assassin,sura,shaman}_{m,f}`, `.max` files are
  3ds Max binaries so gender split is identity-level; the 23-bone core is
  shared) + `pc_mount` (saddle socket per Mounts tutorial: never scale)
  + race fallbacks; `findProfile` tries exact identity before race.
  UI profile combo lists all 14 identities.
- Real-model aliases: `equip_right_hand`/`equip_left_hand` (grnreader
  output) -> `equip_right`/`equip_left`.
- Videos (.mp4): categorized by filename + paired How-to.txt only — video
  content itself was not viewed. Workflow facts used (Physique attach to
  Bip01, per-part vertex assign, mirror, HeadNub for helmet/hair, saddle
  never scaled, umodel->PSK->ActorXImporter for mobs) all come from the
  text docs. `Tools/` and `Training/` are empty directories.
- Live integration test `weights.grnreader_converts_real_gr2` (skips
  honestly when tool/sample absent) + temp-staging cleanup fixed
  (ifstream closed before remove — Windows locking).

## Render/viewport/CLI pass (2026-09-13, hive parallel research)

- CPU skinning preview (render-only, canonical bind data untouched):
  `buildSkinningPalette(skel, bindInverse)` (palette = bindInv * current
  global, row-vector), `deformVertex`/`deformNormal`, `LoadedAsset::
  bindInverse` captured at load, `buildGpuVerticesDeformed`, timeline
  Play/Pause + FPS + Loop + Deform-preview toggle (wall-clock advance).
  Key catch fixed by test: rebuild overwrites inverseBind, so the bind
  snapshot is mandatory (not the live field).
- Viewport views: new UV mode (uv0 + checker), Weights heatmap,
  `showWireOverlay` finally wired (depth-biased `rsWireBias` overlay, no
  z-fight), `xrayBones` toggle (`dsNoDepth` line pass), joint crosses
  (X/Y/Z ticks), hover highlight (orange) + bone-name tooltip, paint
  circle overlay kept.
- ImGuizmo bone manipulators (Translate/Rotate, WORLD): pinned commit
  `18cef5e0` verified via GitHub API after the suggested pin 404'd and
  upstream moved sources to `src/` (FetchContent populate-only, `/W3
  /WX-`, `M2RIG_WITH_GIZMO` switch). World->local via
  `parent.inverseRigid * world`; rotation via own `Mat4::
  eulerXyzFromRotation` (exact inverse of `rotationEulerXyz`, tested
  round-trip); Translate path never touches euler. Gizmo drags push the
  unified undo snapshot (weights + pose); orbit/pick/paint/hover gated
  while `IsOver`/`IsUsing`. Op radio lives in the Bone panel.
- Headless CLI `tools/cli/main.cpp` (`m2rig_cli`: validate/info/smd2smd/
  smd2msm, exit codes 0-4, core only) + `tests/data/two_bone.smd` fixture
  + 4 ctest smoke suites; MSM export logic extracted to shared
  `buildMsmExport` (GUI and CLI emit identically).
- `.github/workflows/ci-windows.yml` (push/PR, VS2022, debug + release
  configure/build/ctest); stale skills rewritten to the native reality
  (`build-system`, `build-rigapp`, `regression-tests`, `run-regression`,
  `validate-models`) + new `cli-reference` skill.

## Wave 10 passes (bridge logs, locks, batch, isolation, budget, status)

- Bridge stdout/stderr capture (`adapters/bridge_process`: inheritable
  temp log, `CREATE_NO_WINDOW`, timeout + terminate, last-200-lines trim):
  grnreader98, Noesis export and Noesis import all stream into the Console
  panel with exit code + last line in the status; "Running bridge..."
  status set before each blocking call.
- Bone locking (`App::lockedBones`, per-asset, cleared on switch):
  paint target/mirror-target blocked with warning, mirror + quick/self-
  train transfer restore locked weights from the undo snapshot, self-train
  optimizer never maps to locked bones, gizmo blocked on locked bones.
  UI: Bone-panel checkbox + Lock sockets/Unlock all, `[L]` tree markers,
  `.m2rig` persistence by bone NAME (tolerant parse).
- Batch export (`exportAllBatch`): all loaded assets to SMD+MSM next to the
  first source file (`batch_export/`, else temp), per-asset OK/FAIL table
  in the Export panel. Sample templates (`loadSampleArmorForProfile`,
  "Sample as..." popup over all 14 identities) for transfer workflows.
- Submesh isolation (`hiddenSubmeshes`, `filterVisibleIndices` in core):
  hierarchy checkboxes filter the GPU upload only; exports/picking stay
  full-mesh.
- Reports: per-submesh + total triangle budget (10k hero guideline,
  `MESH_BUDGET` warning), weld-cell near-duplicate report
  (`MESH_NEAR_DUP` info, report-only, no auto-merge); non-manifold
  detection honestly deferred (needs edge-hash adjacency that does not
  exist yet). System panel: Noesis/grnreader presence, Data/Models
  GR2/FBX/DDS counts, asset + validation totals, bridge boundary note.
- Bug fixed by test: use-after-move (`current = asset.id` after
  `assets[...] = std::move(asset)`) created phantom `""` assets via
  `assets[current]`; ids are now captured before the move at all three
  sites.

## Wave 11 pass (menus, shortcuts, cleanup, skills, versioning)

- Dead code removed: `brushFalloff` field (shadowed by `paintFalloff`),
  `UndoPaintCommand` + `commitUndo/commitRedo/doRedo` stubs (real path is
  `pushUndoSnapshot`/`App::undo/redo`), `viewModeName` (zero callers).
- View menu: Grid/Bones/X-ray/Wire-overlay/Paint/Deform toggles, 1-7 view
  modes, Ortho, Frame all, VSync. Help menu: Shortcuts cheatsheet table +
  About modal (version from CMake, bridge presence, boundary note).
- Global shortcuts (skipped while typing): F frame, Ctrl+Z/Y undo/redo,
  G/B/X/W/P/D toggles, 1-7 modes. Timeline: |< < Play > >| stepping +
  Space/Left/Right transport.
- Version single-sourced from CMake (`PROJECT_VERSION 0.9.0` ->
  `M2RIG_VERSION` define -> `appVersion()` in About + `m2rig_cli
  --version`); CLI gained `validate-msm` + `tests/data/sample.msm`
  fixture (6 ctest suites).
- All 25 stale skills rewritten to native reality (C:\rigapp/dotnet/182
  claims removed; proposals honestly marked); `docs/DEPENDENCIES.md` +
  `docs/CHANGELOG.md` added; `.github/workflows/ci-windows.yml`.

## Test coverage (41/41 checks + 4 CLI suites, ctest green, release)

- math (compose/lookAt/camera), skeleton build/reject, sample armor
  validity, repair pipeline + never-silent-truncate, profiles/mirror/
  mapping, mesh damage detection, JSON round-trip/malformed.
- SMD: two-bone parse, hierarchy+rigid fallback, roundtrip identical,
  roundtrip sample armor, rejects malformed (incl. bad parent id 7 and
  unknown triangle bone 9), file IO, pose frames, real 90-bone ninja
  bridge excerpt (sockets + Spine2 resolve, no missing core).
- weights: paint falloff center-vs-edge + curve endpoints, paint clamps
  to 4 with normalization, kNN transfer copies nearest, mirror pairs +
  remap, MSM Group round-trip, workspace save/load (+locks by name),
  extractor format detection + unknown-format rejection, profile
  optional-bone coverage, socket-deform warning, self-train convergence
  on identity, deform bind-identity + single-bone translation,
  grnreader live conversion, locks block paint + undo/redo, locks survive
  mirror, isolation filter, budget + weld reports, batch export.

## Next tasks (roadmap waves 9+)

- Wave 9: FBX/OpenFBX native mesh reader (optional dep, core stays clean).
- Wave 10: universal extraction UI polish (progress, stdout/stderr log).
- Waves 11-15: PBR + ImGuizmo + morph targets + DQS skinning.
- Waves 16-20: .mse/.mde particles, .ani timeline, LOD, material atlas,
  auto-rig templates (Warrior/Ninja/Sura/Shaman/Lycan).
- Waves 21+: MCP stdio server, skills registry, batch converter,
  100+ CTest scenarios, AVX2, dep graph, sandboxing, Lua API, CI/CD.
