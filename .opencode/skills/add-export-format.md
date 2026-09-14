# Skill: Add Export Format

Add a new mesh export format to the native pipeline.

## Where formats live
- SMD writer: `src/smd.cpp` (`writeSmd`, `SmdWriteResult`) + `include/m2rig/smd.hpp`
- MSM writer: `src/ast/msm_ast.cpp` (`buildMsmExport`) + `include/m2rig/ast/msm_ast.hpp`
- GUI wiring: `App::exportSmdFile` / `App::exportMsmFile` in `src/app_state.cpp`
  (repair -> validation gate -> write; never bypass the gate)
- CLI wiring: `tools/cli/main.cpp` (`smd2smd` / `smd2msm` commands)

## Rules for a new format
1. Clamp to `kMetin2MaxInfluences` (4) via `repairMeshWeights`; report
   `RepairStats::removedMass`, never truncate silently.
2. Respect `ValidationReport::exportBlocked()` before writing a byte.
3. Add a CLI subcommand + ctest smoke test over `tests/data/two_bone.smd`.
4. GR2 native emit is explicitly NOT supported
   (`Gr2ExportStatus::NotSupportedDirectly`); route through
   `grnreader98.exe <gr2> -a` / `noesis/Noesis.exe ?cmode`.

Legacy note: this file previously described `C:\rigapp` C# batch export;
that project does not exist here.
