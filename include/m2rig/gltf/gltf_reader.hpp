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
// non-indexed inputs, Draco-compressed inputs, unmapped skin joints, or
// animation channels outside the import scope (non-joint targets,
// morph/weights, scale, non-LINEAR interpolation). A single LINEAR
// translation/rotation animation is resampled to `frames` (see above); an
// empty `animations` array yields no frames (not a failure); more than one
// animation fails explicitly (single-clip SMD timeline).
//
// EXT_meshopt_compression: bufferViews carrying the extension are decoded
// in-memory (heap, inside the m2rig_gltf TU so m2rig_core stays clean)
// when built with M2RIG_WITH_MESHOPT (meshoptimizer v1.2, default ON,
// effective only with M2RIG_WITH_CGLTF); the DECODED byte count is
// capped like file bytes (checked BEFORE allocation, cumulative 512 MB).
// Without the decoder the extension stays NOT_SUPPORTED_YET. Either way
// the accessor path downstream is unchanged (decoded views are repointed
// onto owned buffers before any accessor read). `extensionsRequired`
// listing only EXT_meshopt_compression is accepted when the decoder is
// present (every other required extension still fails explicitly).
//
// Documented non-goals (no code, decisions with reasons):
// - KHR_draco_mesh_compression stays explicit-fail: Draco's reference
//   decoder is a full C++ codec library (entropy + prediction schemes,
//   ~100+ TUs) whose weight contradicts the zero-dep-core posture that
//   keeps m2rig_core standard-C++-only and every adapter single-purpose.
//   Alternative when needed: the Noesis bridge path (already the FBX/GR2
//   fallback) or a future dedicated Draco adapter wave — never a silent
//   skip (import fails loudly, so Draco assets cannot pass as complete).
// - Morph targets stay explicit-fail: Mesh has no blend-shape data model
//   (no per-vertex delta streams, no target-weight timeline, no CPU/GPU
//   blend path in deformVertex/GpuVertex). Supporting morphs would need
//   that model first (targets[] deltas + weights animation + deform
//   wiring + SMD has nowhere to carry them), so primitives with
//   `targets` and `weights`-path animation channels fail loudly instead
//   of importing a frozen base mesh.
Result<ConvertedGltf> readGltfFile(const std::string& path, const std::string& assetName);

}  // namespace m2rig
