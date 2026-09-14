# Skill: Metin2 Core + Noesis Bridge (C++)

Native bridge layer — no C#, no AssimpNet.

## Components
- `include/m2rig/adapters/gr2_adapter.hpp` — `Gr2BridgeConfig`,
  `exportGr2ViaBridge`, `extractWeightsFromGr2ViaBridge`,
  `isValidGr2Container`, `findGrnReader`,
  `convertGr2ToSmdViaGrnReader` (temp staging, non-destructive).
- `include/m2rig/extractors/universal_weight_extractor.hpp` —
  `SmdWeightExtractor` (native) + `NoesisBridgeExtractor` (FBX/GR2).
- `App::importBridgedFile` (`src/app_state.cpp`): `.smd` direct;
  `.gr2` via grnreader98 first, Noesis fallback; `.fbx` via Noesis.
- Shared subprocess runner with log capture:
  `include/m2rig/adapters/bridge_process.hpp` (output lands in Console).

## Command reference
- `grnreader98.exe "<model.gr2>" -a` (batch, all submeshes + weights)
- `noesis/Noesis.exe ?cmode "<in.fbx>" "<out.smd>"`
- Probing order: `noesis/Noesis.exe`, `noesis/Noesis64.exe`, legacy
  `external/noesis/` (+ ancestor dirs for dev runs from `build/`).

Legacy note: C# `Metin2Core.cs` / `FbxParser` / `NoesisIntegration` do not
exist here.
