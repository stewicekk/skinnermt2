# Specialized Agents & Skills — 10x Upgrade Program

> Companion k `docs/UPGRADE_10X_BRAINSTORM.md`. Definice 10 nových skillů
> (S1–S10) a 10 specializovaných agentů včetně kontraktů, entry-pointů,
> test-gatů a failure-protokolů. Konvence skillů dle
> `.opencode/skills/docs-skills.md`. Žádný skill nesmí tvrdit hotové to,
> co kód neumí (poučení z `rendering/SKILL.md` 48 B driftu).

---

## S1 — `pbr-rendering` (vlny 24, 25)

**Agent: `render-modernizer`** (vlastní: RHI abstrakce, shadery, pipeline
state, post, stíny; nevlastní: skinning palette obsah, texturový obsah).

**Kontrakt (pravda v kódu dnes):**
- `GpuVertex` 72 B (offsets 0/12/24/36/48/64), `static_assert`
  `include/m2rig/renderer.hpp:36-42`. Tangent/bitangent dnes nuly.
- Frame CB 128 B (32 floatů: gWvp + gViewRot + gLightViewAndAmbient);
  každý `Map(DISCARD)` přepisuje všech 32 přes `cachedSecondHalf[16]`
  (`src/renderer.cpp:131-134`).
- Solid rasterizer `CULL_NONE` load-bearing pro mixed-winding
  (`src/renderer.cpp:296-302`).
- `PbrMaterial` (`renderer.hpp:45-83`) data-only; žádná backend metoda ho
  nekonzumuje (`renderer.hpp:134-137`). Header claim "PBR shading"
  (`renderer.hpp:2-3`) je over-claim do opravy.
- Offscreen viewport: `ensureViewportTarget/beginViewportPass/
  endViewportPass/viewportSrv/readViewport/frameStats`
  (`renderer.hpp:114-125`); aspect z **logických** `avail` (`panels.cpp`),
  target z **fyzických** `rect`; backbuffer fallback fyzické pixely.

**Entry points:** `Renderer::init/resize/beginScenePass/setSceneView/
drawMesh/drawMeshWireOverlay/drawMeshTextured/drawLines/drawLinesXRay/
present/readBackbuffer/ensureViewportTarget/beginViewportPass/
endViewportPass/readViewport/viewportSrv/frameStats`,
`buildGpuVertices*/buildGridLines/filterVisibleIndices`.

**Cílové chování (po vlnách 24–25):** `IRenderer` + `D3D11Renderer`;
`PsPbr` (GGX + IBL Split-Sum); `setPbrMaterial` + material CB; `b0/b1`
CB split; `R16F` offscreen + MSAA resolve + ACES; failure-atomic resize s
half-res pádovou cestou.

**Failure handling:** stop expanze při kritickém validačním failu,
reproduce → root-cause → fix → regrese → pokračovat. WARP smoke musí
projít vždy; offscreen pixel-proof (`readViewport` non-clear >100 px)
povinný pro každý render PR.

**Test gate:** `offscreen_72B_viewport_pass`, `deformed_dqs_upload`,
`resize_64_to_128`, `oversize_8193_rejected`, `aspect_uses_logical`.

---

## S2 — `gpu-skinning` (vlny 24, 28)

**Agent: `skinning-engineer`** (vlastní: palette buildery LBS/DQS,
vertex stream layout, CPU fallback; nevlastní: shaderová strana palety).

**Kontrakt dnes:** CPU `buildSkinningPalette/buildDqsPalette`
(`src/skin_weights.cpp:866-875,950-960`); CPU deform
(`deformVertex/deformNormal[Dqs]`, `:884-910,962-1072`); viewport
uploaduje deformované kopie každý frame (`src/app/app_gpu.cpp:62-92`);
VS nemá bone inputy (`renderer.cpp:36-46`); `useDqs` toggle
(`app.hpp:107`); CPU path závislostně čistý (`mesh_views.cpp`) = šev pro
testy/headless.

**Cíl:** `BLENDINDICES (4× u8) + BLENDWEIGHT (4× fp)` vertex kanály;
`StructuredBuffer<float4x4> gBones ≤256` + dual-quat `float2x4` path;
palette upload 1×/frame; CPU buildery jen fallback. Author-8/export-4
model (defaulty zvednout, clamp až na export gate s `removedMass`).

**Test gate:** `palette_bind_identity` (bind → identita), LBS vs DQS
parita na rigid (rozdíl <1e-4), `gpu_cpu_parity` na sample armor.

---

## S3 — `modern-textures` (vlny 25, 27)

**Agent: `texture-modernizer`** (vlastní: decode, sRGB, mipy, samplery,
cache, per-submesh binding; nevlastní: PBR BRDF matematika).

**Kontrakt dnes:** decode BC1-3 base-mip only
(`dds.hpp:2-5`, `dds.cpp:101-141`); 0× `_SRGB` v repu; 1 globální
`activeTexture` (`renderer.hpp:119`) + `materials[0]` only
(`app_gpu.cpp:113`); tangent 0 callers; sampler `LINEAR/WRAP/aniso-1`
(`renderer.cpp:253-263`); `GenerateMips` na UNORM (`:737-739`); sync
decode na UI vlákně; 5× `exists()` na materiál na frame
(`panels.cpp:1535-1540`); `resolveTextureFile` ancestor-walk
(`app_gpu.cpp:17-42`).

**Cíl:** albedo `UNORM_SRGB`, data mapy linear, authored-mip upload,
content-hash SRV cache, `geometryDirty/materialDirty` split, aniso 8–16×
+ sampler cache, `drawMeshRange` per-submesh, LRU 256–512 MB, 2 s TTL
probe cache, BC4/5 + uncompressed, pak BC6H/7 + KTX2, `xatlas`
export-only, `MESH_UV_RANGE/OVERLAP/DEGENERATE` findings.

**Test gate:** `srgb_roundtrip` (128 sRGB → linear ≈0.216), DXT3 nibble,
7×5 edge, DX10 dxgi=77, truncated-mip2 → fail, `tangent_consumed`.

---

## S4 — `quaternion-anim` (vlna 28)

**Agent: `anim-modernizer`** (vlastní: klipy, samplování, bake, komprese,
`.ani` rozhodnutí; nevlastní: deform preview).

**Kontrakt dnes:** euler-XYZ lerp + `wrapDelta`, explicitně ne slerp
(`anim.hpp:1-8`, `anim.cpp:143-218`); `Quat::slerp` existuje mrtvý
(`math.hpp:300-312`); lineární segment search; clamp bez loop módů;
clip in-session, bake nahrazuje frames mimo pose-undo (`app.hpp:308-310`);
`.ani` write-only bez importerů/testů/CLI; 0× komprese.

**Cíl:** paralelní quat track (capture `fromEulerXyz`, sample slerp/
squad, euler až na export hranici); Hermite/cubic toggle;
loop/clamp/ping-pong; binární search; key-reduction + 16-bit/smallest-3
kvantizace; `ani2smd` + round-trip **nebo** `.ani` smazat (prove-or-retire).

**Test gate:** existující euler testy zůstávají + `slerp_midpoint`,
`wrap_pi_parity`, `bake_matches_preview`, `ani_roundtrip` (nebo delece).

---

## S5 — `spatial-index` (vlna 26)

**Agent: `geometry-engineer`** (vlastní: KD-tree/BVH core, transfer,
mirror-pairing, auto-rig vzdálenosti, LOD náklady; nevlastní: UI).

**Kontrakt dnes:** "KD-tree" header vs brute-force tělo
(`skin_weights.cpp:540`); `dst×src×k` (`:544-562`); donor table brute
(`:607-629`); self-learn druhá vnořená smyčka
(`self_learning.cpp:394-471`); mirror O(n²) greedy, tolerance 1e-4,
one-way, `axis` ignorován (`skin_weights.cpp:367-421`); auto-rig nearest
segment + `1/(d+eps)²` (`:482-528`); LOD shortest-edge + full re-scan
(`lod.cpp:87-108`); 0× heat/geodesic.

**Cíl:** jeden KD-tree/BVH core pro transfer+mirror+auto-rig
(O(n log m)); voxel-geodesic + heat/biharmonic solver nad Wave-17
topologií; QEM LOD s heap + weight-space penalty + boundary locks;
mirror tolerance absolutní + bbox-relativní, bidirectionální + repair;
`(length,minId,maxId)` tie-break + `lod_orphans` zachovat.

**Test gate:** `transfer_determinism`, `mirror_tolerance_scaled`,
`qem_no_boundary_erosion` (ninja 3208→1603 kontrola), `geodesic_no_bleed`
(prsty/nohy syntetika).

---

## S6 — `gltf-pipeline` (vlna 29)

**Agent: `format-modernizer`** (vlastní: glTF import/export, nativní GR2
reader, FBX hardening/ufbxi, MSM inverse, MSE wiring, `.ani` verdikt,
bridge hygiena; nevlastní: herní GR2 emit — zůstává
`NOT_SUPPORTED_DIRECTLY`).

**Kontrakt dnes:** SMD nejsilnější (`smd.cpp` tolerance/fallbacky/xref/
gate); FBX pin `4d4a45a0`, úzký (žádné animace `:419`, soup `:325-326`,
materiály name-only); GR2 bridge-only + `gr2_deep_parser.cpp` scaffold
mimo import cesty; MSM export-only + indent-dialekt + `bool` parse;
MSE parse+CLI ano, UI ne; `.ani` write-only; coordsys Wave-22 pinned +
2 stubby (XUp fail, detect-nullopt); CLI 16 verbů / header 7 / CTest 8.

**Cíl/sekvence:** bridge-hygiena (unique Noesis tmp!) → nativní GR2
(clean-room, golden bone-by-bone) → FBX hardening/ufbxi + `fbx2smd`
ctest → glTF 2.0 (`cgltf`, `gltf2smd/smd2gltf`) → meshopt (Draco až po
potřebě) → `msm2smd` + braces-autorita → MSE Effects tab + `update`
wiring → `.ani` verdikt → coordsys cleanup → USD preview (tinyusd).

**Test gate:** `gr2_golden_warrior_m` (37 kostí, rigid distance SANE),
`gltf_roundtrip_ninja` (počty + váhy + bind), `fbx2smd` ctest suite,
`msm_brace_nesting`, `mse_golden_real`, `ani_roundtrip_or_deleted`.

---

## S7 — `viewport-ux` (vlna 30)

**Agent: `viewport-ux-engineer`** (vlastní: viewport panel, overlaye,
picking matematika, kamera, zkratky, pie/paleta; nevlastní: render
backend, core váhy).

**Kontrakt dnes:** `drawViewportPanel:2062-2466` (~404 řádků), vše v
`panels.cpp` (3067); picking/box/label math v anonymous namespace
(netestovatelné); kamera DPI-fixed Wave-23 (pole-reflection, fmod yaw,
tilt guardy Top/Bottom) + testy `camera_orbit_*`,
`camera_apply_preset_view_mapping`, `grid_blue_axis_*`; smooth ruší
každý dotek; overlay-vs-orbit residuum (button+pick double-fire);
paint Ctrl+drag vs Ctrl+click konflikt; weight single-slider;
multi-select data ano, opy ne; `SHBrowseForFolderA` ANSI bug
(`file_dialog.cpp:121`); 0× i18n; dark-only; 0× pie.

**Cíl:** Blender-grade header `[Shading▾][Overlays▾][View▾]`, view-cube,
grid/snap popover, frame-graph; input router
`Gizmo > Overlay > Box/Paint > Orbit/Pan`; shortest-arc yaw + damped
flights + `clipAuto` toggle; stroke objekt + `[/]` size + `Shift`-smooth/
`Ctrl`-subtract + live highlight; virtualized weight table; median-pivot
bulk gizmo; `Q`-pie + `Ctrl+K` paleta + keymap editor; picking math do
core s testy; per-panel TU split.

**Test gate:** `box_project_roundtrip`, `pick_bone_root_clickable`,
`yaw_shortest_arc`, `overlay_router_priority` (headless kde lze).

---

## S8 — `workspace-restore` (vlna 30)

**Agent: `session-engineer`** (vlastní: `.m2rig` projekt, autosave,
recovery, workspace layouty, undo architektura, selection sets,
lokalizace persistentních dat; nevlastní: ImGui docking internals).

**Kontrakt dnes:** `saveWorkspace/loadWorkspace` + `assets[]` reimport
(Wave-18), brush/kamera/ortho/fov persist; undo `InfluenceSnapshot`
full-copy cap 50 (`app.hpp:441-449`) + per-dab push; locks/hidden/sets
by-name persist (`project_file.cpp:475,542-573`); autosave `.tmp`+rename
+ `session.lock` recovery; 14 panel flags v `user_prefs.json` vs docking
v `imgui.ini` (2 zdroje); reset vyžaduje restart.

**Cíl:** single `workspace_layout.json` (ini + flags + kamera) + named
`Rig/Paint/Anim/Review` + per-`.m2rig` layout + live reset; sparse
stroke-coalesced undo (touched-vert deltas + pose masky, oddělené
stacky); multi-bone ops přes `selectedBones`/sets; `cs.json` lokalizace
(cs first) s migrací perzistentních klíčů.

**Test gate:** `workspace_restore_reimport` (oběma směry, existující),
`undo_coalesced_stroke` (N dabů = 1 entry), `sets_survive_reload_apply`.

---

## S9 — `build-ci` (průběžně)

**Agent: `build-engineer`** (vlastní: CMake/presets/CI/packaging/test
harness/perf baseline/security; nevlastní: feature kód).

**Kontrakt dnes:** floor 3.21, schema v6, 2 presety, VS2022-only;
FetchContent bez hash/SBOM; CI `windows-2022` 2-job bez cache/shard/
artifactů; `m2rig_core` zero-dep + `/W4 /WX /FS` + PDB-lock (vynuceno
dokumentací, ne CI); god-files; `Result<T>` model (deleted `ok()`);
1 CTest = 100 casů; debug >30 min triage (`test_main.cpp:15-16`) bez
flagu; 8+ verbů bez CTest; 0× sanitizer/fuzz/bench; `package-windows.ps1`
bez verzovaného jména/sha-sidecaru; `.gitignore` bez `dist/`.

**Cíl:** floor 3.28, schema v8+, Ninja + VS, `windows-2025` +
`ubuntu-24.04-headless` (null-RHI), ccache, `ctest --parallel --timeout`,
artifacty; vcpkg/CPM+hash + SBOM + Dependabot; `M2RIG_HEAP_TRIAGE`
(default OFF, fast <2 min); suite-per-CTest; wire `gr22smd/orient/
fbx2smd`; ASan/UBSan + libFuzzer corpus z `tests/data/*`; nanobench
baselines + `ctest -L perf`; streaming caps + SARIF; `Result` +
`[[nodiscard]]`/`and_then` (bez lámání hard-rule).

**Test gate:** `docs-check` job zelený, `ci_matrix` (win dbg+rel + linux
headless) zelená, `perf_budgets` (ninja LOD ≤0.3 s) drží.

---

## S10 — `docs-brain-sync` (průběžně)

**Agent: `brain-keeper`** (rozšířený; vlastní: AGENT_STATE, brain,
changelog, guide truth-passy, skill kontrakty, BENCHMARKS.md, fuzz
corpus manifest; nevlastní: cokoli v `src/`).

**Kontrakt dnes:** `docs/AGENT_STATE.md` autoritativní (Wave-23, 626
řádků); `brain/README.md` stale ("waves 1-14, 94/94"); drift tabulka §6.1
(7 položek); `USER_GUIDE:203-205` stale (DQS/MSE); `main.cpp:2` 7 vs 16
verbů; 79 skill entries, `C:\rigapp` guard stale; `BENCHMARKS.md`
neexistuje; sync manuální.

**Cíl:** `docs-check` CI (sizeof→skill codegen, `usage()`→cli-reference
codegen, brain-vs-AGENT_STATE lint, skill front-matter validator);
truth-pass každého releasu (guide vs kód); `BENCHMARKS.md` s čísly každé
perf vlny; skill audit (archiv legacy `*.md`, promote 5 nativních).

**Povinné první kroky (tato syntéza):**
1. Refresh `rendering/SKILL.md` (48 B → 72 B + offscreen kontrakt) —
   AGENT_STATE Wave-23 to explicitně žádá.
2. Bump `brain/README.md` pointer na Wave-23.
3. Založit `brain/BENCHMARKS.md` s baseline (ninja LOD 0.14 s).

---

## Mapa agent → soubory (vlastnictví pro implementační vlny)

| Agent | Primární soubory | Nesmí lámat |
|-------|------------------|--------------|
| render-modernizer | `src/renderer.cpp`, `include/m2rig/renderer.hpp`, `src/mesh_views.cpp`, `tests/test_render.cpp` | WARP smoke, 72 B layout, offscreen kontrakt |
| skinning-engineer | `src/skin_weights.cpp` (palette/deform), `src/app/app_gpu.cpp`, `src/mesh_views.cpp` | CPU fallback path, ≤4 export gate |
| texture-modernizer | `src/dds.cpp`, `include/m2rig/dds.hpp`, sampler/`setTexture` v rendereru, `app_gpu.cpp` resolve | `NOT_SUPPORTED` honest fails, DoS caps |
| anim-modernizer | `src/anim.cpp`, `include/m2rig/anim.hpp`, `math.hpp` Quat | euler UI kompatibilita, bake==preview |
| geometry-engineer | `src/skin_weights.cpp` (transfer/mirror/autorig), `src/lod.cpp`, `src/mesh.cpp` topologie | determinismus, `removedMass` reporting |
| format-modernizer | `src/fbx/*`, `src/adapters/*`, `src/gr2_deep_parser.cpp`, `src/ast/*`, `src/mse.cpp`, `tools/cli/main.cpp` | bridge boundary, Y-up, `M⁻¹RM`, `local*parent` |
| viewport-ux-engineer | `src/app/panels.cpp` (viewport část), `include/m2rig/camera.hpp`, `src/app/file_dialog.cpp` | DPI fixy Wave-23, tilt guardy, Front=+Z |
| session-engineer | `src/app_state.cpp`, `src/workspace/*`, `include/m2rig/app.hpp` | undo honest kontrakty, locky, `.m2rig` compat |
| build-engineer | `CMakeLists.txt`, `CMakePresets.json`, `.github/*`, `scripts/*`, `tests/*` | `/W4 /WX /FS`, zero-dep core, PDB disciplína |
| brain-keeper | `docs/*`, `brain/*`, `.opencode/skills/*` | single-source `AGENT_STATE.md` |

**Orchestrace:** master orchestrátor čte `AGENTS.md` + `brain/` před
rozhodnutím, deleguje specialistům, `solution-judge` na alternativy
(Front/Back lekce z Wave-23: důkaz > návrh), wave-gates (build+testy
před expanzí), evidence `soubor:řádek`, nikdy fake-success.
