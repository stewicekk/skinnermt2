# Skill: Regression Tests (native CTest)

## Description
Automated suite validating the complete pipeline. Reality: dependency-free
`tests/` harness + `m2rig_cli` smoke tests (no C#, no `C:\rigapp`).

## Suites (`ctest --preset windows-release`)
- `m2rig_tests` — 35 checks: math, core (skeleton/samples/repair/profiles/
  mesh/JSON), SMD (parse/round-trip/xref/file-IO/frames/real 90-bone ninja
  fixture), weights (paint/transfer/symmetry/MSM/workspace/extractor/
  profiles/sockets/self-train/deform/grnreader-live).
- `cli-validate`, `cli-info`, `cli-smd2smd`, `cli-smd2msm` — headless CLI
  over `tests/data/two_bone.smd`.

Run: `ctest --preset windows-release --output-on-failure` (also covered by
`.github/workflows/ci-windows.yml` on push/PR, debug + release).

## Real-model spot checks (manual, need tools in repo)
- GR2: `grnreader98.exe <model.gr2> -a` in
  `Data/resources/Convert gr2 to mesh/` (ships `granny2.dll`);
  covered live by `weights.grnreader_converts_real_gr2` (skips honestly
  when tool/sample absent).
- FBX: `noesis/Noesis.exe ?cmode <model.fbx> <out.smd>` (native FBX DLLs).
- GR2 via Noesis does NOT work (GR2 Python plugins need `granny2.dll`,
  absent in `noesis/`) — use grnreader98 or the sibling `.fbx`.
- Container check: `isValidGr2Container` accepts V1 `gr2\0` and real V2
  `29 DE ..` (all 31 `Data/Models` GR2 are `29de6cc0`, zero ASCII Bip01).

## Legacy note
This file previously described `C:\rigapp\RigApp.Tests` (`dotnet test`,
182 models, native GR2 emit). None of that exists here; native GR2 emit is
explicitly NOT supported (`Gr2ExportStatus::NotSupportedDirectly`).
