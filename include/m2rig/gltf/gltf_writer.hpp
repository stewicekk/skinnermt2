#pragma once
// Native glTF 2.0 writer (.glb only, Wave 29b). Lives in the m2rig_gltf
// adapter library so m2rig_core stays dependency-free. Mirrors the 29a
// reader (ConvertedGltf): indexed TRIANGLES + POSITION/NORMAL/TEXCOORD_0,
// a single skin with JOINTS_0/WEIGHTS_0, Y-up (no axis conversion —
// canonical is Y-up), pbrMetallicRoughness factors. Everything outside
// that scope fails explicitly, never silently.
//
// Deferred with reasons (see Wave 29b notes):
// - `.gltf` + external `.bin` layout: single-file `.glb` (one BIN chunk)
//   is the only emitted container; `.gltf` paths fail NOT_SUPPORTED_YET.
// - Animation sampler emission: the clip lives in the App session and the
//   bake path produces SmdFrames; the follow-up is a
//   smd2gltf-with-baked-clip verb emitting glTF samplers from
//   bakeClipFrames() output (no new quat work needed).
#include <string>
#include <vector>

#include "m2rig/mesh.hpp"
#include "m2rig/renderer.hpp"  // PbrMaterial factors only (no GPU use here)
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"

namespace m2rig {

// Writes a single-BIN-chunk `.glb` file from canonical data. `bindInverse`
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

}  // namespace m2rig
