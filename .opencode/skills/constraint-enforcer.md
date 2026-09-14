# Skill: Constraint Enforcer (Metin2 limits)

Enforce engine limits at the validation gate (same rules in GUI + CLI).

## Limits
- Max 4 influences per vertex (`kMetin2MaxInfluences`); over-limit is an
  Error (`WEIGHTS_OVER_LIMIT`), reduced strongest-first with
  `RepairStats::removedMass` reported.
- Weights sum to 1.0 (warning past 1e-2, `WEIGHTS_UNNORMALIZED`).
- No NaN/negative/zero weights (`WEIGHTS_INVALID` = error); no unweighted
  vertices (`WEIGHTS_UNWEIGHTED` = error); bone ids in range
  (`WEIGHTS_BAD_BONE` = error).
- 23-bone Bip01 core expected per `--profile`
  (`pc_{warrior,assassin,sura,shaman}_{m,f}`, `pc_wolfman`, `pc_mount`);
  verified optionals (Spine2, fingers, toes, ponytail, armor sockets);
  socket deform use warns (`SOCKET_DEFORM_USE`).

## Entry points
- GUI: `App::runValidation` (export gate blocks on errors).
- CLI: `m2rig_cli validate <model.smd> --profile <id>` (exit 3 when blocked).
- Code: `include/m2rig/skin_weights.hpp`, `src/profiles.cpp`.

Legacy note: C# `Metin2Validator`/`Metin2AutoFixer` do not exist here.
