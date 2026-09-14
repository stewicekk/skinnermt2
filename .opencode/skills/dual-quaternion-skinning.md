# Skill: Dual-Quaternion Skinning (proposal)

STATUS: not implemented. Linear blend skinning is current
(`buildSkinningPalette` + `deformVertex`); DQS lives in roadmap Waves 11-15
(`docs/MASTER_PROMPT.md`, `docs/AGENT_STATE.md`).

## Proposal (if implemented)
1. Core API in `m2rig_core`: dual-quat palette build
   (`buildDQPalette(skeleton, bindInverse)`), `deformVertexDQ` with
   antipodality fix (dot with first non-zero weight), normalize blend.
2. Unit tests: rigid-motion exactness, candy-wrapper comparison vs LBS on
   a 90-degree elbow fixture, normalization edge cases.
3. Viewport toggle LBS/DQS in the Toolbar; GPU path only after CPU parity.

## Rules
- No new third-party deps; header math in `m2rig/math.hpp` style.
- Keep `/W4 /WX` clean; benchmark 5k-vert deform before/after.

Legacy note: C# `DualQuaternionSkinning` and the corrupted tail of the
previous version of this file are removed.
