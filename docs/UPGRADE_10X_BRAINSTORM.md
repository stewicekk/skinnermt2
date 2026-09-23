# 10x Upgrade Brainstorm — Metin2 Rigging Studio (native)

> Vzniklo 2026-09-21 jako syntéza 6 paralelních specializovaných agentů
> (gpu-engineer, texture-engineer, rigging-engineer, metin2-engineer,
> ux-engineer, architect). Každý výrok níže má oporu `soubor:řádek`
> ověřenou v kódu — žádné dohady, žádný fake-success.
> Vstupní pravda: `docs/AGENT_STATE.md` Wave 1–23
> (100/100 + 8/8 green, v0.10.0), `brain/README.md`, `AGENTS.md`.

---

## 0. Executive summary (CZ)

Projekt je po 23 vlnách **funkčně kompletní, ale technologicky zamrzlý
cca v roce 2012**: D3D11 FL11_0 forward renderer s Blinn-Phongem, CPU
skinning s full re-uploadem každý frame, ASCII-only formátová pipeline,
euler-lerp animace, jeden globální texturový slot, god-file UI
(`panels.cpp` 3067 řádků). Všechno funguje a je to poctivě otestované —
ale každý z těchto bodů je dnes 5–50× pomalejší a vizuálně chudší, než
musí být. Níže je **prioritizovaný plán vln 24–30**, kde každá vlna je
samostatně releasovatelná, každá má regresní testy a žádná nerozbíjí
Wave-22 root-cause fixy (`M⁻¹RM` konjugace, `local*parent` kompozice,
Y-up kanonika — ty jsou **zmrazené** a každá vlna je musí držet zelené).

### Pořadí vln = poměr (uživatelský zisk × odblokování downstream) / riziko

| Vlna | Název | Klíčový důkaz dluhu | Viditelný zisk |
|------|-------|---------------------|----------------|
| 24 | GPU skinning + RHI abstrakce | `app_gpu.cpp:62-92` CPU deform + upload/frame | 60 fps playback na 7k-tri GR2 |
| 25 | PBR backend + sRGB + IBL základ | `renderer.cpp:29-89` Blinn-Phong, 0× `_SRGB` v repu | armor vypadá jako armor, ne plastelína |
| 26 | Prostorový index (KD-tree/BVH) | `skin_weights.cpp:540` "Brute-force kNN (n is small…)" | transfer/mirror/auto-rig O(n·m)→O(n log m) |
| 27 | Texturová pipeline (mipy, aniso, per-submesh) | `renderer.hpp:119` 1 globální slot, `materials[0]` only | multi-materiálové armory konečně správně |
| 28 | Quaternion animace + komprese | `anim.hpp:1-8` "not quaternion slerp" | žádné flipy, 5–10× menší klipy |
| 29 | glTF 2.0 import/export | 0× `gltf` v kódu, write-only `.ani` | moderní interchange, konec ASCII-soup pro velké scény |
| 30 | Viewport UX + workspace layouty | `panels.cpp:1-3067` god-file, 2 zdroje layoutu | Blender-grade viewport, CZ lokalizace |

Detaily každé vlny + nové skills + noví agenti: níže.

---

## 1. Rendering / viewport engine (gpu-engineer)

### 1.1 Nalezené dluhy (vše ověřeno)

1. **Blinn-Phong místo PBR** — `src/renderer.cpp:29-89` (`kShaderSrc`,
   `PsMain`/`PsTextured`): hardcoded `pow(nh,64)*0.35`, ambient
   `0.1/0.1/0.15*0.35`, `V=(0,0,1)` view-space hack, `F0=albedo*0.04`
   bez metalness. `PbrMaterial` (`renderer.hpp:45-83`) je potvrzeně
   data-only — `renderer.hpp:134-137` explicitně říká "no backend method
   consumes PbrMaterial yet". Header `renderer.hpp:2-3` přitom tvrdí
   "PBR metallic/roughness shading" — **over-claim, opravit comment**.
2. **72 B vertex nese 24 B nul** — `GpuVertex` tangent@24 + bitangent@36
   (`renderer.hpp:28-36`), ale input-layout je skipne
   (`renderer.cpp:265-276`, comment přiznává). `mesh_views.cpp`
   nikdy neplní tangent (vždy nuly). `computeTangents()`
   (`src/mesh.cpp:249-296`) má **0 callerů** v celém repu.
   CPU `mesh.hpp:23-24` je `Vec4+t.handedness`, GPU `vec3+vec3` —
   konverze nedefinovaná nikde.
3. **Per-draw `Map(DISCARD)` 128 B Frame CB** —
   `renderer.cpp:422,471,580,767,796,832,855`: každý draw/begin/view
   přepisuje všech 32 floatů (WVP + `cachedSecondHalf[16]`). ~5–8 Mapů
   na frame, každý potenciální stall.
4. **`CULL_NONE` berlička** — `renderer.cpp:301` kvůli mixed-winding.
   Skrývá flipped normály, zdvojuje overdraw, láme budoucí stíny.
5. **Žádné MSAA/HDR/tonemap/stíny/SSAO/post** — swapchain i offscreen
   `R8G8B8A8_UNORM, Count=1` (`renderer.cpp:192-202,502-510`). 0×
   `shadow/SSAO/tonemap/ACES/MSAA` mimo audit (grep).
6. **Žádný GPU skinning** — `app_gpu.cpp:56-109` CPU paleta + deform +
   `uploadMesh` (full `CreateBuffer`) při každém `gpuDirty`. Timeline
   playback = CPU O(verts×bones) + GPU alloc/frame. Header
   `renderer.hpp:2-5` stále tvrdí "GPU skinning palette arrives in
   wave 6" — **lež od Wave-14, přepsat comment**.
7. **Offscreen realloc bez hystereze** — `renderer.cpp:484-546`:
   exact-match jinak synchronních 5 alloců uprostřed framu; `Reset()`
   první → failed `Create` = černý viewport (není failure-atomic).
   Cap 8192 bez OOM fallbacku. `readViewport:609-637` full-res staging
   copy + `Map(READ)` stall bez renderer-side throttle.
8. **Žádný frustum culling / instancing** — `panels.cpp:1991-2054`
   kreslí grid+mesh+overlay+bones vždy; `buildGridLines()` alokuje
   `vector<GpuVertex>` **každý frame** (`:2000`); přitom `Mesh::bounds
   + boundingSphereRadius` (`mesh.hpp:51-53`) existují.
9. **WARP smoke-test necvičí produkční cestu** — `tests/test_render.cpp`
   jen backbuffer `beginScenePass/readBackbuffer`, nikdy
   `ensureViewportTarget/beginViewportPass/readViewport`, nikdy DQS/deform,
   nikdy `resize()`, nikdy aspect≠1 ani DPI.
10. **Skill drift** — `.opencode/skills/rendering/SKILL.md:14` tvrdí 48 B
    vertex; pravda 72 B (`renderer.hpp:36`). Wave-23 note v AGENT_STATE
    už říká "Skill needs a refresh pass" — **opravit v této vlně**.

### 1.2 Upgrade (Vlna 24 — RHI + GPU skinning, Vlna 25 — PBR)

- `IRenderer` interface (`init/resize/beginPass/draw/upload/present`)
  + `D3D11Renderer : IRenderer`; HLSL přes DXC (SM 6.x) místo
  `D3DCompile`; pak D3D12/Vulkan backend bez dotyku `panels.cpp`.
- GPU skinning: `BLENDINDICES/WEIGHT` (4× u8 + 4× fp = 8 B) +
  `StructuredBuffer<float4x4> gBones (≤256)` + DQS dual-quat path;
  `app_gpu.cpp` přestane realokovat VB; CPU deform zůstane jako
  headless/test fallback (`mesh_views.cpp` je ideální šev — už je
  dependency-free).
- PBR: `PsPbr` (Cook-Torrance GGX + Schlick-GGX + Split-Sum IBL
  `prefilteredEnv/BRDF_LUT/irradiance`), `setPbrMaterial` + material CB,
  UV offset/scale/rotation z `PbrMaterial:80-82` konečně konzumovány.
- Frame-graph: `b0 frame (1×/pass)` + `b1 draw (WVP per-draw)`,
  dirty-flag lighting → konec `cachedSecondHalf` symptomu.
- Offscreen: hystereze (Δ>12 px nebo 250 ms debounce), grow-only
  POT pool, atomic resize, half-res OOM pádová cesta + toast.
- Swapchain: `FLIP_DISCARD, BufferCount 3`, `R16F` scene target + MSAA
  4× + `Resolve` + `ACES + FXAA` resolve quad; `CULL_BACK` default po
  intake-time `fixWindingConsistent()` (Wave-17 topologie už umí hrany).

---

## 2. Textury / materiály (texture-engineer)

### 2.1 Nalezené dluhy

1. **Decode jen BC1-3 base-mip** — `dds.hpp:2-5` kontrakt; `dds.cpp:101-141`
   rejectne BC4/5/6H/7, uncompressed, KTX2/ASTC/ETC/HDR. Authored mipy
   zahozeny (`dds.hpp:17` "only level 0 decoded"), runtime regeneruje
   přes `GenerateMips` — ztráta artist-tuned mip tails + load-time cost.
   `test_dds.cpp` nemá DXT3-explicit-alpha, non-4-aligned, DX10-header
   ani truncated-tail testy.
2. **sRGB špatně v celém řetězci** — 0× `_SRGB` v repu; swapchain, target
   i textura `UNORM` (`renderer.cpp:196,507,716`). `PsTextured` počítá
   lighting v gamma-space. **Každý texturovaný pixel je špatně.**
   `GenerateMips` na `UNORM` filtruje v gamma-space (klasický bug).
3. **Jeden globální `activeTexture`** — `renderer.hpp:119`,
   `renderer.cpp:749-763`; `app_gpu.cpp:113` uploaduje jen `materials[0]`;
   Materials panel edituje všechny (`panels.cpp:1513-1523`), viewport je
   ignoruje — **UI/preview mismatch**.
4. **Tangent pipeline odpojená** — viz §1.1 bod 2. K tomu: `mesh.hpp:26-29`
   `uv1/hasUv1`, `color/hasColor` deklarovány, ale `mesh_views` je ignoruje
   (color vždy přepsáno procedurálně); `fbx_reader.cpp:310` píše jen `uv0`.
   Rozhodnout: wired `TEXCOORD1` + color passthrough, nebo smazat pole —
   deklarováno-ale-ignorováno = tichá ztráta dat.
5. **Sampler z roku 2005** — `renderer.cpp:253-263`: `LINEAR, WRAP,
   MaxAnisotropy=1`, jeden globální. Grazing-angle shimmer na každém
   pohledu na podlahu/plášť.
6. **Sync decode na UI vlákně + žádná cache** — `app_gpu.cpp:115-119`
   `readDdsFile + setTexture` při každém `gpuDirty` (každý úhoz do
   texture-path textboxu = re-decode). `panels.cpp:1535-1540` dělá až
   5× `std::filesystem::exists` **na materiál na ImGui frame** (Wave-18
   cache explicitně deferred). `resolveTextureFile:17-42` ancestor-walk
   O(depth) syscallů.
7. **Žádný atlas packer** — roadmap přiznává (`AGENT_STATE.md:619`).
8. **UV-checker je vertex-color, ne textura** — `mesh_views.cpp:41-49`:
   `floor`-fold skrývá out-of-[0,1] overflow; žádná `MESH_UV_RANGE`
   validace (`mesh.cpp:105-183` kontroluje jen finiteness).

### 2.2 Upgrade (Vlna 27)

- P0: sRGB path (albedo `UNORM_SRGB` SRV, data mapy linear, sRGB-aware
  mipy, `PsFlat` pro debug módy). 1 shader + 3 formáty + test.
- P1: per-submesh draw (`drawMeshRange` + SRV na `materials[i]` +
  material CB) — reuse existujících map, jen key-scheme + range.
- P2: zapojit tangent (volat na importu, kopírovat do `GpuVertex`,
  `TANGENT vec4` input) nebo kanály smazat — žádné třetí "platíme 24 B
  za nuly".
- P3: authored-mip upload + content-hash SRV cache + split
  `geometryDirty/materialDirty`.
- P4: aniso 8–16× + per-material sampler cache (WARP fallback 4×).
- P5: normal/rough/metal/AO/emissive sloty (glTF-2.0-minimal) + fix
  header over-claimu; `clearcoat/anisotropy/transmission` explicitně
  `NOT_SUPPORTED` dokud není IBL.
- P6–P10: async decode + LRU 256–512 MB, TTL probe cache (kopie System
  2s patternu), BC4/5 + uncompressed, pak BC6H/7 + KTX2, atlas (`xatlas`,
  export-only) + `MESH_UV_RANGE/OVERLAP/DEGENERATE`.

---

## 3. Rigging / animace / weights (rigging-engineer)

### 3.1 Nalezené dluhy

1. **Euler-lerp animace, quaternion existují ale spí** —
   `anim.hpp:1-8` "not quaternion slerp"; `sampleClip` component-lerp +
   `wrapDelta` (`anim.cpp:143-170`); `bakeClipFrames` duplikuje stejnou
   matematiku (`:172-218`). `Quat::slerp` (`math.hpp:300-312`) nikdy
   nevolán z `anim.cpp`. Segment search lineární (`:153-154`),
   exact-key lookup O(keys) na frame. Clip in-session only
   (`app.hpp:55-56`), bake **nahrazuje** `animFrames` a není v pose-undu
   (`app.hpp:308-310`).
2. **CPU skinning** — viz §1.1 bod 6. DQS preview toggle (`useDqs`,
   `app.hpp:107`) platí CPU+upload cost dvakrát (dva palette buildery,
   dvě deform cesty: `skin_weights.cpp:866-875,950-960,884-910,962-1072`).
3. **"KD-tree" transfer je brute-force** — header slibuje KD-tree
   (`skin_weights.hpp:173-189`), tělo přiznává "Brute-force kNN (n is
   small for armor; KD-tree partitioning is wave 26)"
   (`skin_weights.cpp:540`). Vnitřek `dst×src×k` (`:544-562`).
   `SelfLearningTransfer::transfer` druhá vnořená smyčka
   (`self_learning.cpp:394-471`). Ninja (2070 verts) a warrior_m (7426
   tris) už to cítí.
4. **Self-train: greedy + klonování meshe na každý eval** —
   `Mesh tmp = dstMesh` na kandidáta (`skin_weights.cpp:743`) →
   `O(iters·bones·cands·dst·k)` alokací. Fixní ascending order, ≤6
   kandidátů na kost (`:764-793`), cap `maxIter ≤25`. Dva paralelní
   transfer stacky (`skin_weights.cpp` self-train vs `self_learning.cpp`
   DB transfer s `std::stof/stoul` bez guardů `:27-97`,
   `lastUpdated=0.0 // TODO` `:334-335`, no-op
   `analyzeBonesPresets:660-678`). DB persistuje per-vertex indexy —
   křehké po LOD/decimaci.
5. **≤4 influences hard-coded jako authoring limit** —
   `kMetin2MaxInfluences=4` (`skin_weights.hpp:21`), enforce už při
   paintu (`skin_weights.cpp:267-351`). Moderní pipeline: author 8+,
   quantize/export-clamp (UE5 8, Unity N-authored). Kód ničí 5.+
   influenci při paintu místo uchování pro re-export. Parametr
   `maxInfluences` je přitom provlečen všude — stačí přestat defaultovat
   na 4 (`PaintParams::maxInfluences`, `skin_weights.hpp:99`).
6. **Žádný heat/geodesic auto-rig** — `autoRigMesh` = nearest segment +
   `1/(d+eps)²` (`skin_weights.cpp:482-528`). Bleed přes mezery (prsty,
   nohy, armor-vs-body). `grep heat|diffusion|geodesic|voxel|biharmonic`
   = **0 produkčních hitů**.
7. **LOD = naive shortest-edge** — `lod.hpp:1-15` přiznává boundary erosion
   a UV-seam volby; cost `(length,minId,maxId)`, full re-scan na kolaps →
   `O(collapses·tris)` (`lod.cpp:87-108`). Midpoint shrinkuje objemy.
   Bez quadrik, bez skinning-aware costu.
8. **Weight "table" = jeden vertex slider** — `panels.cpp:1446-1497`,
   `selectedVertex:int` (`app.hpp:87-88`); auto-reset skáče na vertex 0
   při switchi (`:1448-1450`). Žádný bulk/filter/sort/CSV.
9. **Multi-select existuje, opy ho ignorují** — `selectedBones`,
   `selectBoneHierarchy`, name-keyed sets + persist
   (`app.hpp:63-80`, `app_state.cpp:310-373`, `project_file.cpp:475,542-573`),
   viewport Ctrl-toggle + box-select (`panels.cpp:143-153,2180-2258`) —
   ale gizmo/paint/heatmap/flood berou jen `selectedBone` primary.
   `boxSelectCandidates` plněn nikdy (dead field `:2115`). Wave-22
   follow-up "bone multi-select + selection sets" not-started.
10. **Undo = full-mesh snapshoty, cap 50** — `InfluenceSnapshot`
    celé influences + celá pose na entry (`app.hpp:441-449`,
    `app_state.cpp:682-698`); `erase(begin)` O(n) shift; každý paint dab
    = jeden snapshot (Wave-18 "per-stroke undo coalescing" deferred).
11. **Mirror tolerance fixní `1e-4`, O(n²), one-way** —
    `skin_weights.hpp:121-122`, greedy first-fit `skin_weights.cpp:367-401`,
    `mirrorMeshWeights` kopíruje `a→b` only, `axis` param ignorován
    (`(void)axis`, `:403-421`). Na scaled/rotated importech selhává.
12. **Žádná komprese animací** — `grep compress|quantiz` = jen DDS
    (`dds.cpp:139`). `.ani` frame = `bones×9×4 B` raw float32.
13. **`.ani` je write-only custom binary** — `"ANI " v1`
    (`anim.hpp:27-50`, `anim.cpp:35-102`); 0× `importAni/parseAni/readAni`;
    rotace euler-degrees bez order/convention header fieldu. Nelze
    reimportovat co vyexportuje. `USER_GUIDE.md:205` poctivě říká "No
    native .ani readers yet" — držet poctivost dokud není round-trip.

### 3.2 Upgrade (Vlna 26 — index, Vlna 28 — quaty)

1. **Quaternion sampling (euler UI zůstává)** — `fromEulerXyz` při capture,
   `Quat::slerp` při sample, euler až na bake/export hranici. 1 soubor
   (`anim.cpp`), primitiva hotová (`math.hpp:269-312`).
2. **GPU skinning** (viz §1.2) — největší perf win při playbacku.
3. **Jeden KD-tree/BVH core** nahrazující `skin_weights.cpp:540,607-629,
   367-401,497-508` brute smyčky → O(n log m); semínko: Wave-17
   `MeshTopology` edge-hash. Předpoklad pro geodesic distances.
4. **Author-8 / export-4** — zvednout defaulty, clamp až na SMD/`.ani`
   export gate s existujícím `removedMass` reportingem.
5. **Heat/geodesic auto-rig** nad Wave-17 adjacencí (Baran-Popović /
   biharmonic), current segment-distance jako initializer/fallback.
6. **QEM LOD** (Garland-Heckbert + heap + attribute/weight-space penalty +
   boundary locks); `(length,minId,maxId)` tie-break + `lod_orphans`
   kontrakt zůstávají.
7. **Sparse stroke-coalesced undo** — `{vertexId, before[], after[]}` jen
   touched verts + pose mask + idle coalescing; oddělené weight/pose stacky.
8. **Weight table v1** (virtualized `ImGui::Table`, filter/sort/bulk/CSV)
   na existujících `set/remove/normalizeVertex` + `boneHistogram_`.
9. **Multi-bone ops přes `selectedBones`/sets** — dokončit Wave-22 deferred.
10. **Komprese + `.ani` prove-or-retire** — key-reduction + quat kvantizace
    na bake path + `ani2smd` round-trip, nebo exporter smazat a
    standardizovat SMD frames + glTF samplers.

---

## 4. Formáty / konvertory (metin2-engineer)

### 4.1 Opravy prompt-předpokladů (agent ověřil)

- "Žádný nativní GR2 parser" = **half-true**: `src/gr2_deep_parser.cpp:1-670`
  existuje, ale není v **žádné** import cestě (jediný caller learning
  analyzer `self_learning.cpp:692-693,735`, 0 testů). Section model
  (32-B header + 16-B table, `:40-85`) neodpovídá Granny 2.x
  (type-tagged chunky, Oodle/zlib, pointer-fixup). `parseMeshBinding`
  jen `hasWeights=true` (`:223-232`), `parseVertexData/parseIndexData`
  no-op (`:388-396`), `toMesh` dropne weights (`:491-539`).
  **Zacházet jako speculative scaffold — přepsat, ne rozšiřovat.**
- "MSE unwired" = **částečně outdated**: parser + 6 testů
  (`test_mse.cpp:1-182`, `CMakeLists:108`) + CLI `validate-mse`
  (`main.cpp:178-232`, ctest `:153-154`, fixture `sample.mse`) existují;
  chybí **UI/viewport wiring** (0× `mseDoc/drawMse/importMse` v
  `app.hpp/panels.cpp`; `MseRuntime::update` `:333-375` má 0 callerů;
  `MseRuntime:356-368` přiznává representative-only částice).
  `USER_GUIDE.md:205` "No native .ani/.mse/.mde readers yet" je **stale
  pro MSE** — opravit guide.
- `.ani` write-only potvrzeno (§3.1 bod 13).

### 4.2 Stav po formátech

- **SMD nejsilnější**: case-insensitive `Version 1` (`smd.cpp:199-206`),
  rigid fallback (`:511-517`), sparse-id remap (`:471-488`, frames
  `:604-615`), xref hard-errors (`:303-342`), ≤4 writer gate (`:437-464`),
  round-trip diff (`:640-717`). Limity: `snprintf`-key dedup O(n)
  (`:540-552`, `%.5f` může mergovat near-identical verts); `parsePoseLine`
  failne celý soubor na 1 špatném floatu (`:135-147`, žádný recover-mode
  s line numbers — `SmdBone` nemá line field narozdíl od `MsmNode`);
  žádný skinning-metadata/tangent v writeru.
- **FBX native-first, úzký**: pin `4d4a45a0` 2023-era (`CMakeLists:62-63`,
  ne ufo0905 fork). `LIMB_NODE+NULL_NODE` (`fbx_reader.cpp:121-153`),
  deg→rad (`:157-161`); hard-faily: non-XYZ order (`:165-171`), empty
  skeleton (`:174-178`), unknown skin link (`:210-217`), bad CP
  (`:223-228`), non-finite (`:285-310`) — žádný partial import. **Žádný
  animation import** (`:419`), žádné blendshapes, soup output bez dedupu
  (`:325-326` — ninja 9624 verts místo ~1/3), materiály name-only
  (`:243-263`) navzdory `PbrMaterial`. Kaydara-variant + mesh-less
  Wolfman padají do Noesis (`test_weights.cpp:306-322`). Axis
  trust-but-verify 2× conversion cost (`:348-418`).
- **GR2 bridge-only, fragile by design**: primary grnreader98
  (`app_state.cpp:1300-1304`), fallback Noesis (`:1324-1354`). Hardcoded
  cesta s mezerou + version suffix (`gr2_adapter.cpp:222-240`), tool píše
  `<input>.smd` vedle inputu → temp-stage copy (`:259-280`), fixní flagy
  `"<staged>" -a -t` (`:281`, sémantika `-t` neověřená). Noesis import má
  **single shared tmp** `m2rig_bridge_import.smd`
  (`app_state.cpp:1337-1338`) — concurrent-import race. Noesis GR2 mrtvý
  (chybí `granny2.dll`). Export SMD→Noesis `?cmode`; FBX export hardcoded
  `-rotate 90 0 0` (`app_state.cpp:1190-1253`) — druhý stacked orientation
  hack vedle coordsys profilů. `extractWeightsFromGr2ViaBridge` je
  explicitní stub co vždy vrací `NotSupportedDirectly` (`:103-119`).
  `isValidGr2Container` jen 4 magic bytes (`:206-220`).
- **MSM export-only**: nesting fix Wave-12 (`stripComment` `:27-36`,
  `indent/2-1` `:94-100`), gate `validateMsmDoc` (`:299-368`), shared
  GUI≡CLI `buildMsmExport` (`:222-255`). Limity: indent-dialekt bet
  (tab/brace MSM se složí flat, `msm_ast.hpp:53-54`); žádný `msm2smd`
  import; lossy stringifier (`msm_ast.hpp:4-5`), `rawText` (`:42`) nikdy
  neplněn; `parseMsm` vrací `bool` ne `Result` (`:197-214`) — tiché faily.
- **Coordsys Wave-22 hardening reálný**: single source
  (`convertZUpToYUp` reuse, `coordsys.cpp:36-41`), `M⁻¹RM` fix s důkazem
  (`:186-204,220-231`), frames too (`:210-232`), `diagnoseOrientation`
  4-rule gate + `m2rig_cli orient`. Dva explicitní stubby zůstávají:
  XUp fail (`:61-79`, pinned test) a auto-detect nullopt by-design
  (`detectGr2CoordSys:95-100` vždy nullopt; `detectFbxCoordSys`
  `fbx_reader.cpp:70-88` jen 4 right-handed páry) → fallback
  `assumedSource ZUp_YForward`. `ZUp_YBackward` mirror (det −1, `:50-60`)
  neflipne winding — potential inside-out na rare path, netestováno.
- **CLI 16 verbů, header tvrdí 7**: `main.cpp:2-4` stale; `usage:37-56` +
  dispatch `:732-760` implementují `validate/info/smd2smd/smd2msm/autorig/
  lod/validate-msm/validate-mse/fbx2smd/gr22smd/orient/learn-*/self-learn-*/
  analyze-gr2-dir`. CTest jen 8 (`CMakeLists:149-168`); `fbx2smd` build-only;
  `gr22smd/orient/learn-*` 0 CTest; chybí `msm2smd/mse2*/mde2*/ani2smd/
  smd2ani/fbx2msm/gr22msm` a všechno `gltf/usd/draco/meshopt`.

### 4.3 Upgrade (Vlna 29 — glTF; předtím GR2 reader)

Sekvence: bridge-hygiena → nativní GR2 reader → FBX hardening/ufbxi →
glTF → meshopt → MSM inverse → MSE wiring → `.ani` prove-or-retire →
coordsys cleanup → USD preview.

1. **Nativní GR2 reader (clean-room, golden-tested) → primary**;
   grnreader98 → fallback. Odvodit reálný `29DE6CC0` layout z hexu +
   `grnreader98 -a` ground truth (warrior_m 37 bones/7426 tris, sura_m 36
   bindings); pin bone-by-bone golden testy. `granny2.dll` **nikdy** do
   linku (licence); emit zůstává `NOT_SUPPORTED_DIRECTLY`.
2. **glTF 2.0 import + export** (`.gltf`/`.glb`, `cgltf` single-header —
   sedí k zero-dep core étosu): indexed mesh (fix FBX soup), ≤4
   `JOINTS/WEIGHTS` + inverse-bind, quaternion samplers (náhrada `.ani`),
   Y-up mandate (konec axis arbitráže), PBR materiály (backend pro
   `PbrMaterial`). CLI `gltf2smd/smd2gltf`.
3. **FBX: hardening nebo ufbxi swap** — anim kanály + dedup + textury/PBR
   + refresh pinu s dated policy, nebo **ufbxi** (single-header, MIT,
   širší binary-version tolerance → Kaydara/Wolfman nativně). **FBX SDK
   rejected** pokud není Autodesk-exact export requirement. Doplnit
   `fbx2smd` ctest.
4. **meshopt first, Draco second** — quantized + `EXT_meshopt_compression`
   (single-header decoder); Draco jen pokud web-preview interop.
5. **MSM inverse** (`msm2smd`) + brace-aware gramatika (braces autorita,
   indent hint) + `Result` z `parseMsm` + `rawText` populate-or-drop.
6. **MSE viewport wiring (beze změny formátu)** — Effects Tools tab:
   attachment→bone resolver + texture probe + `MseRuntime` overlay draw;
   zapojit zero-caller `update`; real-`.mse` golden test; `"MDE "` binary
   verify-against-game nebo mark-studio-local; refresh `USER_GUIDE:205`.
7. **`.ani`: prove or retire** — `ani2smd` + round-trip, nebo smazat a
   standardizovat SMD frames + glTF.
8. **USD preview (read-only)** přes tinyusd — až po glTF; full authoring
   out-of-scope pro Metin2 studio.
9. **Coordsys cleanup na glTF vlně** — Y-up + `upAxis` eliminují detekci;
   XUp explicit-fail pattern zachovat; `ZUp_YBackward` winding fix-or-delete.
10. **Bridge hygiena (levné, hned)**: unique tmp pro Noesis import (zabít
    shared `m2rig_bridge_import.smd`), dokumentovat `-a -t`, rename-or-
    implement `extractWeightsFromGr2ViaBridge`, refresh CLI header +
    `USER_GUIDE:205`, chybějící ctesty.

---

## 5. UI/UX / kamera / workflow (ux-engineer)

### 5.1 Nalezené dluhy

1. **God-file** — `panels.cpp:1-3067`: všech 12 panelů + viewport + gizmo +
   picking + dialogy v jednom TU; helpery v anonymous namespace
   (`statusBar:40`, `boneSegments:48`, `drawSkeletonTree:128`,
   `updateBoneGizmo:170`, `doImport*/doExport*:318-486`,
   `pickMeshPoint:521`, `pickBoneAt:565`, toolbar `636-752`) = headless
   netestovatelné. Public facade jen 7 fn (`panels.hpp:17-32`). Největší:
   `drawViewportPanel:2062-2466` (~404), `drawAllPanels:2723-3065` (~342),
   `drawBoneProperties:1051-1346` (~295). Wave-23 split jen naming
   (funkce extrahovány, ale stejné TU). `drawSceneContents` vs
   `renderScene` duplikují `modelRadius` fallback `2.8f` (`:2279,2712`).
2. **Dock: 2 zdroje pravdy** — 14 panel flags (`app.hpp:156-180`,
   persist `user_prefs.json` `ui` sekce) vs docking (`imgui.ini`,
   `main.cpp:179-180` — takže `AGENT_STATE:109` "`IniFilename=nullptr`"
   je **stale**). Žádné named layouty (Rig/Paint/Anim/Review), žádný
   per-project layout, reset vyžaduje restart (`:2532,2829`), `dockBuilt`
   static neresetovatelný, split ratios hard-coded (`:2933-2947`,
   ne DPI-aware).
3. **Gizmo Parent = aproximace** — enum `World/Local/Parent` existuje
   (`app.hpp:41`), UI všechna tři (`:1300-1315,2590-2605`), ale Parent je
   draw-matrix alignment + per-op special cases (`panels.cpp:196-315`:
   Translate-only `:277-285`, `applyParentRotationDelta:287-290`,
   `drawScaleRatios*localScale:291-297` s "approximate" tooltipem).
   Wave-22 follow-up "gizmo Parent orientation (custom delta mapping +
   tests)" not-started. Single-bone only (`:184-186`), žádný pivot
   selector, žádný axis-lock, display knobs (`:1336,2627`) nikdy
   neforwardovány do ImGuizmo (dead knobs, žádný `SetStyle` call).
4. **Overlaye základní, ne Blender-grade** — buttons row + 2 text řádky
   (`:2377-2421`); 0× view-cube/shading-popover/grid-editor/clip-planes/
   stats-overlay/profiler/pie-menu (`pieMenu` 0 hitů). `frameStats()`
   drawCalls schované ve status baru.
5. **Kamera: smooth ruší každý dotek** — `orbit/pan/zoom` cancellují smooth
   (`camera.hpp:225,236,253`) → preset flight umře při zachvění myši.
   `presetSmooth` lerpuje yaw lineárně bez shortest-arc (možný 350° spin).
   `setOrthographic` flipne mode hned (`:373`) a přitom plní smooth struct.
   `orbit/pan` nikdy nevolají `updateClip` → near/far stale; Viewport
   Settings Near/Far slidery (`panels:2654-2655`) přepsány bez notice.
6. **Overlay-vs-orbit residuum** — Wave-23 fix (žádný `AllowOverlap`,
   globální `btnDown` + rect-hover `:2087-2109`) drží, ale press
   začínající na `Front` buttonu pořád nastaví `btnDown`; release <6 px
   nad buttonem může spustit button **i** bone-pick. `btnDown` reset
   (`:2261`) jen když `!mouseOverViewport && !MouseDown` — drag puštěný
   mimo okno stickne. Řešení: input router s prioritou
   `Gizmo > Overlay > Box/Paint > Orbit/Pan` + hit-test overlay rectu first.
7. **Paint UX: Ctrl+drag dab, žádný stroke** — gesture vyžaduje Ctrl/Shift
   (`:2125`), což koliduje s Ctrl+click multi-selectem ve stromu
   (`:143-153`) — stejný modifikátor, dvě významy. Per-dab undo (Wave-18
   coalescing deferred). Žádný falloff editor, pressure/tilt, spacing,
   gradient/line fill, zero-weights toggle, live affected highlight.
   Feedback jen brush ring (`:2447-2461`).
8. **Materials panel statuje FS každý frame** — viz §2.1 bod 6.
9. **Zkratky/toasty/status/empty-state** — hard-coded, žádný remap
   (`:2847-2878`); cheatsheet statická tabulka (`:2893-2913`); toast
   mutace během iterace (`:867-871`), žádné action buttons/history;
   `PixelProof` verdict se neukazuje ve viewportu (jen backend);
   empty-state `TextDisabled` místo buttonů (`:2422-2439`), žádný
   drag-drop target, žádné recent-files.
10. **Témata/lokalizace/a11y = 0** — jeden dark theme (`main.cpp:59-137`);
    `panelSpacing/panelRounding/compactMode` z prefs nikdy re-aplikovány
    na `ImGuiStyle`. 0× `locali|gettext|i18n` v `src/` — **CZ user base
    bez CZ UI**. Default ImGui font bez diacritics checku. `SHBrowseForFolderA`
    **ANSI** (`file_dialog.cpp:121`) — rozbije cesty s diakritikou
    (`D:\modely\brnění\`)! `GetOpenFileNameW` blocking modal (render loop
    stojí), žádný multi-select (`OFN_ALLOWMULTISELECT` absent — batch
    import po jednom), žádný thumbnail. Žádné
    `SetProcessDpiAwarenessContext(PerMonitorV2)` (`main.cpp:141-281`) —
    fonty blur na 150 % do restartu; okno fix `1600×950`. A11y: jen
    `NavEnableKeyboard`, žádný keyboard gizmo nudge, žádná color-blind
    paleta, žádný UI scale slider.

### 5.2 Upgrade (Vlna 30)

1. **Workspace layouty** — single `workspace_layout.json` (ImGui ini +
   flags + kamera), `Rig/Paint/Anim/Review` switcher v toolbaru, live
   reset, per-`.m2rig` layout.
2. **Blender-grade overlaye** — header `[Shading▾][Overlays▾][View▾]`,
   view-cube, grid/snap popover, frame-graph z existujících
   `fps+drawCalls`.
3. **Paint station** — stroke coalescing, falloff curve editor, `[/]` size,
   `Shift`-smooth / `Ctrl`-subtract (Photoshop konvence), live highlight,
   symmetry gizmo + topology mirror.
4. **Weight table v1** — virtualized + filter + bulk + CSV.
5. **Multi-select co něco dělá** — median-pivot bulk gizmo, "apply to
   selection", fix dead `boxSelectCandidates`.
6. **Parent gizmo pořádně** (Wave-22 follow-up) — unified
   `decomposeParentDelta` + shortest-arc + axis-lock + reálné ImGuizmo
   styly + testy.
7. **Pie menu + command palette + remap** — `Q`-pie, `Ctrl+K` nad
   existujícími `do*` fn, keymap editor v `user_prefs.json`.
8. **Material panel bez statů** — 2 s TTL probe cache, `Enter`-commit edit,
   DDS thumbnails, PBR backend.
9. **CZ lokalizace + a11y + témata** — `cs.json` + diacritics font +
   `SHBrowseForFolderW`, light/high-contrast, UI scale slider, keyboard
   nudge, color-blind-safe paleta. **Dialog ANSI→W je P0 bugfix.**
10. **Node workflow** (transfer→repair→validate→LOD→export canvas s live
    `report` badges; honest gate + never-silent mass reporting zůstávají).

Souborový split: `viewport/` (panel/overlays/picking) + `gizmo/` +
`panels/` (per-panel TU) + `shell/` (menubar/dock/shortcuts); čistá
matematika (`pickBoneAt`, box/label projekce) do `m2rig_core` s unit testy.

---

## 6. Architektura / build / testy / docs (architect)

### 6.1 Nalezené dluhy (+ drift tabulka)

- **Build floor starý**: `cmake_minimum 3.21` (`CMakeLists:1`), presets
  schema v6 (current v8–10), jen `windows-debug/release`, žádný Ninja/
  ASan/coverage/`linux-headless`, žádný vcpkg toolchain. FetchContent
  bez `URL_HASH`/offline/SBOM (`:62-63,178,206`); `_deps` staleness =
  manuální delete. `glob vcpkg*/conan*` = 0 hitů.
- **CI minimální**: `windows-2022` runner, matrix 2 presety, žádný cache/
  parallel/shard/retry/artifact/SBOM/sign/sanitizer/Linux
  (`.github/workflows/ci-windows.yml:1-18`). PDB-lock jen dokumentován,
  nevynucen.
- **Core/exe leak**: `src/app_state.cpp` (1971 řádků!) v core, zatímco
  `app.hpp:18` includuje `renderer.hpp` (exe koncept v core headeru);
  zachraňuje to jen `.cpp` separace. `panels.cpp` 3067 + `renderer.cpp`
  920 + `main.cpp` CLI 761 = god-files. Wave-23 split jen v rámci TU.
- **Žádná RHI abstrakce**: `d3d11.h` v `renderer.cpp:12`, `ID3D11*` 40+
  řádků, `HWND` v `renderer.hpp:17-18`, `ImGui_ImplDX11_Init` v
  `main.cpp:184-185`. Windows/MSVC-only záměrně, ale blokuje headless
  Linux CI i všechnu GPU práci.
- **Test harness**: dependency-free `expect.hpp:1-71` (záměrně, bez GTest)
  — ale bez fixtures/parametrizace/timeout/JUnit. **Debug full-run
  >30 min** kvůli intentional CRT heap-check triage
  (`test_main.cpp:15-16,29-33`, `AGENT_STATE:608-610` "not claimed
  green") — triage nemána za feature flagem. 1 CTest = 100 casů (žádný
  sharding); 8+ verbů bez CTest (`fbx2smd` build-only; `gr22smd/orient/
  learn-*` 0 CTest). Sanitizers 0×, fuzzing 0 (ač `diagnostics.hpp:10-21`
  limity + `IMPLEMENTATION_PLAN:59` "corrupted-file fuzz" future),
  benchmarky 0 (`benchmark.md` placeholder → `brain/BENCHMARKS.md`
  neexistuje; `AGENT_STATE:417` "no profilers in repo"; roadmap AVX2 bez
  baseline).
- **Error/model solidní, malé mezery**: `Result<T>` (`result.hpp:10-63`,
  `ok()` deleted `:29` — hard rule) bez `map/and_then`, bez
  `[[nodiscard]]`, bez numeric codes (strings only). `ValidationReport`
  bez stable codes/`repair()` callback/SARIF. `Logger` bez sink
  abstrakce/rotace/structured keys.

| Drift | Pravda v kódu | Co tvrdí doc | Fix |
|-------|---------------|--------------|-----|
| GpuVertex | 72 B (`renderer.hpp:36`) | 48 B (`rendering/SKILL.md:14`) | refresh skill (tato vlna) |
| DQS | CPU preview existuje (`app.hpp:107`) | "future work" (`USER_GUIDE:203-204`) | update guide |
| MSE/MDE | parse+CLI wired | "No readers yet" (`USER_GUIDE:205`) | split mse-ok vs ani-roadmap |
| CLI verbs | 16 (`main.cpp:37-56,742-760`) | 7 (`main.cpp:2`) | sync header z `usage()` |
| Brain | Wave-23 (626 řádků) | "waves 1-14, 94/94" (`brain/README:3,15`) | bump pointer |
| Skills | 79 entries, `C:\rigapp` guard stale | "25 rewritten" (`AGENT_STATE:223-225`) | audit: archiv legacy, promote 5 nativních |
| Master prompt | "86/86" (Wave-20) | "100/100" (`AGENT_STATE:561`) | counts z CTest output |

### 6.2 Upgrade (průběžně, gate každé vlny)

1. **RHI + null backend** (§1.2) — základ všeho GPU + headless Linux.
2. **Package mgmt + SBOM** — vcpkg manifest nebo CPM+URL_HASH,
   Dependabot rev-bumpy.
3. **CMake/presets/CI matrix** — floor 3.28, schema v8+, Ninja + VS,
   `windows-2025` + `ubuntu-24.04-headless` (core+CLI+null-RHI),
   ccache, `ctest --parallel --timeout`, artifact ZIP+logs.
4. **God-file split** — `src/ui/*`, `src/session/*`, `m2rig_app` lib;
   unity-build pro CI rychlost.
5. **Headless Linux** (po #1) — `M2RIG_WITH_UI=OFF` staví+testuje;
   null-RHI smoke místo WARP.
6. **Test scale-out** — `M2RIG_HEAP_TRIAGE` option (default OFF, fast
   path <2 min, nightly full-heap), suite-per-CTest, wire
   `gr22smd/orient/fbx2smd`, ASan/UBSan job, libFuzzer na
   `smd/msm/mse/dds`, nanobench deform/LOD baselines (ninja
   3208→1603 0.14 s jako baseline).
7. **Perf baseline + budgets** — tracy opt-in + `ctest -L perf` budgets,
   AVX2 kNN/deform za feature flagem s before/after čísly.
8. **Security** — streaming caps + SARIF export + OSS-Fuzz seed corpus
   z `tests/data/*`, log rotace.
9. **Docs/brain contract CI** — `docs-check` job: `sizeof(GpuVertex)`→skill,
   `usage()`→`cli-reference.md` codegen, `brain/README` vs `AGENT_STATE`
   lint, skill front-matter validator.
10. **Plugin/Lua/MCP (až po 1+4+5)** — `m2rig_ext` C ABI + `m2rig_cli
    --mcp-stdio` (JSON-RPC nad existujícími verby) + Lua (sol2) batche;
    core zůstává zero-dep.

### Neměnné (zmrazeno, každá vlna drží zelené)

- `Result<T>` model (deleted `ok()`, `succeeded()` query).
- `m2rig_core` zero-dep + `/W4 /WX /FS` + PDB-lock disciplína.
- Bridge-only GR2 boundary (`NOT_SUPPORTED_DIRECTLY`).
- Row-vector + Y-up kanonika + `M⁻¹RM` konjugace + `local*parent`
  kompozice (testy `coordsys_skeleton_conversion_preserves_bind`,
  `skeleton_composes_local_then_parent`, `orient_*`).

---

## 7. Sekvenční roadmap vln 24–30

```
24  RHI abstrakce + GPU skinning LBS/DQS ............ největší perf win
25  PBR backend + sRGB + IBL základ + skill refresh .. největší visual win
26  KD-tree/BVH core (transfer/mirror/auto-rig) ..... největší algo win
27  Textury: mipy/cache/aniso/per-submesh ........... correctness win
28  Quaternion anim + komprese + ani prove-or-retire . motion win
29  glTF 2.0 + nativní GR2 reader + bridge hygiena ... interchange win
30  Viewport UX + workspace + CZ + god-file split ... workflow win
+ průběžně: CI matrix, triage-flag, docs-check, BENCHMARKS.md, fuzz corpus
```

Každá vlna: reproduce → root-cause → fix → focused testy → regrese
(`ctest --preset windows-release --output-on-failure` + debug CLI 8/8),
evidence `soubor:řádek` + čísla (fps, ms, MB, %), nikdy fake-success.

---

## 8. Nové skills a agenti (definice v `docs/SPECIALIZED_AGENTS_AND_SKILLS.md`)

| # | Skill (adresář) | Pokrývá vlny | Nový agent |
|---|-----------------|--------------|------------|
| S1 | `pbr-rendering` | 24, 25 | `render-modernizer` |
| S2 | `gpu-skinning` | 24, 28 | `skinning-engineer` |
| S3 | `modern-textures` | 25, 27 | `texture-modernizer` |
| S4 | `quaternion-anim` | 28 | `anim-modernizer` |
| S5 | `spatial-index` | 26 | `geometry-engineer` |
| S6 | `gltf-pipeline` | 29 | `format-modernizer` |
| S7 | `viewport-ux` | 30 | `viewport-ux-engineer` |
| S8 | `workspace-restore` | 30 | `session-engineer` |
| S9 | `build-ci` | průběžně | `build-engineer` |
| S10 | `docs-brain-sync` | průběžně | `brain-keeper` (rozšířený) |

Detaily kontraktů, entry-pointů, failure-protokolů a test-gatů: viz
`docs/SPECIALIZED_AGENTS_AND_SKILLS.md`. Oprava driftu
`rendering/SKILL.md` (48 B → 72 B + offscreen kontrakt) je součástí
této syntézy jako první committable krok.
