# Skill: RigAppPaths Update (tool probing)

Centralized external-tool discovery (no hardcoded absolute paths).

## Probing order (implemented)
- Noesis: `noesis/Noesis.exe` -> `noesis/Noesis64.exe` -> legacy
  `external/noesis/Noesis.exe` (+ ancestor dirs for dev runs from
  `build/`). See `defaultGr2BridgeConfig` (`src/adapters/gr2_adapter.cpp`)
  and `UniversalWeightExtractor` (`src/extractors/
  universal_weight_extractor.cpp`).
- grnreader98: `Data/resources/Convert gr2 to mesh/
  grnreader98.v1.4.0.3.debug (1)/grnreader98.exe` (+ ancestors).
  See `findGrnReader`.
- Model data: `Data/Models/` next to the executable (System panel counts
  GR2/FBX/DDS; missing dir is tolerated).

## Rules
- Never hardcode `C:\` tool paths; never invent `skeletons/*.fbx`
  libraries. Missing tools produce explicit bridge errors, never silent
  fallbacks.

Legacy note: C# `RigAppPaths` / `Metin2Core.cs` do not exist here.
