# Skill: MSM Inspector

Native MSM AST + inspector (`src/ast/msm_ast.cpp`, `src/app/panels.cpp` `drawMsmInspector`, `tools/cli/main.cpp` `validate-msm`).

## Contract
- Parser: `Group <Name>` syntax, `//` + `/* */` comments, reordered fields, unknown blocks; nesting by indent (never trim before measuring).
- `msmStringify` + shared `buildMsmExport` (GUI and CLI emit identically).
- Semantic gate `validateMsmDoc`: `MSM_SHAPE_COUNT/REF/VERTEX_COUNT`, `MSM_NO_INDEX/SKIN` into report; GUI Inspector shows Group tree + report; CLI `validate-msm` runs the same gate (exit 3 when blocked).
- Tests: `msm_roundtrip_preserves_structure`, `msm_validation_catches_mismatch`, `msm_inspector_open_close`, CLI `cli-validate-msm`.

## Entry points
- `parseMsm/msmStringify/buildMsmExport/validateMsmDoc/findChildByName`, `App::openMsmInspector/closeMsmInspector`.
