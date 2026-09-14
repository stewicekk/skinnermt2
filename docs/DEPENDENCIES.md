# Dependencies

Single source of truth for every external dependency. Core rule:
`m2rig_core` has ZERO third-party dependencies (standard C++20 UCRT only).

## BuildRequires (configure time, network needed once)
| Dep | Pin | License | Notes |
|---|---|---|---|
| Dear ImGui (docking branch) | `367b2c24f399988ddafc0bb4628da0106bcc09be` | MIT | FetchContent tarball; `imgui_lib` built `/W3 /WX-` |
| ImGuizmo | `18cef5e031d8c6973d80284c67f60549fafd78c1` | MIT | FetchContent populate-only (upstream CMakeLists NOT configured); `imguizmo_lib` `/W3 /WX-`; disable with `M2RIG_WITH_GIZMO=OFF` |

The ImGuizmo pin was verified via GitHub API (an earlier suggested pin
404'd; upstream moved sources to `src/`, hence populate-only + both
include dirs).

## System (no download)
DirectX 11 + D3DCompiler + DXGI + DWMAPI + ComDlg32 (Windows SDK, VS2022
MSVC 19.39, `windows-debug`/`windows-release` presets).

## Runtime tools (shipped in repo, never linked)
| Tool | Path | License/notes |
|---|---|---|
| Noesis 4.44 | `noesis/Noesis.exe`, `Noesis64.exe` | Rich Whitehouse; FBX bridge via `?cmode`; GR2 plugins need absent `granny2.dll` |
| grnreader98 v1.4.0.3 + granny2.dll | `Data/resources/Convert gr2 to mesh/...` | Pesmontis 2010; GR2->SMD batch `<gr2> -a` |
| Metin2 samples | `Data/Models/` (31 GR2 + FBX pairs, ~80 DDS) | game data, git-ignored binaries |

## Test-only
`tests/data/two_bone.smd`, `tests/data/sample.msm`,
`tests/ninja_excerpt.inc` (offline fixtures, committed).

## Explicitly NOT dependencies
ONNX/torch/SQLite/GTest/Assimp/.NET — deterministic engine only; GTest
replaced by the dependency-free `tests/` harness so offline configure works.
