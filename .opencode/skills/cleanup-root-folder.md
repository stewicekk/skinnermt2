# Skill: Cleanup Root Folder

## Canonical root layout (`D:\devapp\skinnermt2`)
```
CMakeLists.txt  CMakePresets.json  scripts/build-windows.ps1
include/  src/  tools/cli/  tests/  tests/data/
docs/  .opencode/skills/  .github/workflows/
Data/Models/  Data/resources/  noesis/  frontend(ex-legacy, see _archive/)
build/  (generated, git-ignored)
```

## Rules
- KEEP: `CMakeLists.txt`, `include/`, `src/`, `tools/cli/`,
  `tests/data/two_bone.smd`, `docs/`, `scripts/build-windows.ps1`.
- Generated/ignored: `build/`, `logs/`, `*.smd` droppings in `Data/Models`,
  `*.m2rig`, temp bridge files (auto-cleaned).
- NEVER run destructive `Remove-Item -Recurse` on source dirs; use `git status`
  before any cleanup. The old `C:\rigapp` layout in earlier versions of this
  file does not exist.

Legacy note: previous version referenced `C:\rigapp` + `RigApp.csproj`;
see `build-system.md` for the real build.
