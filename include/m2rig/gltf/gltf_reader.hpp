#pragma once
// Native glTF 2.0 reader built on cgltf (Wave 29a). Lives in the m2rig_gltf
// adapter library so m2rig_core stays dependency-free. Strategy mirrors the
// SMD/FBX importers: indexed triangles -> canonical Mesh/Skeleton with
// repaired <=4 influences. Minimal scope (29a): indexed TRIANGLES only,
// POSITION (+optional NORMAL/TEXCOORD_0), a single skin with JOINTS_0/
// WEIGHTS_0, Y-up (no axis arbitration — canonical is Y-up), PBR factor
// passthrough. Everything else fails explicitly, never silently.
#include <cstddef>
#include <string>
#include <vector>

#include "m2rig/mesh.hpp"
#include "m2rig/renderer.hpp"  // PbrMaterial passthrough only (no GPU use here)
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"  // SmdFrame timeline (animation import below)

namespace m2rig {

struct ConvertedGltf {
    Mesh mesh;
    Skeleton skeleton;
    std::vector<Mat4> bindInverse;        // joint order, from inverseBindMatrices
    std::vector<PbrMaterial> pbrMaterials;  // parallel to mesh.materials
    // Animation timeline (empty when the file has no `animations` array):
    // one SmdFrame per resampled keyframe time, all bones posed (bind pose
    // for bones/channels without a key at that frame), boneId = dense joint
    // index, time = mapped integer frame. Frame convention (documented in
    // the writer too): glTF keyframe times are seconds on a 30 fps timeline,
    // so frame = round(time * 30); input times on emit are frame / 30.0.
    std::vector<SmdFrame> frames;
    double removedMass = 0.0;             // weight mass dropped by the <=4 repair
    std::size_t repairedVertices = 0;     // verts changed by the <=4 repair
    std::size_t meshCount = 0;            // source glTF mesh objects merged
    std::string conversionNote;           // Y-up record + warn-only orient gate
};

// Reads a .gltf (+ sidecar .bin) or .glb file into canonical form. Fails
// explicitly (never partial) on IO errors, security-cap violations,
// non-indexed/compressed inputs, unmapped skin joints, or animation
// channels outside the import scope (non-joint targets, morph/weights,
// scale, non-LINEAR interpolation). A single LINEAR translation/rotation
// animation is resampled to `frames` (see above); an empty `animations`
// array yields no frames (not a failure); more than one animation fails
// explicitly (single-clip SMD timeline).
Result<ConvertedGltf> readGltfFile(const std::string& path, const std::string& assetName);

}  // namespace m2rig
