# Skill: Rigged Collection Export

Export finished, validated assets (native pipeline).

## GUI
Project menu or Export panel: Export SMD... / Export MSM... /
Export GR2 (bridge)... / Export all loaded (SMD+MSM)... — all respect the
validation gate (`report.exportBlocked()`); GR2 goes through the external
bridge and reports honestly (`NOT_SUPPORTED_DIRECTLY` when unconfigured).

## CLI (scriptable)
```powershell
.\build\release\Release\m2rig_cli.exe smd2smd <in.smd> <out.smd>
.\build\release\Release\m2rig_cli.exe smd2msm <in.smd> <out.msm>
```

## Rules
- Repair (`≤4 influences`, normalize) runs before every export; dropped
  mass is reported, never silent.
- Socket deform use warns; locked bones are preserved.

Legacy note: `RigApp.exe`, `production_pipeline.py`, `*_rigged.gr2`
outputs and the 182-model/239MB figures do not exist here.
