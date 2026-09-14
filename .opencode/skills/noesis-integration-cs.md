# Skill: Noesis Integration (CLI syntax)

## Correct command (verified live)
```
noesis/Noesis.exe ?cmode "<input.fbx>" "<output.smd>"
```
- `?cmode` = headless console mode (bare two-arg form falls back to GUI).
- FBX works (native `autodesk_fbx*.dll` plugins present; verified:
  `ninja.fbx` -> 90 bones / 2070 verts).
- GR2 via Noesis does NOT work here: GR2 support is Python plugins needing
  `granny2.dll`, absent from `noesis/`. Use grnreader98 for GR2.

## In code
- Probing + timeout + exit-code handling: `src/adapters/gr2_adapter.cpp`
  (`defaultGr2BridgeConfig`, 30s timeout), `src/extractors/
  universal_weight_extractor.cpp` (60s).
- Output capture: `adapters/bridge_process` (Console panel shows logs).

Legacy note: `-quickconvert` and `C:\rigapp\models` paths from the previous
version do not exist; use `?cmode` above.
