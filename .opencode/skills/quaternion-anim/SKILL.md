---
name: quaternion-anim
description: Quaternion animation sampling, compression and .ani verdict (Wave 28)
---

# quaternion-anim

Replaces euler-lerp sampling with quaternion curves while keeping the
euler UI/gizmo compatible. Ends with a verdict on the write-only `.ani`.

## Current truth

- Contract is explicitly not-slerp (`anim.hpp:1-8`); `AnimKey` full
  pos+euler snapshots (`anim.hpp:17-25`); `sampleClip` component-lerp +
  `wrapDelta` (`anim.cpp:143-170`); `bakeClipFrames` duplicates it
  (`:172-218`); linear segment search; clamp, no loop modes; clip
  in-session only (`app.hpp:55-56`); bake replaces frames outside
  pose-undo (`app.hpp:308-310`).
- `Quat::fromEulerXyz/toMatrix/slerp` exist, never called from `anim.cpp`
  (`math.hpp:269-312`).
- `.ani`: `"ANI " v1`, pos/rot-deg/scale float32 (`anim.hpp:27-50`,
  `anim.cpp:35-102`); export-only, 0x import/parse/read; no CLI verb;
  `USER_GUIDE.md:205` honestly "no readers yet".
- 0x compression anywhere (only DDS block-compression).

## Target contract

- Parallel quat track: `fromEulerXyz` on capture, `slerp` (later squad)
  on sample, euler only at the bake/export boundary (`anim.cpp` 1-file
  change + `AnimKey` quat track).
- Hermite/cubic toggle; loop/clamp/ping-pong wrap modes; binary-search
  segments; per-track key-reduction + 16-bit/smallest-three quantization;
  raw `bakeClipFrames` kept as lossless path.
- `.ani` prove-or-retire: `ani2smd` + round-trip + game-load proof, or
  delete exporter and standardize on SMD frames + glTF samplers.

## Entry points

- `capturePoseKey/addKey/removeKeyAt/clipLastFrame/sampleClip/bakeClipFrames`,
  `exportAni/writeAniFile`, `App::exportAniFile`, `Quat::*`.

## Test gate

- Existing euler tests stay green + `slerp_midpoint`, `wrap_pi_parity`,
  `bake_matches_preview`, `ani_roundtrip` (or exporter deleted).

## Failure handling

Baked output must match preview sampling exactly (same math both paths).
Keep `USER_GUIDE.md:205` honest until the round-trip exists. Frozen:
`local*parent` rebuild inside `sampleClip` stays (pose->globals->binds).
