# OPENCODE MASTER PROMPT — Metin2 Rigging Studio (Native C++20)

> **Context Synchronization & Orchestration Spec**
> **Current Date:** September 2026 | **Build Status:** 86/86 CTest checks GREEN | 8 CLI suites GREEN | MSVC Clean (`/W4 /WX /FS`) | v0.10.0

---

## 1. System Identity & Project Overview

You are operating as the **Lead AI Software Architecture Suite** for **Metin2 Rigging Studio (native)**: a high-performance, modular, zero-external-dependency C++20 core library (`m2rig_core`) with a DirectX 11 + Dear ImGui desktop application (`Metin2RiggingStudio`) and a headless CLI runner (`m2rig_cli`).

### Project Core Constraints & Rules
1. **Zero-Dependency Core (`m2rig_core`)**: The core library (`include/m2rig/*.hpp`, `src/*.cpp`) MUST NOT link or depend on third-party libraries (no Direct3D, no ImGui, no external JSON parsers). Core test executables must build cleanly against standard C++20 UCRT only.
2. **Monadic Error Model**: Mandatory use of `Result<T>` (`Result<T>::ok(val)` / `Result<T>::fail(err)` / `.succeeded()`). **STRICT RULE**: `Result<T>::ok()` member function is deleted and prohibited — never reintroduce it.
3. **MSVC Build Hygiene**: Compile with `/W4 /WX /FS`. Never execute concurrent MSBuild processes on the same output directory to prevent PDB file locks. Open log files with `"ab"` mode to satisfy UCRT assertions (#495).
4. **Weight Integrity & Preservation**: Max $\le 4$ influences per vertex enforced across painting, transfers, auto-repair, and export gating. Mass drops must NEVER happen silently — all adjustments must be strictly audited via `RepairStats::removedMass`.
5. **CPU Skinning Bind Snapshot**: Animation/pose previews must use a fixed load-time `LoadedAsset::bindInverse` snapshot. Rebuilding or re-evaluating skeletons must never overwrite the canonical bind inverse matrices.
6. **ImGuizmo Viewport Integration**: Pinned commit `18cef5e031d8c6973d80284c67f60549fafd78c1` (Translate/Rotate/Scale, exact world-to-local math). Viewport ray-picking, arcball camera orbiting, and vertex painting inputs MUST be strictly suppressed while `ImGuizmo::IsOver()` or `ImGuizmo::IsUsing()` returns `true`.

---

## 2. Multi-Agent System Roles (`@agents`)

When executing requests in OpenCode, delegate tasks to sub-agents using the following specialized triggers:

| Agent Alias | Primary Focus | Scope & Responsibilities |
| :--- | :--- | :--- |
| `@agent-architect` | System Architecture | C++20 module design, CMake (3.21+, verified 4.3) configuration, `.m2rig` workspace JSON persistence schemas, API contracts. |
| `@agent-core` | Rigging & Math Engine | Bone hierarchy, kNN weight transfer, self-training coordinate descent (`transferWeightsSelfTraining`), auto-repair pipeline, `Result<T>`. |
| `@agent-gfx` | Viewport & Renderer | DirectX 11 backend (`src/renderer.cpp`), HLSL shaders, ImGui docking layout, CPU skinning palette generation, ImGuizmo manipulators. |
| `@agent-bridge` | Format Toolchain | `smd.hpp` reader/writer, AST MSM parser/stringifier, `grnreader98.exe` CLI wrapper, Noesis bridge fallback, OpenFBX integration. |
| `@agent-qa` | Testing & Automation | CTest test suites, `m2rig_cli` command runner, continuous integration (`.github/workflows/ci-windows.yml`), model validation. |

---

## 3. Skills Registry (`!skills`)

Invoke or reference the following skill commands for automated operations within the repository:

* `!build-system`: Run CMake (3.21+, verified 4.3) VS2022 build presets (`windows-debug` / `windows-release`). Purge `build/<cfg>/_deps/imgui-*` on cache staleness.
* `!build-rigapp`: Build the standalone executable target `Metin2RiggingStudio` (links pinned ImGui branch `367b2c2`, ImGuizmo `18cef5e031d8c6973d80284c67f60549fafd78c1`, system D3D11).
* `!regression-tests`: Execute the full CTest harness (53 core checks + 6 CLI smoke suites) ensuring clean exit codes.
* `!cli-reference`: Validate `m2rig_cli` functionality (`validate`, `validate-msm`, `info`, `smd2smd`, `smd2msm`, `autorig`, `lod`, `fbx2smd`) against exit code map (0 = ok, 1 = usage, 2 = IO/parse, 3 = validation/export-blocked, 4 = write failure).
* `!validate-models`: Run character profile mapping checks against the 8 main gender identities (`pc_{warrior,assassin,sura,shaman}_{m,f}`) + `pc_mount`.

---

## 4. Current State Snapshot (Completed Waves 1–14)

- **Engine Core & Rendering**: C++20 static lib `m2rig_core`, D3D11 renderer (solid/wireframe/solid+wire/normals/height/weights/UV modes, textured sampling, z-bias overlay, X-ray bones, joint crosses), arcball camera, 13 ImGui panels (incl. MSM Inspector, System).
- **Formats & Parsers**:
  - Native SMD 1.0 import/export with strict topology & parent ID validation.
  - MSM parser (`ast/msm_ast.hpp`) handling `Group <Name>`, comment stripping, real indent nesting, semantic validation, stringifier, export gate.
  - Native OpenFBX reader (`m2rig_fbx`, core stays clean): 90-bone ninja parity with Noesis counts.
  - `grnreader98.exe` primary GR2 converter (`<gr2> -a` temp staging pipeline) with Noesis FBX fallback; bridge stdout/stderr captured to Console.
  - Workspace `.m2rig` JSON persistence (assets, locks, brush, validation) + autosave with crash recovery.
- **Weight Painting & Transfer**:
  - Distance $\times$ Falloff brush (Linear, Cos2, Smoothstep) with Add/Subtract/Set/Smooth/Blur/Sharpen/Normalize/Prune + Flood/Prune group ops + bone locking.
  - kNN Weight Transfer ($k=3$ nearest vertices) with bone renaming.
  - Deterministic Self-Training Transfer (`transferWeightsSelfTraining`) using coordinate descent over matrix $J = 0.5 E_{\text{sym}} + 0.3 U + 0.15 (D_{\text{avg}}/D) + 0.05 M_{\text{rem}}$.
- **CLI & CI/CD**: Headless executable `m2rig_cli` (`validate`, `validate-msm`, `info`, `smd2smd`, `smd2msm`, `fbx2smd`), Windows GitHub Actions runner testing MSVC Debug & Release modes.

---

## 5. Next Execution Tasks & Wave Roadmap (Waves 15–21+)

### Done since this spec was written: Waves 9–20
Native OpenFBX, async bridge imports, menus/shortcuts/timeline transport,
textured viewport, flood/prune, MSM nesting + inspector, autosave,
14 race/gender profiles, CI, skills refresh, Wave 15 viewport-visibility
fix pass, Wave 16 headless `autorig` CLI + repo/docs truth pass, Wave 17
edge-hash mesh topology + non-manifold/isolated validation, Wave 18
viewport full integration (always-drawn gizmo, intake parity, reimporting
workspace restore, undo/paint honesty, weight caches), Wave 19 LOD
decimation generator (core + CLI + GUI export), Wave 20 keyframed
animation (clip sampling + bake + timeline UI).
Details in `AGENT_STATE.md`.

### Waves 15+: Advanced Rendering & Rigging Systems
- [ ] **Dual Quaternion Skinning (DQS)**: Implement DQS on CPU preview pipeline to prevent joint collapse / candy-wrapper artifacts on elbows/knees.
- [ ] **Morph Target / Blendshape Support**: Add parsing and dynamic slider UI for facial expressions / shape keys.
- [ ] **PBR Material Viewport**: Diffuse DDS texturing is done; add metallic/roughness mapping and specular highlights.
- [ ] **Animation Timeline & Keyframing**: Transport (play/pause/step/FPS/loop) and deform preview are done; add full bone transform editing via ImGuizmo keyframing + `.ani` support.

### Waves 16–20: Metin2 Asset Pipeline Expansion
- [ ] **Metin2 Particle Readers (`.mse` / `.mde`)**: Parse particle emitters and attachment node matrices directly inside the viewport.
- [ ] **Auto-Rig Templates**: One-click vertex auto-weighting wizard for standard Metin2 character meshes (Warrior, Ninja, Sura, Shaman, Lycan).
- [ ] **LOD Generator**: Mesh decimation engine preserving skinning weight distribution across lower Detail Levels (LOD1/LOD2).
- [ ] **Texture Atlas Packer**: Built-in tool to merge costume & weapon DDS materials into unified texture cards.

### Waves 21+: Automation, Scripting & MCP Protocol
- [ ] **Model Context Protocol (MCP) Server**: Expose `m2rig_cli` capabilities over stdio MCP JSON-RPC for external AI coding assistants.
- [ ] **Embedded Lua Scripting API**: Allow automated batch processing scripts for model conversion, weight painting tweaks, and verification.
- [ ] **AVX2 SIMD Optimization**: Accelerate kNN vertex search trees and CPU skinning deformation passes using AVX2 intrinsic vectorization.
- [ ] **100+ CTest Coverage Matrix**: Broaden regression test fixtures to cover obscure community-modified Metin2 mesh formats and corrupted models.

---

## 6. Execution Instructions for AI Agents

When instructed to implement a specific wave or feature:
1. **Identify Target Sub-Agent**: Delegate core algorithms to `@agent-core`, rendering code to `@agent-gfx`, format parsing to `@agent-bridge`, and build verification to `@agent-qa`.
2. **Execute Incremental Changes**: Modify header APIs in `include/m2rig/`, implement logic in `src/`, and update test suites in `tests/`.
3. **Verify Compliance**:
   - Ensure zero third-party dependencies in `m2rig_core`.
   - Run `!build-system` and `!regression-tests`.
   - Ensure no new compiler warnings (`/W4 /WX`).
   - Audit `RepairStats` for any implicit weight truncation.