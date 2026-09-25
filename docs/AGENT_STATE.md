# Agent State — Metin2 Rigging Studio (native)

Last updated: 2026-09-24 (waves 1-29 + UI + Round A/B/C/D,
186/186 checks + 18 CLI suites green in debug and release, v0.10.0).

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
- Version single-sourced from CMake (`PROJECT_VERSION 0.10.0` ->
  `M2RIG_VERSION` define -> `appVersion()` in About + `m2rig_cli
  --version`); CLI gained `validate-msm` + `tests/data/sample.msm`
  fixture (6 ctest suites).
- All 25 stale skills rewritten to native reality (C:\rigapp/dotnet/182
  claims removed; proposals honestly marked); `docs/DEPENDENCIES.md` +
  `docs/CHANGELOG.md` added; `.github/workflows/ci-windows.yml`.

## Wave 12 pass (native FBX, MSM nesting, inspector, autosave)

- Native OpenFBX reader (`m2rig_fbx` adapter lib, core stays clean):
  nem0/OpenFBX pin `4d4a45a0` verified via GitHub API (ufo0905 fork is
  dead; libdeflate ships in-tree, no second dep), populate-only +
  `/W3 /WX-`. Reader: LIMB_NODE+NULL_NODE bones (id-ordered, parent walk
  to nearest bone ancestor), degrees->radians XYZ euler (order-checked,
  non-XYZ rejected explicitly), triangulated soup with UV/normals,
  geometric matrix applied when non-identity, skin clusters mapped via
  control points with unknown-link hard errors, materials per partition.
  Verified live: `ninja.fbx` -> 90 bones / 9624 verts / 3208 tris / 4
  meshes — identical counts to the Noesis conversion; profile check
  passes. GUI tries native FBX first (Noesis fallback); CLI gained
  `fbx2smd`.
- MSM parser fixed for real: nesting never worked (`stripComment`
  trimmed leading whitespace before indent measurement, so every tree
  was flat) — fixed + regression test, plus move-safe recursive
  assembly. MSM Inspector panel (right dock): open .msm, Group tree with
  attributes, semantic validation (`MSM_SHAPE_COUNT/REF/VERTEX_COUNT`,
  `MSM_NO_INDEX/SKIN`) into its own report.
- Autosave (`tickAutosave`, `.tmp` + atomic rename, dirty-gated) +
  crash recovery (`session.lock` PID flag, restore/discard modal) +
  Project-panel interval slider + Save-now + last-backup readout.
  Honest scope: settings + asset refs persist; SMD sources reimport
  where files still exist.

## Wave 13 pass (textured viewport, flood/prune, async bridge)

- DDS decoder in core (`dds.hpp/dcc.cpp`: DXT1/BC1, DXT3/BC2, DXT5/BC3
  incl. 1-bit-alpha and both alpha-ramp branches, mip-size validation,
  DX10 BC1-3 mapping, explicit rejections). Verified on synthetic blocks
  (solid red, opaque + transparent alpha) and a real 512x512 DXT3 game
  texture decoded end to end.
- Textured viewport: UV channel in the vertex layout, textured pixel
  shader + linear sampler + SRV cache, `drawMeshTextured` with untextured
  fallback, first-material DDS resolve (literal/Data-Models/basename),
  Tex toggle (toolbar, View menu, T key).
- Flood/Prune selected bone (core ops + App guards + Bone-panel buttons,
  undoable, lock-aware, mass-reported).
- Async bridge imports: `startBridgedImport` launches a worker producing
  SMD text only; per-frame `pollBridgeImport` applies it on the UI
  thread; import controls gated while busy with a running-time readout.
  Sync `importBridgedFile` kept for scripting/tests.

## Wave 14 pass (viewport visibility, scale gizmo, render smoke test)

- Root cause of the empty viewport found and fixed: the D3D11 scene is
  rendered BEFORE `ImGui::Render`, so the Viewport window's opaque
  `WindowBg` hid grid, mesh, bones and gizmo behind it. The window now
  uses `ImGuiWindowFlags_NoBackground` (documented in code as
  load-bearing).
- Scale gizmo op completes the Translate/Rotate/Scale trio (world scale
  through parent world scale, guarded divide); undo snapshots now carry
  bone scale too. Skeleton-tree selection marks the GPU mesh dirty so the
  Weights heatmap follows clicks.
- Headless D3D11 smoke test (`tests/test_render.cpp`, Windows only):
  hidden 64x64 window, init (WARP fallback allowed), sample-armor upload
  with the 52-byte UV layout, one frame through solid/wire/overlay/
  textured-fallback/textured/texture-upload/lines/X-ray paths, present.
  Catches shader-compile failures and layout regressions in CI.

## Test coverage (186/186 checks + 18 CLI suites, ctest green, debug + release)

- math (compose/lookAt/camera), skeleton build/reject, sample armor
  validity, repair pipeline + never-silent-truncate, profiles/mirror/
  mapping, mesh damage detection, topology census + determinism,
  JSON round-trip/malformed.
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
  mirror, isolation filter, budget + weld reports, batch export, native
   FBX 90-bone ninja, MSM mismatch validation, autosave tick,
   deformed-Height parity, deterministic auto-rig, 25-file FBX model set
   (21 native + 4 honest Noesis fallbacks), 31 GR2 header validations.
- Wave 27-29 additions: DDS multi-mip BC1-5 (8x8 2-mip pin, truncated-tail
  fail, BC4/BC5 ramps, SNORM explicit reject), authored-mip upload
  idempotence + ranges, offscreen 72 B viewport pass (64 + 32 resize),
  GPU tangent non-zero pin, quaternion sampling (short-arc, wrap parity,
  double-cover, gimbal stability, bake==preview, binary-search parity),
  compression bounds (reduction, smallest-three, position range,
  .ani bytes pinned), bridge per-call log/extractor/exit-code pins,
  workspace restore round-trip (locks/brush/fovY/selection sets), FBX
  indexed dedup (3208 tris / 90 bones exact), glTF cube-skin round-trip
  + non-indexed/meshopt/draco explicit rejects, glTF 29b emit round-trip
  + overlimit/layout rejects, msmToSmd shell + rejects, routing matrix
  (G4), winding + static-prop advisory, range parity + normal-map
  bound/unbound, glTF anim import (rotation channel, scale/step rejects)
  + sampler emission + mismatch reject, msm2smd shell chain, texture
  cache dedup/accounting, UV range/degenerate/overlap (+ determinism),
  App glTF import round-trip.
- CLI suites (18): validate, validate-msm, validate-mse, info, smd2smd,
  smd2msm, autorig, lod, learn-from-asset, self-learn-autorig,
  self-learn-transfer, orient-two-bone, gr22smd-missing, fbx2smd-missing,
  learn-from-gr2-dir-missing, analyze-gr2-dir-missing, msm2smd
  (exit-2 negatives need no fixtures; msm2smd chains sample.msm +
  two_bone.smd) — the autorig
  suite round-trips `tests/data/two_bone.smd` through the same
  deterministic bind as the GUI button (exit 0, verts/bones preserved).
  fbx2smd/gltf2smd verbs exist build-only (no checked-in fixtures).

## Wave 15 fix pass (2026-09-16)

- Restored the tracked `CMakePresets.json`; both `windows-debug` and
  `windows-release` configure successfully with CMake 4.3.
- Hardened `scripts/package-windows.ps1`: only the two documented presets are
  accepted, and the package README is sourced from the existing user guide.
  Release packaging was verified; Noesis/grnreader assets remain excluded by
  design and are documented as runtime dependencies.
- Verified debug and release builds plus 6/6 CTest suites in each
  configuration. DQS remains a future CPU deformation slice, not a claimed
  implemented feature.
- Hardened the D3D11 viewport boundary: scene rectangles are clamped to the
  swap-chain dimensions and rasterized through a matching scissor rectangle.
  The render smoke test covers a panel rect extending beyond the backbuffer,
  while the existing grid/model/skeleton/gizmo ordering remains unchanged.
- Fixed the application-level blank-frame path: invalid scene passes bind and
  clear the RTV before ImGui renders, resize requests retry after failure,
  asset selection guarantees a GPU upload, and hidden-all submeshes cannot
  leave stale geometry. Render smoke now proves model pixels separately from
  grid pixels. ImGui layout persists to `config/imgui.ini`, shortcuts honor
  keyboard capture, and texture roots are found through runtime ancestors.
- Full release rebuild and launch smoke passed; the release executable reached
  the Win32 message loop. Release CTest remains 6/6 green.

## Wave 16 (2026-09-17)

- Headless `m2rig_cli autorig <in.smd> <out.smd>`: same deterministic
  `autoRigMesh` engine as the GUI button + standard repair + export gate
  + stats print; `cli-autorig` ctest suite (7th) round-trips
  `tests/data/two_bone.smd` (exit 0, error paths 1/2 verified manually).
- Restored the root `.gitignore` (was deleted in the working tree, which
  exposed `build/`, `Data/Models` binaries, `noesis/` as untracked noise);
  content identical to HEAD plus trailing newline. `git status` is clean
  again apart from the intended change set.
- `docs/USER_GUIDE.md` truth pass: removed the legacy web "AI Weight
  Transfer" / "Neural Weight Prediction" sections (`train_pipeline.py`,
  `POST /ai_skin` do not exist in the native product), fixed the deleted
  `_archive/` reference, documented the real transfer workflow + full CLI
  reference, corrected Known Limitations (dock persists in
  `config/imgui.ini`; deform preview is CPU LBS, DQS is future).
- Verified: 59/59 + 7/7 green in debug and release, `/W4 /WX /FS` clean.

## Wave 17 (2026-09-17)

- Edge-hash adjacency in core (`MeshTopology`/`buildMeshTopology` in
  `mesh.hpp`/`mesh.cpp`, zero third-party deps): boundary / manifold /
  non-manifold edge census + isolated/open vertex counts over
  topologically valid triangles only (3 distinct in-range ids; degenerate
  and out-of-range tris stay flagged by their own rules and are skipped).
  Fully deterministic (order- and winding-independent).
- Validation wiring: `MESH_TOPOLOGY` info always, `MESH_NON_MANIFOLD` +
  `MESH_ISOLATED_VERTS` warnings (non-blocking — game meshes stay
  exportable). Flows automatically into GUI `App::runValidation` and all
  CLI verbs via `validateMeshStructure`; closes the old "non-manifold
  honestly deferred" gap. LOD groundwork: adjacency is the prerequisite
  the roadmap asked for.
- 5 new core tests (closed tetrahedron 6-manifold census, open fan +
  isolated vert, non-manifold fin, bad-triangle skipping + triangle-order
  determinism, isolated-vert warning without export block).
- Real-data proof: native `ninja.fbx` -> 3208 tris, 860 boundary, 4382
  manifold, 0 non-manifold, 0 isolated (edge math cross-checked:
  2*4382+860 = 9624 = 3*3208), zero new warnings on the real asset.
- Verified: 64/64 + 7/7 green in debug and release, `/W4 /WX /FS` clean.

## Wave 18 (2026-09-17) — viewport full integration (8-agent audit + fix)

Orchestration: 5 skills loaded (rendering/testing/debugging/performance/
documentation) + 8 parallel read-only research agents (render path, gizmo/
camera, intake flows, bug-hunt, performance, QA-UX, test gaps, docs truth).
Every finding below carries agent-reported file:line evidence, verified by
the orchestrator in code before fixing. Out of scope (documented, not
silently dropped): per-stroke undo coalescing, deform-space picking,
hidden-submesh paint indication, camera target/distance persistence,
per-frame material `exists()` cache, `gPaintState` dead duplicate.

- Always-visible viewport: gizmo extracted from the hover gate into
  `updateBoneGizmo` (drawn whenever a bone is selected; degenerate rects
  guarded; drag-end bookkeeping can no longer freeze off-panel); root and
  orphan bones now draw joint markers and are clickable (point-to-ray
  branch in `pickBoneAt`); `pickMeshPoint` skips out-of-range indices
  instead of reading OOB; camera sanitizes NaN bounds/state in
  `frameAabb`/`updateClip` (no more permanently poisoned viewport).
- Gizmo math fix: Rotate scale-strip normalizes ROWS (compose() scales
  rows via S*R) — columns corrupted euler under non-uniform parent scale;
  covered by `gizmo_rotate_row_strip_matches_compose` incl. a scaled +
  rotated parent world->local roundtrip.
- Intake parity: `loadSampleArmor` now clears locks/hidden/undo + validates
  (matches `installConverted`/`loadSampleArmorForProfile`); asset switch
  clears undo/hover + validates + status; Noesis-tmp installs under the
  ORIGINAL source path (honest id/status, no repeat collisions); async
  `.smd` failures surface a status; `loadWorkspaceFile` reports failure
  honestly instead of false success.
- Workspace restore that reimports: `loadWorkspace` now parses the
  `assets[]` array (was silently ignored) + brush radius/strength +
  orthographic/fovY (validated); `restoreWorkspace` reimports missing
  sources via the bridge, resolves selection, locks, brush, camera mode,
  frames + re-uploads; unresolvable workspaces fail WITHOUT touching the
  live session (regression-tested both directions).
- Undo/paint honesty: undo history cleared on install/switch/restore;
  zero-vert dabs roll back their snapshot + report info; stale-bone paint
  refused; mirror pushes undo only after pairs exist; flood/prune on
  locked bones FAIL instead of fake-ok (test updated to the honest
  contract); symmetry-skip noted in the paint status.
- Perf (measured-by-construction, no profilers in repo): per-bone
  influence histogram + weight-quality metrics cached on App (tree/panel
  were O(bones x verts) per frame); System-panel Data/Models scan on a
  2 s TTL; timeline `lastT` reset stops cross-asset playback jumps.
- UX honesty: empty-state `File >` corrected to `Project >`; Frame button
  disabled with no asset; overlay preset tooltips; red brush ring with no
  bone; hover tooltip shows id + `[LOCKED]`; cheatsheet documents
  `Ctrl/Shift+drag` + click preconditions.
- 12 new tests (8 camera/gizmo math, 4 App/workspace behavior); one real
  failure caught by the suite during the wave itself (missing `assets[]`
  parse) and fixed via the failure protocol.
- Real-data: ninja validates 0 errors + autorig round-trips exit 0.
- Verified: 76/76 + 7/7 green in debug and release, `/W4 /WX /FS` clean.

## Wave 19 (2026-09-17) — LOD decimation generator

- Core `lod.hpp`/`lod.cpp` (`decimateMesh`, zero third-party deps):
  deterministic shortest-edge collapse with (length, minId, maxId)
  strict-less ordering; merged vertices carry the union of both influence
  lists through the standard repair (<=4, normalized, removed mass in
  `LodStats`); degenerate tris dropped + counted; vertices compacted in
  old-id order; submesh ranges rebuilt in original order (emptied ones
  dropped, orphans get a `lod_orphans` entry); bounds + normals
  recomputed. Explicit rejects: empty/triangle-less meshes, ratio outside
  (0,1). No-op success when the floor already covers the input.
- Export-only by design: CLI `lod` and the GUI `Export LOD SMD...` button
  decimate a COPY (live session, undo and `hiddenSubmeshes` indices never
  touched); LOD output re-runs repair + the export gate.
- 5 new core tests (cube halving + determinism + submesh coverage,
  bone-union merge, bad-options, min-floor no-op, sample-armor stays
  export-valid) + `cli-lod` ctest suite (8th). One wave-local test bug
  (wrong expected edge (0,2) vs deterministic (0,1)) caught by the suite
  and fixed via the failure protocol — algorithm was correct.
- Real-data: ninja 3208 -> 1603 tris (50%), 2070 -> 1089 verts, 4
  submeshes kept, 0 errors, 0.14 s wall time.
- Verified: 81/81 + 8/8 green in debug and release, `/W4 /WX /FS` clean.

## Wave 20 (2026-09-17) — keyframed animation system

- Core `anim.hpp`/`anim.cpp` (zero third-party deps): `AnimKey` (full
  local pos+euler snapshots parallel to skeleton bones) + sorted-unique
  `AnimClip`; capture/add-replace/delete/last-frame; `sampleClip` with
  component lerp + per-component shortest-path euler wrapping (documented
  euler-lerp, not slerp); `bakeClipFrames` emits plain SmdFrames so the
  existing writer, timeline and deform preview work unchanged. Empty clip
  samples/bakes nothing, explicitly.
- App routing: `LoadedAsset::clip` (in-session, not persisted);
  `timelineFrameCount()` = max(frames, lastKey+1); `setCurrentFrame`
  samples the clip when keys exist (clamped), imported frames otherwise —
  zero behavior change with an empty clip. Add/Del/Bake/Clear with honest
  statuses (bake REPLACES imported frames and says the frame array is not
  pose-undo-covered). Timeline panel: transport + slider + time readout
  run on the clip-aware length; keyframe row (Add/Del/Bake/Clear,
  tooltips, key list, re-key hint) also shown for static assets with
  frame steppers so the first key is always reachable.
- 5 new tests (lerp midpoint, euler wrap to pi, add/replace/delete +
  end clamping, bake times/poses + SMD round-trip, clip-driven App
  timeline incl. bake/delete/clear honesty).
- Verified: 86/86 + 8/8 green in debug and release, `/W4 /WX /FS` clean.

## Wave 21 (viewport/render/session rework — working-tree, from code evidence)

- Offscreen viewport: 3D scene renders into a D3D11 texture sized to the
  panel rect (`ensureViewportTarget`/`beginViewportPass`/`endViewportPass`),
  composited via `ImGui::Image()` — survives the occupied-central-node
  opaque dock background and DPI transitions. Backbuffer path kept for the
  headless smoke test + invalid-rect fallback. Pixel-proof readback
  (`readViewport`, throttled `PixelProof` verdict) proves model pixels.
- Session UX: toast queue (sticky errors, expiring info/warning), status bar
  (viewport/asset/draw/camera/status), empty-state guide, X-ray bones default
  ON so skeletons read through closed meshes.
- Skinning/render data: DQS dual-quaternion palette + deformed-DQS GPU views
  (`useDqs` toggle), `GpuVertex` tangent/bitangent channels (72 B) +
  `computeTangents` (MikkTSpace-style accumulate), PBR material data model
  (`PbrMaterial`, header only), MSE parser module (`mse.hpp`/`mse.cpp`,
  no UI/CLI/test wiring yet).
- NOTE (Wave 22 finding): this wave's tree did not compile — duplicate
  `computeNormals` in `mesh.cpp`, `GpuVertex` 80 B assert vs 72 B struct,
  stale 48 B D3D11 input layout, renderer texture-API header/impl drift.
  All repaired in Wave 22 (see below); treat pre-Wave-22 build/test claims
  for this tree as unverified.

## Wave 22 (2026-09-18) — coordsys profiles + orientation root-cause + transform editing

Orchestration: Phase-0 read-only audit by a research agent (TODO/FIXME:
zero hits; dead-code: `gPaintState` write-only [known Wave-18 defer],
MSE parse/stringify unwired, XUp/detector stubs [fixed this wave];
duplicates: coordsys preamble [factored], SMD exits [gated, kept];
risks top-10 recorded in the wave report).

- Canonical coordinate system (`coordsys.hpp`/`coordsys.cpp`, core):
  Y-up single source of truth (`Mat4::convertZUpToYUp` reused, never
  re-derived) + per-format profiles (FBX/GR2/SMD/Noesis: assumed source,
  auto-detect flag, mesh/skeleton/frames switches). `tryConvertToCanonical`
  FAILS explicitly on unimplemented spaces (XUp) — no silent identity.
  Native FBX reader + grnreader98 GR2 path routed through the profiles;
  inline matrix copies deleted. GR2 frames now convert too (frame 0 must
  reproduce the converted bind).
- ROOT-CAUSE orientation fix (HARD RULE class): bone/frame rotation
  conjugation was `M*R*M^-1`, corrected to `M^-1*R*M` (proof:
  `(u*M)*R' = (u*R)*M` holds only for `M^-1*R*M`; translation `t*M`
  already matched it, so old code was a hybrid). Invisible for identity
  binds (ninja counts unchanged) but detached mesh from skeleton under
  posing for rotated binds. Pinned by `coordsys_skeleton_conversion_`
  `preserves_bind` incl. an end-to-end yaw-direction check through the
  shipped path.
- ROOT-CAUSE hierarchy fix (the actual detachment): `rebuildSkeletonRuntime`
  composed `parent * local`, but row-vector points transform `p*L*P`, so
  globals must compose `local * parent`. The old order applied the CHILD's
  rotation to the PARENT's translation — correct only for identity binds
  (sample armor, all suite rigs: blind spot), scrambling every rotated-bind
  skeleton (GR2 bipeds: 243-unit rigid detachment on warrior_m). Fixed in
  `skeleton.cpp` + gizmo decomposition (`nW * pG^-1`) + its test mirror;
  palette/deform/export paths are order-agnostic (same rebuild) and SMD
  round-trips stay byte-identical (locals untouched). Pinned by
  `skeleton_composes_local_then_parent` (90°-parent swing + 3-level chain).
  Real-data proof: warrior_m.gr2 243.19 -> 30.89 worst rigid distance,
  SANE (head 159.3, feet 12.3, skel Y -0.2..177.5 vs mesh 0.4..174.0);
  sura_m.gr2, warriorout.fbx and the `-rotate 0 180 90` Noesis-experiment
  FBX files all SANE too (they were never broken sources — the engine was).
  `-a` vs `-a -t` grnreader flags compared: identical joints/orientation
  (`-t` only negates some skeleton values); flag kept, GUI/CLI consistent.
- Build repairs (tree was red): removed duplicated `computeNormals`;
  `GpuVertex` assert 80 -> 72 B; D3D11 input layout COLOR@48/TEXCOORD@64
  (stale 48 B layout fed tangent bytes as color); texture API reconciled
  (real `GenerateMips` path, `hasTexture` implemented, `setActiveTexture`
  declared, unimplemented PBR/IBL decls removed, `PbrMaterial` kept as the
  Phase-9 data model).
- Bone panel: numeric Position / Rotation (deg) / Scale editors (undoable
  via activate/deactivate, lock-aware, finite + near-zero-scale guards,
  rebuild + revalidate) + Frame bone + Select mirror + Copy/Paste +
  Reset-to-bind (frame 0, whole-skeleton pose path). Gizmo World/Local
  orientation switch (ImGuizmo mode; output stays world, decomposition
  unchanged). Viewport Back/Right/Bottom presets (tilt guards top/bottom
  lookAt degeneracy).
- Tests: 8 new (6 coordsys incl. XUp explicit-failure + frame-0-bind +
  mesh move, 1 camera six-preset finiteness, FBX model-set gate made
  dataset-aware for partial checkouts). Full data: 9/9 local FBX parse
  natively with sane output.
- Transform diagnostics: `diagnoseOrientation` (core) + `m2rig_cli orient`
  (exit 0 sane / 3 blocking): head-vs-feet order, mesh-relative vertical
  overlap, scale advisory, rigid-bind joint-vs-centroid distance (4 tests).
  New `m2rig_cli gr22smd` verb = headless twin of the GUI grnreader98 path
  (same profile conversion, repair, export gate).
- Verified: release + debug configure/build clean (`/W4 /WX /FS`),
  100/100 + 8/8 green in BOTH configurations.

## Wave 23 (2026-09-21) — viewport orientation + camera + UI unification

User report (Czech): whole model upside down, camera won't do 360°, grid
preview inverted, viewport bugged, ImGui panels need unification. Root causes
found by code inspection (not guesses), two agent-suggested "fixes" from the
prior session reverted as regressions after solution-judge review:

- Upside-down model: offscreen viewport composited via `ImGui::Image()` with
  a V-flip for "D3D11 texture origin" (`panels.cpp`). D3D11 textures are
  top-left origin like ImGui, so the flip rendered every frame upside down.
  Fixed to UV `(0,0)->(1,1)` (prior session; kept).
- Camera 360° + DPI (`camera.hpp`): yaw folds only by full turns (`fmod`,
  no visual jump); `orbit()`/`pan()` scale `dx/dy` by `dpiScaleX/Y`,
  `zoom()` scales the wheel; pole-crossing reflection kept (pinned by the
  existing `camera_orbit_clamp_and_pan_scale` test).
- Aspect: viewport/box-select/bone-label projections use logical
  `avail.x/avail.y`, not physical `rect.w/rect.h` (`panels.cpp`); the
  backbuffer fallback in `renderScene` intentionally keeps physical pixels.
- Overlay buttons no longer steal orbit drags: `SetNextItemAllowOverlap`
  removed; drag state tracked globally (`btnDown` + viewport-rect hover).
- REVERTED (solution-judge, evidence below): Front/Back preset swap and the
  grid-Z flip to `-axisLen`. `preset({0,0,1})` = yaw 0 = eye at +Z facing a
  +Z-facing character (sample toes/stip at +Z, `samples.cpp:31-33`; test
  comment at `test_math.cpp:561` documents Back = `-Z`, yaw pi). The flipped
  blue shaft also contradicted its own arrowheads (`mesh_views.cpp:192-193`).
  Grid keeps the standard +XYZ triad.
- Real Top/Bottom bug fixed instead: `applyPreset` Top/Bottom used exact
  `{0,+-1,0}`, i.e. pitch exactly +-pi/2, which parallels world-up and
  degenerates `lookAt` (`cross(up,z)==0`). Both now carry the 0.001 tilt the
  existing six-preset test already documents as load-bearing.
- UI unification: left dock split into Assets/Scene/Skeleton windows
  (`drawAssetsPanel`/`drawScenePanel`/`drawSkeletonPanel`, no behavior
  change); Settings god-panel split into Bone Display / Gizmo / Viewport
  Settings windows; right dock = Properties + Workflow columns, bottom dock
  = Output + Tools (MSM Inspector as a Tools tab); View menu gained a Panels
  submenu plus toolbar quick-toggles; all 14 panel flags persisted in
  `user_prefs.json` (`ui` section).
- Regression tests (3 new in `tests/test_math.cpp`):
  `camera_apply_preset_view_mapping` (Front yaw 0, Back |yaw| pi, Left/Right
  +-pi/2, Top/Bottom pitch near +-pi/2 but strict + finite eye/view),
  `grid_blue_axis_points_positive_z` (shaft origin->+axisLen, none to -Z),
  `camera_orbit_applies_dpi_scale` (2x DPI doubles yaw/pan response).
- Verified: release configure/build clean (`/W4 /WX /FS`), `ctest --preset
  windows-release` 9/9 green incl. the 3 new cases; debug build clean, debug
  CLI suites 8/8 green. Full debug `m2rig_tests` exceeds 30 min wall time
  (pre-existing intentional CRT heap-check triage mode in
  `tests/test_main.cpp`, unrelated to this wave) — not claimed green.
- Note: `rendering` skill doc still claims 48 B `GpuVertex`; code truth is
  72 B (`renderer.hpp:36`, Wave 21/22). Skill needs a refresh pass.

## Step 1-2 (2026-09-21) — 10x program kickoff (orchestrator + 6-agent swarm)

Swarm: gpu/texture/rigging/metin2/ux/architect deep-dives (all claims
`file:line`), synthesis in `docs/UPGRADE_10X_BRAINSTORM.md`, 10 program
skills (`pbr-rendering/gpu-skinning/modern-textures/quaternion-anim/
spatial-index/gltf-pipeline/viewport-ux/workspace-restore/build-ci/
docs-brain-sync`) + `docs/SPECIALIZED_AGENTS_AND_SKILLS.md`, `rendering`
skill refreshed (48 B -> 72 B + offscreen contract).

- Step 1: folder-dialog Unicode fix (`SHBrowseForFolderA` -> `W` in
  `src/app/file_dialog.cpp` — diacritics paths like `D:\modely\brnění`
  no longer mojibake; UTF-8 public contract unchanged) + new core module
  `utf8.hpp/utf8.cpp` (`utf8ToWide/wideToUtf8`, Win32 semantics + portable
  fallback, core stays portable) + 3 `core.utf8_*` tests (Czech path,
  empty/ASCII, euro/emoji units). `brain/README.md` bumped to Wave-23,
  `brain/BENCHMARKS.md` created (warrior_m `gr22smd` 1214 ms wall,
  `lod @0.5` 7426->3713 tris in 442 ms, `orient` SANE re-proving the
  Wave-22 numbers head 159.34/feet 12.26/rigid 30.89).
- Step 2: sRGB albedo path (S1/S3 contracts). `PsMain`/`PsTexSrgb`/
  `PsTexLinear` light in linear + sRGB encode; decode in-shader because
  explicit SRGB views fail creation on the WARP box (UNORM views kept for
  max compat); `drawMeshFlat` + debug-mode routing in `drawSceneContents`;
  mip-tradeoff documented, not hidden. Test gaps G1/G2/G3/G5 closed by
  hand-computed pixel pins (flat 255,0,0; sRGB-128 78,78,80; linear-128
  116,116,119; nomips == mipmapped; flag re-upload/release); G4 (routing
  test) + G6/G7 deferred with reasons (need headless App fixture).
  Pre-existing `BlinnSpec` double-0.35 (effective 0.1225, `renderer.cpp`)
  observed, deliberately preserved (PBR wave replaces Blinn anyway).
  Wave-local failures fixed via protocol, never skipped: HLSL `linear`
  reserved-keyword collision (X3000), `Impl::TextureEntry` qualification.
- Verified: release configure/build clean (`/W4 /WX /FS`),
  `ctest --preset windows-release` 9/9 green, unit binary 122/122
  (incl. 3 utf8 + 1 sRGB case); suite-1 pixel proof steady at 873
  non-clear. Debug preset not run this step (triage note stands).

## Steps 3-4 (2026-09-21) — bridge hygiene + triage flag (S6/S9/S10)

- Noesis import race killed: unique tmp per call in `importBridgedFile`
  (`app_state.cpp`: PID + function-local atomic counter + `stemOf`
  framing; async `runBridgeChain` already carried PID + counter) —
  shared `m2rig_bridge_import.smd` gone, stale installs impossible,
  install-under-original + remove-after-read preserved. Best-effort tmp
  remove on both Noesis `timedOut` paths (terminated bridge may leave a
  partial file). Same race class closed in FBX export tmp (counter added)
  and `exportGr2ViaBridge` (unique tmp unless `keepTempSmd`, whose
  deterministic inspection name is preserved); missing cleanup added on
  its failure paths. `-a -t` documented as observed-but-unverified
  (orient gate is the safety net); `extractWeightsFromGr2ViaBridge`
  sharpened to an explicit-unsupported boundary probe (zero callers,
  symbol kept for the documented boundary); CLI header synced to 16/16
  verbs (header-usage-dispatch 1:1:1); USER_GUIDE truth (DQS toggle,
  MSE wired vs overlay roadmap, `.ani` export-only).
- `M2RIG_HEAP_TRIAGE` (default OFF) gates the debug CRT heap validation
  (`test_main.cpp`); fast path proven: debug `m2rig_tests` 122/122 green
  in ~124 s wall without triage (was >30 min) — incl. identical sRGB
  pixel pins across configs (flat 255 / sRGB-128 78 / linear-128 116).
- 3 new fixture CLI suites (`cli-learn-from-asset`,
  `cli-self-learn-autorig`, `cli-self-learn-transfer`, exit 0 verified on
  `two_bone.smd`); gr2-dir/analyze + fbx2smd unwired with stated reasons
  (need Data/Models / FBX fixture, absent in clean checkouts).
- Independent read-only audit (test-engineer): GO-WITH-NOTES, all notes
  incorporated (timeout litter, FBX race, `-a` header drift); G4 routing
  test still deferred (needs headless App fixture — Wave 30).
- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 12/12 green; debug configure clean
  (`M2RIG_HEAP_TRIAGE:BOOL=OFF` in cache), debug `m2rig_tests` target
  builds `/W4 /WX` clean and runs 122/122 green.

## Wave 24 (2026-09-21) — GPU skinning LBS (S2 contract)

- Stream + shader + backend: `SkinVertex` (32 B, `uint32[4]` ids +
  `float[4]` weights, static_assert) as VB slot 1 beside the untouched
  72 B vertex; `VsSkinned` (dynamic `gBones[uint]` indexing, SM5-legal)
  matching `deformVertex` for accepted inputs (blend, /wsum, bind
  fallback; degenerate-band diction narrowed per audit: float-vs-double
  wsum, 1e-9-vs-1e-12 normal gate); 256-matrix b1 CB with identity fill
  + identity padding past the palette; skinned input layout compiled from
  the VsSkinned blob (6 elements, slots 0/0/1/1/0/0).
- API + pairing: `uploadSkinning/hasSkinning/releaseSkinning/
  setSkinningPalette` + 4 skinned draws (shared Impl prologue: lookup +
  count-pairing guard, layout/VS bind, full restore incl. slot-1 unbind);
  `uploadMesh`/`releaseMesh` drop stale skin (no survivor pairing).
- App: `refreshGpu` uploads bind + skin once (`buildSkinVertices` fails
  explicitly -> static + honest warning); legacy CPU-DQS path byte-intact
  (deformed verts + `releaseSkinning` — double-deform structurally
  impossible); `drawSceneContents` sets the per-frame palette and routes
  textured/debug/solid/overlay through skinned variants (drawCalls
  preserved).
- Tests: `core.skinning_stream_sample_armor` (23-bone stream, sums ~1) +
  `core.skinning_stream_rejects_bad_data` (dangling/palette/>4/negative
  fail, unweighted zeros) + `render.gpu_skinning_lbs` (identity diff 0
  bytes; translate diff 3072; textured 78; flat 255,0,0; overlay 95 px on
  interior proof geometry after a boundary-rasterization analysis;
  sample-armor 1036 px). Audit verdict was NO-GO (SkinVertex fwd decl,
  test include) — both fixed pre-build plus slot-1 unbind and narrowed
  parity wording; overlay geometry reworked for margin on evidence.
- Perf shape (structural, no fake numbers): timeline playback no longer
  re-uploads the full VB per frame — one 16 KB palette upload instead
  (warrior_m: ~330 KB VB + 164 KB skin once vs per frame before).
- Sequencing note: full RHI interface deferred deliberately — a single
  backend with no second consumer is a dead abstraction. It lands with
  the null backend (headless-Linux wave), not as plan theater.
- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 12/12 green, unit binary 125/125 (release AND debug,
  identical pixel values across configs); suite-1 proof steady at 873.

## Wave 25a (2026-09-21) — PBR punctual backend (S1/S3 contracts)

- Shader + backend: `PbrMat` CB (b2, PS-only, 48 B: baseColor linear +
  metal/rough/AO/emissive-intensity + emissive rgb) bound in both passes
  with neutral defaults; `PsPbr`/`PsTexPbr` over shared `PbrLighting`
  (GGX NDF with Disney α=rough² mapping, Schlick-GGX geometry with k-guard
  against 0/0, F0 lerp, `max(4·NoV·NoL,eps)` denom guard; ambient is an
  albedo-weighted placeholder documented for IBL 25b; baseColor.a
  declared-unused). `setPbrMaterial` maps the struct field-for-field
  (verified); 4 PBR draws with Blinn-grade fallback discipline + PS
  restore + drawCalls; textured-PBR assumes albedo-by-construction
  (single diffuse path — data slots ride Wave 27).
- App: per-asset `LoadedAsset::pbr` (session state like clip) +
  `App::usePbr`; Materials sliders (metal/rough/AO, dirty-without-gpuDirty
  is correct: CB-per-draw); toolbar checkbox + View menu item (no hotkey
  — 1-7 taken); routing PBR-only-Solid/SolidWireframe (debug flat,
  Wireframe Blinn preserved).
- Tangents: `computeTangents` wired into SMD/FBX (file normals preserved
  — the no-silent-recompute rule), LOD/samples (normals first); SMD
  writer/MSM/merge/coordsys verified tangent-safe; `computeNormals` dead
  `overwriteExisting` param removed (ignored by impl, no caller passed
  true — latent hazard closed); GPU tangent channels still zero until the
  normal-map shader lands (Wave 27, documented).
- Tests: `render.pbr_punctual_values` — all 9 pixel sets hand-verified
  byte-near-exact (metal 98,98,104; dielectric 118,118,124; sharp 255 vs
  broad 151,151,155; greybase 86; AO 107<118; emissive 215; textured 58;
  skinned twins). One wave-local test-design failure (roughness on
  off-lobe geometry yields correctly ~117 — shader right, geometry
  indiscriminate) fixed via failure protocol with H-aligned proof
  geometry, and the lesson recorded in the test comment.
- Data proof (user-supplied Data/Models, 31 GR2): `gr22smd` 31/31 exit 0
  (incl. diacritics `ninjałka.gr2` — UTF-8 paths end to end), `orient`
  30/31 SANE with 0 findings (41–102 joints: ninja 90, surka 102,
  shaman_m2 100, wolfman 71); Gryphon honestly CHECKs (1-node static
  prop at origin vs 13k-vert mesh — gate working as designed; static-prop
  advisory-vs-block rule is a Wave-29 follow-up, not a bug).
- Audit GO-WITH-NOTES, all actionable items incorporated pre-build
  (CB mapping, tangent order/safety, flag removal, UI scope notes kept as
  documented scope, not defects).
- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 12/12 green, unit binary 128/128 (release; debug run
  below in the release step).

## Wave 25b (2026-09-21) — IBL: irradiance SH + prefiltered env + BRDF LUT

- Core: `ibl.hpp/ibl.cpp` (sky params + `defaultSky`, `evalSky`, SH
  project with A_l scales, `shIrradiance`, `floatToHalf/halfToFloat`);
  pinned by `core.sh_uniform_sky_is_pi` (exactly L*pi every normal),
  `core.sh_deterministic_and_sky_shaped`, `core.half_codec_roundtrip`
  (bit patterns + sky-range roundtrip).
- GPU bake at init: CPU-filled 128 base (exact by construction, no sky
  shader/duplicated constants), 64-tap GGX prefilter mips 1-7, 128 BRDF
  LUT, 9-coeff b3 CB, wrap/clamp samplers, blit pipeline (dynamic VB,
  VsBlit, per-draw payload uvs). `PbrLighting` gains world-space IBL
  (inverse view rotation; object == world in the viewport) + split-sum
  specular; the ambient placeholder is DELETED, not kept.
- Orientation proof: `CubeDir` derived from view math (r x u = -d) and
  proven by the mirror-sun case (reflection lands on the disc through the
  whole chain); V-flip and signature bugs caught by audit pre-build/run.
- ROOT CAUSE (textbook failure protocol): metallic specIBL read exactly
  0.000 while dielectric matched to the byte — staging readback (added
  temporarily, removed before release) proved prefilter mips black with
  perfect base + BRDF. Cause: D3D11 force-unbinds an SRV whose RESOURCE
  is bound as RTV even across disjoint subresources (my "no hazard"
  comment was wrong — corrected). Fix: source/destination cube split +
  base copy. All pairs now within 1 LSB: dielectric 156, metallic 107,
  mirror-sun 255, textured/skinned twins exact.
- Notes kept, not hidden: joint-vs-separable geometry stays a documented
  consistent pair (both sides identical, pixel-pinned — Karis-exact swap
  is future work with its own cycle); tap counts 64-GPU vs 128-ref
  (variance absorbed by tolerances; sun-hit analysis recorded);
  init-time bake cost lands in test-suite wall, not in frames.
- Tests: `ibl_split_sum_matches_reference` (dielectric/metallic/mirror
  vs independent CPU numeric integration ±12/±15 + r>150 tripwire) +
  all 25a PBR bands re-pinned through the reference (old absolutes
  retired honestly — IBL shifts every value).
- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 12/12 green, unit binary 132/132 (release AND debug,
  identical IBL pixels across configs).

## Wave 26 + UI unification (2026-09-22) — spatial index + viewport UX (S5/S7)

- KD-tree core (`spatial.hpp/cpp`, zero-dep): owned point copy, median
  split on longest axis (fixed x>y>z priority), total (coord,index)
  order — fully deterministic, no RNG/threads. `query()` exact top-k by
  (distSq,index) incl. ties (prune on <= so equal-distance smaller indices
  survive); `queryRadius()` strict radius sorted; NaN-safe (non-finite
  sorts last, never inserts — same graceful degradation as brute force).
- Rewired bit-identically (up to 1-ulp boundary ties): transfer kNN
  (sqrt(distSq) reproduces distance(), MAX-padding preserved),
  self-train donor table, mirror greedy (reflected query, j>i + unused +
  first-sorted-wins). Blend/remap/repair code untouched. `autoRigMesh`
  deliberately brute-force: nearest-SEGMENT metric is point-incompatible
  (segment can win while its joints lose — proven counterexample), O(V*B)
  negligible at 23-102 bones.
- UI unification (Wave-30a slice): toolbar "Auto-rig" (same op as Weights
  panel, red family, BeginDisabled), Flood/Prune red + tooltips, viewport
  Shading popover (7 mode radios + 6 toggles + Paint-with-guard; single
  state, no fork), input guard (`shadingOpen` suppresses orbit/pan/paint/
  zoom/click-select; reset/completion untouched), teal-cyan theme (all
  families, alphas preserved), Quick/Self-train tooltips, Locked label
  lists all 7 ops, USER_GUIDE truth pass (step order, Shading/PBR/
  Auto-rig/Quick-vs-Selftrain, GPU-skinning limits).
- Tests: 5 new (`matches_bruteforce` 2k pts x 50 queries x k1/3/8 exact +
  radius sets + double-build determinism; `edges`; `nan_graceful` vs
  brute helper; `query_timing` print-only + sink equality; `transfer_
  scale` 5040v mapped); all pre-existing transfer/mirror/self-train
  suites green unchanged (behavioral parity proof).
- 15-agent audit swarm (mesh/weighting/performance/bug-hunter/security/
  ux/frontend/gpu/metin2/architect/test/qa/regression/rigging/brain):
  all GO/GO-WITH-NOTES; incorporated: NaN sort-UB fix + NaN test,
  header signature fix, "Never throws" doc fix, O-complexity wording,
  input-drag guard, Paint-in-popover, red Rig, tooltips/labels/guides.
  Deferred with reasons: menu gpuDirty waste (perf-only, out of scope),
  Tex-warning duplication (single feedback site), fbx2smd/gr2-dir suites
  (no fixtures), G4 routing test (headless App fixture, Wave 30).
- Perf (observed, not budgeted): knn 10k x 200 print 12 ms brute vs
  3 ms tree; transfer 5040v 106 ms wall, 5040/5040 mapped.
- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 12/12 green, unit binary 137/137 (release AND debug).

- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 12/12 green, unit binary 137/137 (release AND debug).

## Wave 27 (2026-09-23) — modern textures: multi-mip DDS + renderer slice C

- DDS: Slice A decoded ALL declared BC1-3 mips (shared `decodeBcLevel`,
  per-level truncation fail, legacy `rgba` mirror of `mips[0]`, 8x8 2-mip
  + truncated-tail pins); Slice B added BC4/BC5-UNORM (DXT5-ramp reuse,
  ATI1/ATI2/BC5U FourCCs + DXGI 80/83, SNORM stays explicit-fail,
  uncompressed deferred to B2 with reason). Uncompressed + cubemaps stay
  explicit failures.
- Renderer Slice C: `setTextureMips` (authored-primary, no GenerateMips;
  `setTexture` kept as fallback for mipCount<=1/procedural),
  TANGENT@24/BITANGENT@36 wired wire-only into both layouts + VsIn
  passthrough (pixels byte-identical — PBR/IBL/skin pins are the
  tripwire), `drawMeshRange` + `drawMeshTexturedRange` (tested utilities,
  no production caller yet), aniso 1x -> 4x (8-16x needs sampler cache).
  Slice C2 deferred explicitly: remaining 10 range variants, per-submesh
  `materials[i]` SRV routing, sampler/SRV caches, async decode.
- App: `refreshGpu` routes authored (`mipCount>1`) vs fallback;
  `materials[0]`-only kept (per-submesh is C2, solution-judge: N-draws,
  never atlas — atlas would break hidden-submesh isolation + SMD/MSM
  material groups + uv0-only intake + zero-dep core).
- Tests: `render.offscreen_72B_viewport_pass` (production path finally
  pinned: 64x64 + 32x32 resize readback; backbuffer-only gap closed),
  `core.gpu_tangents_nonzero` (90% orthogonal + fallback pin),
  `cli-orient-two-bone` + `cli-gr22smd-missing` + `cli-fbx2smd-missing`
  (exit-2 negatives, zero fixtures).
- Verified: release `/W4 /WX /FS` clean, `ctest --preset
  windows-release` 15/15 green, unit binary 162/162 (release AND debug).

## Wave 28 (2026-09-23) — quaternion sampling + compression, .ani verdict

- Sampling: `AnimKey` parallel `quat` track (`fromEulerXyz` at capture,
  dual-written `rot` kept), `sampleBone` single shared helper
  (`slerp -> toMatrix -> eulerXyzFromRotation` at the boundary, then
  untouched `rebuildSkeletonRuntime`), `bakeClipFrames` calls the same
  helper (two-copy preview-vs-bake hazard deleted structurally),
  `upper_bound` segments preserving clamp. Euler UI/gizmo/SMD I/O
  unchanged; frozen math untouched (`local*parent`, `M^-1RM`, Y-up).
- Wave-local test-design failure fixed via protocol:
  `wrap_pi_parity_with_quat` compared raw eulers across the +-pi branch
  (-3.07 vs 3.21 = same rotation + 2pi); now compares modulo 2pi with
  the lesson recorded in the test comment.
- Compression (stored representation only; bake stays lossless):
  `reduceClipKeys` (greedy, endpoints kept, resample-verified bounds,
  stats like `removedMass` honesty), smallest-three N=14 default
  (hemisphere fold, error bound), per-track position range N=16
  (degenerate = constant). `.ani` verdict: stays EXPORT-ONLY (no
  importer/CLI verb; format bytes pinned by `ani_export_bytes_pinned`;
  glTF samplers are the designated successor, prove-or-retire in Wave 29
  resolved as keep-export-only with the pin).
- Verified: 15/15 + 162/162 green release AND debug.

## Wave 29 (2026-09-23) — bridge closeout + FBX dedup + glTF 29a import

- Hygiene closeout (4 gaps from the audit): per-call unique bridge log
  (PID+counter, was PID-only `CREATE_ALWAYS` truncation), extractor
  PID+counter tmp + remove-before-launch + remove on every failure +
  `exitCode!=0` fail-closed, sync+async Noesis import fail-closed on
  non-zero exit (never installs a failing bridge's file), `!is_open` /
  `!started` litter removes, GR2 export probes tool before staging.
  Pins: `bridge_per_call_log_unique`, `bridge_extractor_tmp_unique_
  and_clean`, `bridge_nonzero_exit_fails_closed` (all hermetic).
- FBX: exact-hash indexed dedup (pos/nrm/uv/bits-exact, quantized
  weights; no welding epsilon) + `RepairStats` surfaced via
  `conversionNote` (was `(void)rs`); ninja stays 3208 tris / 90 bones
  with verts < 9624 (soup `3*tris` retired); 21/25 model-set gate
  untouched.
- Workspace restore round-trip extended (all four fields already
  survived — no prod fix needed): locks + brush.radius + fovY +
  selection sets asserted on the reimport path.
- glTF 29a import (`m2rig_gltf` adapter, cgltf pin `85cd6238` verified
  via GitHub API, `/W3 /WX-` like OpenFBX, `M2RIG_WITH_CGLTF` switch;
  `cgltf_validate()` never called — CVE-2026-75148 bypassed by design,
  own overflow-checked `accessorRange`): indexed TRIANGLES only,
  <=4 JOINTS/WEIGHTS + removedMass, IBM bit-faithful, TRS->euler,
  Y-up no arbitration + warn-only orient gate, baseColorTexture URI +
  PBR factors, animations/meshopt/draco explicit `NOT_SUPPORTED_YET`,
  `.gltf`+`.glb`, `gltf2smd` CLI verb (build-only, no fixture — same
  standing as `fbx2smd`). TWO root-cause prod bugs caught by the new
  round-trip test and fixed via protocol: (1) `mat4FromColumnMajor`
  did a verbatim copy (translation parked in column 3, rotations
  untransposed = inverse for the row-vector engine) — now true
  transpose `R = M^T` with the proof in the comment; (2) `materialFor`
  dropped authored metallic/roughness factors (spec defaults 1.0/1.0
  survived) — now copied after the finite check. C4456 shadow pair
  (`w`, `raw`) fixed pre-gate. Export 29b deferred explicitly.
- Tests: `gltf.roundtrip_cube_skin` (runtime-synthesized .glb, no
  fixture) + `rejects_nonindexed_explicit` +
  `rejects_meshopt_draco_explicit`.
- Verified: 15/15 + 162/162 green release AND debug.

## UI unification S1-S4 (2026-09-23) + new skills/agents

- S1 honesty gates: Frame unified disabled-without-asset (all 3 sites,
  shared tooltip), Export/Weights verbs + Flood/Prune/Mirror/Transfer
  guards with AllowWhenDisabled reasons, Deform+DQS surfaced in toolbar
  (same bools, gpuDirty on toggle), Settings Save-Now kept-honest +
  slider range unified 0-30.
- S2-S4: toolbar Undo/Redo/Validate, Project-menu export parity
  (FBX/ANI/GR2->FBX/LOD/batch via existing handlers, prefs lodRatio),
  timeline keyboard gate unified to the stricter predicate, cheatsheet
  +3 rows (box-select, additive, paint preconditions), dead sweep
  (`statusBar` fn, `boxSelectCandidates` use, Settings duplicate
  Export/Tools/Autosave sections). Deferred with reasons: gizmo
  op/space dedup (needs `app.hpp`), `UISettings` unread flags,
  panel-slider prefs unification (behavior-adjacent).
- 5 new skills (`threejs-parity`, `cz-localization`, `release-ops`,
  `mse-effects`, `weight-table`) + 4 owning agents, indexed in
  `docs-skills.md`. Three.js verdict (evidence, not hype): NO embedded
  webview (zero in-repo evidence, archived R3F prototype depended on a
  never-shipped `localhost:8000` backend); native three.js-grade slice
  list instead (ACES/exposure, env-intensity, normal-map, per-submesh,
  orbit parity).
- `.gitignore` extended with the push-blocker set (dist/, zips,
  `*.gr2.ms`, `*.m2learn`, granny2.dll, grnreader exe, session.lock).

## Round A (2026-09-24) — Slice C2 renderer + 29b emit + per-submesh + msm core

- C2 renderer: all 10 range variants (signatures fixed for the panels
  track) + `PsTexPbrNormal` TBN shader with bind/unbind plumbing
  (unbound byte-identical) + aniso sampler cache (SRV hash cache
  deferred with reason: per-key lifetime aliasing, no measured win) +
  `range_parity` + `normal_map_bound_vs_unbound` pins. Wave-local build
  failure fixed via protocol: HLSL blob crossed MSVC's 16 KB single-
  literal limit (C2026) — split into A/B halves with runtime concat
  (`fullShaderSrc`, all 12 compile sites, boundary in whitespace).
- 29b emit: `smd2gltf` (`.glb` single-BIN, ≤4 gate, IBM, PBR factors,
  basename PNG/JPEG URIs; `.gltf`-external + samplers deferred) + CLI
  verb + round-trip/overlimit/layout tests + 2 more negative suites.
  TWO root-cause prod bugs caught by tests and fixed via protocol:
  writer IBM transpose (verbatim row-major, `S[rr*4+c]==M[rr][c]`) +
  reader shared-prim accessor-tuple reuse (multi-material emit no
  longer duplicates verts).
- Per-submesh routing (panels): static-textured ranged structure +
  lazy probe + fallback (skinned/PBR/whole-draw preserved); uploads
  pending in `refreshGpu` — NOT claimed as preview. MSE Effects tab:
  open/tree/play/scrub + 200-particle overlay tick (first
  `MseRuntime::update` caller, headless-safe).
- msmToSmd core: geometry-shell (bones/binds/materials, triangles
  empty BY HONESTY — zero-filled positions would be placeholder;
  autorig cannot fill downstream: needs positions MSM never stores) +
  3 test groups. NO `msm2smd` verb: a gated verb could never exit 0
  (`MESH_EMPTY` Error) — deferred to MDE geometry, documented.
- Verified: 17/17 + 170/170 green release AND debug.

## Round B (2026-09-24) — G4 un-deferred + weight table + layout + rules

- G4: pure `resolveDrawPath` (`app.hpp`, core-linkable) + `drawSceneContents`
  rewired to switch on it (identical calls) + `tests/test_app.cpp`
  112-combo routing matrix. The 4-line CMake wiring is the
  orchestrator's (reported exactly).
- Weight table v1 (Weights-panel section, no new persisted flag):
  filter (bone/min-weight/name) + per-row Norm/Sel + bulk
  normalize/prune (one undo each, same guards as App ops) + CSV export
  + `ImGuiListClipper` virtualization.
- Layout presets Rig/Paint/Anim/Review (commented 4x14 table, instant
  apply) + live dock rebuild (`g_dockBuilt` + setter; both Reset paths
  restart-free; compositing untouched). Paint keys + `tr()` i18n
  deferred with reasons (gesture arbitration load-bearing; string
  churn with no compiler in parallel tracks).
- Rules: `ZUp_YBackward` winding FIX (was broken: det -1 mirror flips
  normals — generic det3<0 index swap + test) + STATIC_PROP advisory
  (Warning, <=2 joints + >=1000 verts; bipeds unaffected, exit 0).
- Independent audit: GO-WITH-NOTES (no prod blocker; this entry +
  guide + skills + counts are the incorporated notes).
- Verified: 17/17 + 170/170 green release AND debug (with test_app
  wired).

## Round C (2026-09-24) — multi-material preview + glTF anim + msm verb + cache

- Per-material preview COMPLETE: `refreshGpu` uploads every resolvable
  material (`<assetId>#mat<i>`, authored/fallback rule, stale-key
  release) + per-material NORMAL maps (`#nmat<i>`, linear) via new
  `MaterialRef::normalTexturePath`; PBR path binds per-submesh albedo
  + normal (static + skinned ranges), clears only when bound (no-normal
  frames call-identical). Metal/rough maps NOT implemented (no shader
  input — factors-only, stated in slider tooltips). Materials panel:
  per-material normal field + probes; 2 s TTL probe cache (resolve
  uncached — plug-in media correctness). Wave-30b TODO removed.
- glTF animation both directions (fps=30 convention documented):
  reader linear TRS import (joint-only, morph/scale/STEP/CUBIC explicit
  fail, empty = no frames) + writer sampler emission (`--anim` on
  `smd2gltf`, bone-count mismatch fails) + 6 tests.
- `msm2smd <in.msm> <bind.smd> <out.smd>` + `cli-msm2smd` suite:
  intermediate contract (validation report informational + loud
  shell-only note, exit 0) — no gate (shell has no geometry by
  honesty). Wave-local test-design failure fixed via protocol: the
  chain test coupled Model-children-count to skeleton size (real
  sample.msm differs) — ref resolution is the real gate, count
  coupling removed with the lesson in the comment.
- SRV content-hash cache (FNV-1a + dims + srgb + mips; shared views +
  refcounts; 256 MB cap with never-evict-live rule + dedicated
  fallback; cumulative stats) + dedup/accounting tests. Eviction
  order honestly unpinned (would need >256 MB test allocs).
- Deferred with reasons: glTF GUI import (needs `App::importGltfFile`
  in app_state.cpp — panels-only would fork the FBX path), command
  palette (gated on the above), full `tr()` rollout, paint keys.
- 2 new skills (`gltf-animation`, `texture-cache`) + 2 agents.
- Verified: 18/18 + 180/180 green release AND debug.

## Round D (2026-09-24) — UI-perfect pass + glTF import + UV findings

- Layout rework (verified in-tree): toolbar View|Display|Rig|Status|
  Panels groups with wrap, overlay two-row wrap + shading scroll,
  status priority collapse, toast stacking, centered empty-state,
  Export/Assets flow sections + batch scroll, gizmo-dedup to the Gizmo
  panel, LOD slider on `prefs.lodRatio`, Theme palette (ok/warn/err/
  info/danger single source), `frameWholeModel` unifying all 5 Frame
  surfaces (overlay keeps smooth flight), `tipFor` tooltip pass on
  Export/LOD/self-learn/batch/Validate/Del. Load-bearing viewport
  paths untouched. Deferred: font scaling (raster proof), dock minimums
  (no DockBuilder API — scroll regions are the substitute), full `tr()`.
- `App::importGltfFile` (gated include, CLI-chain parity, fail-closed
  without session clobber) + dead `showMenuBar/showToolbar/showStatusBar`
  deleted (zero reads verified) + round-trip test. Required CMake fix:
  `M2RIG_WITH_CGLTF` was never defined for core/exe (success branch
  unreachable) — core define + exe link added, mirroring OpenFBX.
- UV findings (all non-blocking by design): `MESH_UV_RANGE` Info census
  (tiling is normal), `MESH_UV_DEGENERATE` Warning (matches tangent
  fallback), `MESH_UV_OVERLAP` Info (O(T) hash, order-independent) +
  5 tests incl. reorder determinism. Non-finite UVs stay under the
  existing `MESH_NON_FINITE` Error (no duplicate rule).
- 2 new skills (`layout-system`, `command-palette`) + 2 agents.
- Verified: 18/18 + 186/186 green release AND debug.

## Next tasks (remaining: palette, i18n-full, paint keys, formats)

- Command palette (Ctrl+K fuzzy table over existing handlers; spec in
  the new skill), full `tr()` rollout + `cs.json`, paint-station keys,
  `.gltf`+external `.bin`, draco/meshopt decode, morph targets,
  eviction-order pin, async decode + placeholder.
- Old roadmap lines below are superseded where struck; history kept.

  (Superseded 2026-09-24 — all landed: per-material uploads +
  per-submesh normals, `msm2smd` verb, sampler emission, SRV cache;
  see Round A/B/C above.)

  (Superseded 2026-09-24 — all landed, see Round A/B above: Slice C2
  ranges + normal-map PS + sampler cache; `smd2gltf` emit; MSE Effects
  tab; layout presets + live reset; weight table v1; G4 un-deferred;
  `ZUp_YBackward` winding fix; static-prop advisory; learn/analyze
  negative suites. Still open: per-material uploads, `msm2smd` verb,
  sampler emission, palette, paint keys, full i18n.)

- Waves 15+: DQS skinning, morph targets, PBR metallic/roughness,
  keyframed timeline + `.ani`.
- Waves 16-20: .mse/.mde particles, auto-rig wizard, LOD (needs
  adjacency first), material atlas packer.
- Waves 21+: MCP stdio server, Lua API, AVX2, 100+ CTest matrix
  (currently 94 checks + 8 CLI suites).
- Wave 22 follow-ups (scoped, not started): gizmo Parent orientation
  (custom delta mapping + tests), bone multi-select + selection sets,
  per-vertex weight table, MSE parser UI/CLI/test wiring, PBR backend
  for the `PbrMaterial` model, FBX global-axis auto-detect
  (`detectFbxCoordSys` is a live extension point returning nullopt).
