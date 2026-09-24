---
name: gltf-animation
description: glTF sampler emission from baked clips + linear TRS import, --anim contract, msm2smd shell-only track (Wave 31)
---

# gltf-animation

Animation slice of the glTF interchange: sampler emission from baked
`SmdFrames`, linear TRS import, `--anim` contract, `msm2smd`
shell-only verb. ALL LANDED (Round C). Extends `gltf-pipeline` +
`quaternion-anim`.

## Current truth

- Writer emits bind TRS + OPTIONAL one animation (7-arg overload;
  6-arg delegates with empty clip, static emit byte-identical):
  per-joint LINEAR translation+rotation samplers from `SmdFrame`
  lists, input `time/30.0`, rotation via `Quat::fromEulerXyz`
  normalized; full joint coverage + boneId range + strictly-
  increasing times validated, bone-count mismatch fails.
- CLI: `smd2gltf <in.smd> <out.glb> [--anim <anim.smd>]`
  (exit 2 on mismatch/unusable clip).
- Reader imports linear TRS channels on skin joints into
  `ConvertedGltf::frames` (fps=30: `frame=round(time*30)`;
  quats via the TRS-joint path); morph/scale/STEP/CUBIC/non-joint
  targets fail per-channel, explicitly; empty array = no frames.
- `msm2smd <in.msm> <bind.smd> <out.smd>` LANDED as the documented
  intermediate (no export gate — shell has no geometry by honesty;
  validation report informational + loud shell-only note, exit 0;
  `cli-msm2smd` suite over sample.msm + two_bone.smd).
- fps=30 convention documented on both members + both note strings.

## Target contract (open)

- `.gltf` + external `.bin` layout (single-BIN `.glb` covers the
  tested path); draco/meshopt decode decision; morph targets;
  GUI glTF import (`App::importGltfFile` first — panels-only would
  fork the FBX path).

## Entry points

- `include/m2rig/gltf/gltf_writer.hpp` (`writeGltfFile`),
  `src/gltf/gltf_writer.cpp` (TRS emit `:521-523`, note `:664-671`),
  `include/m2rig/gltf/gltf_reader.hpp` (`readGltfFile`,
  `ConvertedGltf`), `src/gltf/gltf_reader.cpp` (`jointLocal`,
  animation gate `:541-546`), `include/m2rig/anim.hpp`
  (`AnimKey/AnimClip/bakeClipFrames/exportAni`, `fps`),
  `src/anim.cpp` (bake + `sampleBone`), `include/m2rig/smd.hpp`
  (`SmdFrame/SmdModel`), `include/m2rig/ast/msm_ast.hpp`
  (`msmToSmd`, `validateMsmDoc`), `tools/cli/main.cpp`
  (`cmdSmd2Gltf`, `cmdGltf2Smd`, `cmdOrient`, dispatch).

## Test gate

- Real (green): `smd2gltf_roundtrip_sample_armor`,
  `smd2gltf_rejects_overlimit_explicit`,
  `smd2gltf_gltf_layout_deferred`, `imports_single_rotation_channel_
  to_frames`, `rejects_scale_channel_explicit`,
  `rejects_step_interpolation_explicit`,
  `smd2gltf_emits_animation_samplers`,
  `smd2gltf_rejects_mismatched_anim_bones`,
  `msm2smd_shell_chain_fixture` + `smd.msm_to_smd_*` (3, shell).

## Failure handling

- Sampler math must round-trip through the reader within tolerance
  (writer composer / reader extractor are exact inverses — keep
  them paired). Non-linear channels fail explicitly, never baked
  as linear. A gated `msm2smd` verb must never exit 0 with
  invented geometry: empty triangles + shell warning or a
  non-zero exit. Frozen: Y-up canonical, `M^-1RM`, `local*parent`.
