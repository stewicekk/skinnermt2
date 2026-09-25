#pragma once
// Native glTF 2.0 writer (.glb + .gltf/.bin). Lives in the m2rig_gltf
// adapter library so m2rig_core stays dependency-free. Mirrors the 29a
// reader (ConvertedGltf): indexed TRIANGLES + POSITION/NORMAL/TEXCOORD_0,
// a single skin with JOINTS_0/WEIGHTS_0, Y-up (no axis conversion —
// canonical is Y-up), pbrMetallicRoughness factors. Everything outside
// that scope fails explicitly, never silently.
//
// Container decision (suffix-dispatched, documented here and on the CLI
// `smd2gltf` verb): `.glb` embeds a single BIN chunk; `.gltf` writes JSON
// plus a sidecar `.bin` next to it (same directory, output stem +
// ".bin", e.g. `model.gltf` -> `model.bin`) with a basename-only relative
// URI (no directories, so the reader's relative-URI-only gate accepts our
// own output). Same 29a security caps on both layouts (counts validated
// BEFORE allocation, 64 MB JSON / 512 MB BIN). No bool flag: the output
// suffix is the option, so both existing writeGltfFile overloads stay
// source-compatible (`.glb` bytes are bit-identical to the 29b layout).
// writeGltfSeparate below is the explicit entry point for the sidecar
// layout (requires a `.gltf` path, fails otherwise); writeGltfFile with a
// `.gltf` path delegates to the same code path.
//
// Animation sampler emission: the 7-argument overload below emits one
// glTF `animation` (per-joint LINEAR translation+rotation samplers) from
// SmdFrames such as bakeClipFrames() output. Frame convention (mirrors
// the reader): input times are SmdFrame.time / 30.0 seconds; euler
// radians go out as Quat::fromEulerXyz (normalized). The 6-argument
// overload is unchanged (static bind pose, no samplers).
#include <string>
#include <vector>

#include "m2rig/mesh.hpp"
#include "m2rig/renderer.hpp"  // PbrMaterial factors only (no GPU use here)
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"  // SmdFrame (animation clip source for the overload below)

namespace m2rig {

// Writes a `.glb` (single BIN chunk) or `.gltf` + sidecar `.bin` file
// from canonical data, dispatched by the output suffix (see the container
// decision above). `bindInverse`
// is caller-provided (row-major canonical Mat4, joint order — the App
// captures `inverseBindTransform` at load; the CLI rebuilds it the same
// way). `pbrMaterials` is either empty (neutral defaults) or parallel to
// `mesh.materials`. Returns a conversion note on success (Y-up record +
// omit/defer lines) for the CLI emit+note pattern.
Result<std::string> writeGltfFile(const std::string& path, const Mesh& mesh,
                                  const Skeleton& skeleton,
                                  const std::vector<Mat4>& bindInverse,
                                  const std::vector<PbrMaterial>& pbrMaterials,
                                  const std::string& assetName = {});

// Animation overload: same static emit plus one glTF `animation` built from
// `animFrames` (e.g. bakeClipFrames() output or an SMD clip loaded for
// `smd2gltf --anim`). Every frame must pose every joint (poses.size() ==
// bone count, boneId = dense joint index, full 0..N coverage) with strictly
// increasing non-negative SmdFrame.time; violations fail explicitly
// (incompatible bone counts are never silently remapped).
Result<std::string> writeGltfFile(const std::string& path, const Mesh& mesh,
                                  const Skeleton& skeleton,
                                  const std::vector<Mat4>& bindInverse,
                                  const std::vector<PbrMaterial>& pbrMaterials,
                                  const std::vector<SmdFrame>& animFrames,
                                  const std::string& assetName = {});

// Explicit sidecar-layout entry points: require a `.gltf` path (any other
// suffix fails explicitly) and write JSON + `<stem>.bin` exactly as the
// `.gltf` branch of writeGltfFile above. Provided so callers can pin the
// layout without relying on suffix dispatch; the CLI uses writeGltfFile
// suffix dispatch instead (`smd2gltf` decides by output suffix).
Result<std::string> writeGltfSeparate(const std::string& path, const Mesh& mesh,
                                      const Skeleton& skeleton,
                                      const std::vector<Mat4>& bindInverse,
                                      const std::vector<PbrMaterial>& pbrMaterials,
                                      const std::string& assetName = {});
Result<std::string> writeGltfSeparate(const std::string& path, const Mesh& mesh,
                                      const Skeleton& skeleton,
                                      const std::vector<Mat4>& bindInverse,
                                      const std::vector<PbrMaterial>& pbrMaterials,
                                      const std::vector<SmdFrame>& animFrames,
                                      const std::string& assetName = {});

}  // namespace m2rig
