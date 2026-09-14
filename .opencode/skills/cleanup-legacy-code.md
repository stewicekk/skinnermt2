# Skill: Cleanup Legacy Code

## Policy
Legacy prototypes live in `_archive/` (React frontend) and are reference
only. The maintained product is the native C++ tree
(`CMakeLists.txt`, `include/`, `src/`, `tools/cli/`, `tests/`).

## Rules
- Never delete `_archive/` without explicit user approval.
- SMD/MSM/weight logic of record: `src/smd.cpp`, `src/ast/msm_ast.cpp`,
  `src/skin_weights.cpp` — validate via `m2rig_cli validate/info/smd2smd/
  smd2msm` (`tools/cli/main.cpp`), not by porting old scripts.
- Do not reintroduce C# / `dotnet` / `C:\rigapp` paths anywhere.

Legacy note: this file previously described `C:\rigapp` C# parsers
(`SmdParser`, `ObjParser`, `LoadFromNpz`); none exist here.
