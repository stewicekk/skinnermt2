# Skill: Autosave + Crash Recovery

Native workspace safety (`src/app_state.cpp` `tickAutosave`, `src/app/main.cpp` lock, `src/workspace/project_file.cpp`).

## Contract
- `tickAutosave(dir, intervalSec)`: dirty-gated, `.tmp` + atomic rename, `create_directories` tolerant; `minutes==0` disables.
- `saveAutosaveNow` manual path; `lastAutosaveInfo` readout in Project panel + interval slider + Save-now.
- `session.lock` PID flag (`GetCurrentProcessId`); clean exit removes it; `exists(lock) && exists(autosave)` shows restore/discard modal.
- Scope is honest: settings + asset refs + locks persist; SMD sources reimport where files still exist (mesh weights are session data, `.m2rig` stores metadata).
- Tests: `autosave_tick_writes_when_dirty` (happy path); stale-PID and tmp-vs-rename crash cases are manual QA (see run-regression).

## Entry points
- `App::tickAutosave/saveAutosaveNow/loadWorkspaceFile`, `drawProjectPanel`, recovery modal in `drawAllPanels`.
