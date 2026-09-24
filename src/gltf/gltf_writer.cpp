// Native glTF 2.0 writer: canonical Mesh/Skeleton -> single-BIN `.glb`.
// Design notes:
// - No third-party emit dependency: JSON is built by hand (escaped) and the
//   BIN chunk is packed with explicit little-endian helpers, so the writer
//   never touches cgltf (parse-only) and keeps the reader's 29a security
//   caps on the emit path (counts validated BEFORE allocation, URIs written
//   basename-only).
// - glTF is Y-up by spec and canonical is Y-up: positions/normals/uvs are
//   asserted finite and copied verbatim (no axis arbitration, unlike FBX).
//   Mesh-instance transforms do not exist on emit (canonical verts are
//   already in bind space); the mesh node is identity.
// - Row-vector canonical Mat4 (m[row][col]) -> glTF float[16] is the exact
//   inverse of the reader's mat4FromColumnMajor (R[rr][c] = S[rr*4+c]):
//   stored verbatim row-major (out[rr*4+c] = m[rr][c]), so
//   inverseBindMatrices round-trip.
// - Joint locals (pos/euler/scale) go out as TRS with the rotation as a
//   normalized quaternion from Quat::fromEulerXyz; the reader recovers
//   eulers via Quat->matrix->eulerXyzFromRotation (exact inverse composer).
// - Animation (7-argument overload only): one glTF `animation` with
//   per-joint LINEAR translation+rotation samplers from SmdFrames. Frame
//   convention (mirrors the reader): input times are SmdFrame.time / 30.0
//   seconds; euler radians convert via Quat::fromEulerXyz (normalized).
#include "m2rig/gltf/gltf_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "m2rig/diagnostics.hpp"
#include "m2rig/skin_weights.hpp"

namespace m2rig {

namespace {

constexpr std::uint64_t kMaxGltfJsonBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxBinBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kGlbMagic = 0x46546C67u;      // 'glTF' little-endian
constexpr std::uint32_t kGlbVersion2 = 2u;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534Au;  // 'JSON'
constexpr std::uint32_t kGlbBinChunk = 0x004E4942u;   // 'BIN\0'
constexpr const char* kOp = "gltf.write";

bool checkedAddU64(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    out = a + b;
    return out >= a;
}

bool checkedMulU64(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    out = a * b;
    return out / a == b;
}

std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20u) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(ch));
                    out += buf;
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

std::string fmtF(float v) {
    std::ostringstream os;
    os << std::setprecision(9) << v;
    return os.str();
}

void pushU8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void pushU32le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

void pushF32le(std::vector<std::uint8_t>& out, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    pushU32le(out, u);
}

void padTo4(std::vector<std::uint8_t>& out) {
    while (out.size() % 4 != 0) out.push_back(0);
}

// Basename-only URI gate for emit (mirrors the reader's relative-URI-only
// allowlist from the other direction): takes the filename, rejects empty
// / traversal / scheme / encoded leftovers, and only allows PNG/JPEG image
// extensions. Returns false when the texture must be omitted (DDS game
// textures have no glTF image bytes to point at — material factors still
// emit, so nothing is silently lost).
bool basenameTextureUri(const std::string& raw, std::string& outBase) {
    if (raw.empty()) return false;
    const std::string base = std::filesystem::path(raw).filename().string();
    if (base.empty() || base == "." || base == "..") return false;
    if (base.find('/') != std::string::npos || base.find('\\') != std::string::npos ||
        base.find(':') != std::string::npos || base.find('%') != std::string::npos ||
        base.find("..") != std::string::npos)
        return false;
    const std::string low = lowerOf(base);
    if (!endsWith(low, ".png") && !endsWith(low, ".jpg") && !endsWith(low, ".jpeg"))
        return false;
    outBase = base;
    return true;
}

}  // namespace

Result<std::string> writeGltfFile(const std::string& path, const Mesh& mesh,
                                  const Skeleton& skeleton,
                                  const std::vector<Mat4>& bindInverse,
                                  const std::vector<PbrMaterial>& pbrMaterials,
                                  const std::string& assetName) {
    // Static bind pose: same emit, no animation samplers.
    const std::vector<SmdFrame> noAnim;
    return writeGltfFile(path, mesh, skeleton, bindInverse, pbrMaterials, noAnim, assetName);
}

Result<std::string> writeGltfFile(const std::string& path, const Mesh& mesh,
                                  const Skeleton& skeleton,
                                  const std::vector<Mat4>& bindInverse,
                                  const std::vector<PbrMaterial>& pbrMaterials,
                                  const std::vector<SmdFrame>& animFrames,
                                  const std::string& assetName) {
    const std::string asset = assetName.empty() ? path : assetName;
    const std::string ext = lowerOf([&] {
        const auto pos = path.find_last_of('.');
        return (pos == std::string::npos) ? std::string() : path.substr(pos);
    }());
    if (ext == ".gltf")
        return Result<std::string>::fail(
            "Refusing '.gltf' output (NOT_SUPPORTED_YET): the external-.bin layout is "
            "deferred; emit '.glb' (single BIN chunk) instead.",
            "FORMAT", asset, kOp);
    if (ext != ".glb")
        return Result<std::string>::fail(
            "Unknown glTF extension '" + ext + "' (expected .glb).", "FORMAT", asset, kOp);

    const SafetyLimits& lim = defaultLimits();
    const std::size_t vertCount = mesh.vertices.size();
    const std::size_t indexCount = mesh.indices.size();
    const std::size_t jointCount = skeleton.bones.size();

    // --- structural validation (before any allocation) ----------------------
    if (vertCount == 0)
        return Result<std::string>::fail("Mesh has no vertices.", "EXPORT", asset, kOp);
    if (indexCount == 0 || indexCount % 3 != 0)
        return Result<std::string>::fail("Mesh has no usable triangles.", "EXPORT", asset,
                                         kOp);
    if (jointCount == 0)
        return Result<std::string>::fail("Skeleton has no bones.", "EXPORT", asset, kOp);
    {
        std::string err;
        if (!checkCount("vertices", vertCount, lim.maxVertices, err))
            return Result<std::string>::fail(err, "EXPORT", asset, kOp);
        if (!checkCount("indices", indexCount, lim.maxIndices, err))
            return Result<std::string>::fail(err, "EXPORT", asset, kOp);
        if (!checkCount("triangles", indexCount / 3, lim.maxTriangles, err))
            return Result<std::string>::fail(err, "EXPORT", asset, kOp);
        if (!checkCount("bones", jointCount, lim.maxBones, err))
            return Result<std::string>::fail(err, "EXPORT", asset, kOp);
        if (!checkCount("materials", mesh.materials.size(), lim.maxMaterials, err))
            return Result<std::string>::fail(err, "EXPORT", asset, kOp);
    }
    if (vertCount > 0xFFFFFFFFULL)
        return Result<std::string>::fail("Mesh exceeds 4G vertices.", "EXPORT", asset, kOp);
    if (jointCount > 65536)
        return Result<std::string>::fail("Skeleton exceeds 65536 joints (JOINTS_0 limit).",
                                         "EXPORT", asset, kOp);
    if (bindInverse.size() != jointCount)
        return Result<std::string>::fail(
            "bindInverse has " + std::to_string(bindInverse.size()) + " entries for " +
                std::to_string(jointCount) + " joints (caller must pass one per joint).",
            "EXPORT", asset, kOp);
    if (!pbrMaterials.empty() && pbrMaterials.size() != mesh.materials.size())
        return Result<std::string>::fail(
            "pbrMaterials has " + std::to_string(pbrMaterials.size()) + " entries for " +
                std::to_string(mesh.materials.size()) +
                " materials (pass empty for defaults or one per material).",
            "EXPORT", asset, kOp);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const Bone& b = skeleton.bones[i];
        if (b.name.empty())
            return Result<std::string>::fail("Bone " + std::to_string(i) + " has no name.",
                                             "EXPORT", asset, kOp);
        if (!std::isfinite(b.localPosition.x) || !std::isfinite(b.localPosition.y) ||
            !std::isfinite(b.localPosition.z) || !std::isfinite(b.localRotationEuler.x) ||
            !std::isfinite(b.localRotationEuler.y) || !std::isfinite(b.localRotationEuler.z) ||
            !std::isfinite(b.localScale.x) || !std::isfinite(b.localScale.y) ||
            !std::isfinite(b.localScale.z))
            return Result<std::string>::fail("Bone '" + b.name + "' has non-finite TRS.",
                                             "EXPORT", asset, kOp);
        if (b.parentId != kNoParent &&
            (b.parentId < 0 || static_cast<std::size_t>(b.parentId) >= jointCount))
            return Result<std::string>::fail("Bone '" + b.name + "' has a bad parent id.",
                                             "EXPORT", asset, kOp);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (!std::isfinite(bindInverse[i].m[r][c]))
                    return Result<std::string>::fail(
                        "bindInverse for joint '" + b.name + "' is non-finite.", "EXPORT",
                        asset, kOp);
    }
    for (const std::uint32_t ix : mesh.indices)
        if (static_cast<std::uint64_t>(ix) >= vertCount)
            return Result<std::string>::fail("Mesh has out-of-range indices.", "EXPORT",
                                             asset, kOp);
    for (const auto& sm : mesh.subMeshes) {
        std::uint64_t end = 0;
        if (!checkedAddU64(sm.startIndex, sm.indexCount, end) || end > indexCount)
            return Result<std::string>::fail("Submesh '" + sm.name + "' is out of range.",
                                             "EXPORT", asset, kOp);
        if (!mesh.materials.empty() && sm.materialIndex >= mesh.materials.size())
            return Result<std::string>::fail("Submesh '" + sm.name + "' has a bad material.",
                                             "EXPORT", asset, kOp);
    }
    // Y-up assert: canonical is Y-up, so geometry is copied verbatim after a
    // finiteness gate (no axis conversion, unlike FBX).
    for (std::size_t vi = 0; vi < vertCount; ++vi) {
        const Vertex& v = mesh.vertices[vi];
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) ||
            !std::isfinite(v.position.z) || !std::isfinite(v.normal.x) ||
            !std::isfinite(v.normal.y) || !std::isfinite(v.normal.z) ||
            !std::isfinite(v.uv0.x) || !std::isfinite(v.uv0.y))
            return Result<std::string>::fail(
                "Vertex " + std::to_string(vi) + " is non-finite (Y-up assert).",
                "EXPORT", asset, kOp);
        // <=4 export gate: fail explicitly, never silently truncate (the SMD
        // writer truncates + reports; glTF JOINTS_0/WEIGHTS_0 are fixed VEC4,
        // so over-limit input is a hard error here).
        if (v.influences.size() > kMetin2MaxInfluences)
            return Result<std::string>::fail(
                "Vertex " + std::to_string(vi) + " has " +
                    std::to_string(v.influences.size()) +
                    " influences (limit 4 for JOINTS_0/WEIGHTS_0; run repair first).",
                "EXPORT", asset, kOp);
        if (v.influences.empty())
            return Result<std::string>::fail(
                "Vertex " + std::to_string(vi) + " has no usable skin weights.",
                "EXPORT", asset, kOp);
        bool anyPositive = false;
        for (const auto& inf : v.influences) {
            if (inf.bone >= jointCount)
                return Result<std::string>::fail(
                    "Vertex " + std::to_string(vi) + " references joint " +
                        std::to_string(inf.bone) + " (>= " + std::to_string(jointCount) +
                        " joints).",
                    "EXPORT", asset, kOp);
            if (!std::isfinite(inf.weight) || inf.weight < 0.0f)
                return Result<std::string>::fail(
                    "Vertex " + std::to_string(vi) + " has invalid weight.", "EXPORT",
                    asset, kOp);
            if (inf.weight > 0.0f) anyPositive = true;
        }
        if (!anyPositive)
            return Result<std::string>::fail(
                "Vertex " + std::to_string(vi) + " has no usable skin weights.",
                "EXPORT", asset, kOp);
    }
    for (std::size_t mi = 0; mi < pbrMaterials.size(); ++mi) {
        const PbrMaterial& pm = pbrMaterials[mi];
        for (int k = 0; k < 4; ++k)
            if (!std::isfinite(pm.baseColor[k]))
                return Result<std::string>::fail(
                    "PBR material '" + pm.name + "' has non-finite baseColor.", "EXPORT",
                    asset, kOp);
        if (!std::isfinite(pm.metallic) || !std::isfinite(pm.roughness))
            return Result<std::string>::fail(
                "PBR material '" + pm.name + "' has non-finite metallic/roughness.",
                "EXPORT", asset, kOp);
    }

    // --- animation clip validation (before any allocation) -----------------
    // Every frame must pose every joint exactly once (boneId = dense joint
    // index); SmdFrame.time values become sampler input times (time/30.0),
    // so they must be non-negative and strictly increasing.
    const bool hasAnim = !animFrames.empty();
    const std::size_t animCount = animFrames.size();
    // Per-frame, per-joint pose snapshots in joint order (validated below).
    std::vector<std::vector<Vec3>> animPos;
    std::vector<std::vector<Vec3>> animRot;
    std::vector<float> animTimes;
    if (hasAnim) {
        if (animCount > 0xFFFFFFFFULL)
            return Result<std::string>::fail("Animation exceeds 4G frames.", "EXPORT",
                                             asset, kOp);
        animPos.assign(animCount, std::vector<Vec3>(jointCount));
        animRot.assign(animCount, std::vector<Vec3>(jointCount));
        animTimes.reserve(animCount);
        int prevTime = -1;
        for (std::size_t fi = 0; fi < animCount; ++fi) {
            const SmdFrame& f = animFrames[fi];
            const std::string fWhat = "animation frame " + std::to_string(fi) +
                                      " (time " + std::to_string(f.time) + ")";
            if (f.time < 0)
                return Result<std::string>::fail(
                    "Animation " + fWhat + " has a negative time (must be >= 0).",
                    "EXPORT", asset, kOp);
            if (fi > 0 && f.time <= prevTime)
                return Result<std::string>::fail(
                    "Animation frame times must be strictly increasing (frame " +
                        std::to_string(fi) + " time " + std::to_string(f.time) +
                        " <= previous " + std::to_string(prevTime) + ").",
                    "EXPORT", asset, kOp);
            prevTime = f.time;
            if (f.poses.size() != jointCount)
                return Result<std::string>::fail(
                    "Animation " + fWhat + " has " + std::to_string(f.poses.size()) +
                        " poses for " + std::to_string(jointCount) +
                        " joints (incompatible bone counts; frames must pose every joint).",
                    "EXPORT", asset, kOp);
            std::vector<char> seen(jointCount, 0);
            for (const auto& p : f.poses) {
                if (p.boneId >= jointCount)
                    return Result<std::string>::fail(
                        "Animation " + fWhat + " references joint " +
                            std::to_string(p.boneId) + " (>= " +
                            std::to_string(jointCount) + " joints).",
                        "EXPORT", asset, kOp);
                if (seen[p.boneId] != 0)
                    return Result<std::string>::fail(
                        "Animation " + fWhat + " poses joint " +
                            std::to_string(p.boneId) + " twice (one pose per joint).",
                        "EXPORT", asset, kOp);
                seen[p.boneId] = 1;
                if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) ||
                    !std::isfinite(p.position.z) || !std::isfinite(p.rotation.x) ||
                    !std::isfinite(p.rotation.y) || !std::isfinite(p.rotation.z))
                    return Result<std::string>::fail(
                        "Animation " + fWhat + " has a non-finite pose for joint " +
                            std::to_string(p.boneId) + ".",
                        "EXPORT", asset, kOp);
                animPos[fi][p.boneId] = p.position;
                animRot[fi][p.boneId] = p.rotation;
            }
            animTimes.push_back(static_cast<float>(f.time) / 30.0f);
            if (!std::isfinite(animTimes.back()))
                return Result<std::string>::fail(
                    "Animation " + fWhat + " maps to a non-finite input time.", "EXPORT",
                    asset, kOp);
        }
    }

    // --- primitives (one per submesh, else a single full-range primitive) ----
    struct Prim {
        std::uint32_t material = 0;
        std::size_t start = 0;
        std::size_t count = 0;
    };
    std::vector<Prim> prims;
    if (!mesh.subMeshes.empty()) {
        for (const auto& sm : mesh.subMeshes) {
            if (sm.indexCount == 0) continue;
            Prim p;
            p.material = sm.materialIndex;
            p.start = sm.startIndex;
            p.count = sm.indexCount;
            prims.push_back(p);
        }
    }
    if (prims.empty()) {
        Prim p;
        p.material = 0;
        p.start = 0;
        p.count = indexCount;
        prims.push_back(p);
    }
    if (!mesh.materials.empty())
        for (const auto& p : prims)
            if (static_cast<std::uint64_t>(p.material) >= mesh.materials.size())
                return Result<std::string>::fail("Primitive has a bad material index.",
                                                 "EXPORT", asset, kOp);

    // --- BIN size validation BEFORE allocation (29a caps kept) ---------------
    const bool useU8Joints = (jointCount <= 256);
    const bool useU16Index = (vertCount <= 65536);
    const std::uint64_t jointElem = useU8Joints ? 4ULL : 8ULL;
    const std::uint64_t idxElem = useU16Index ? 2ULL : 4ULL;
    std::uint64_t binSize = 0, step = 0, tmp = 0;
    if (!checkedMulU64(vertCount, 12ULL, step) || !checkedAddU64(binSize, step, tmp))
        return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT", asset, kOp);
    binSize = tmp;
    if (!checkedMulU64(vertCount, 12ULL, step) || !checkedAddU64(binSize, step, tmp))
        return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT", asset, kOp);
    binSize = tmp;
    if (!checkedMulU64(vertCount, 8ULL, step) || !checkedAddU64(binSize, step, tmp))
        return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT", asset, kOp);
    binSize = tmp;
    if (!checkedMulU64(vertCount, jointElem, step) || !checkedAddU64(binSize, step, tmp))
        return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT", asset, kOp);
    binSize = tmp;
    if (!checkedMulU64(vertCount, 16ULL, step) || !checkedAddU64(binSize, step, tmp))
        return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT", asset, kOp);
    binSize = tmp;
    {
        std::uint64_t idxBytes = 0;
        if (!checkedMulU64(static_cast<std::uint64_t>(indexCount), idxElem, idxBytes) ||
            !checkedAddU64(binSize, idxBytes, tmp))
            return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT", asset,
                                             kOp);
        binSize = tmp;
        if (useU16Index && (indexCount % 2 != 0)) {
            if (!checkedAddU64(binSize, 2ULL, tmp))
                return Result<std::string>::fail("Mesh exceeds buffer caps.", "EXPORT",
                                                 asset, kOp);
            binSize = tmp;
        }
    }
    if (!checkedMulU64(jointCount, 64ULL, step) || !checkedAddU64(binSize, step, tmp))
        return Result<std::string>::fail("Skeleton exceeds buffer caps.", "EXPORT", asset,
                                         kOp);
    binSize = tmp;
    if (hasAnim) {
        // Input times (1 float/frame) + per joint: translations (VEC3/frame)
        // + rotations (VEC4/frame).
        if (!checkedMulU64(animCount, 4ULL, step) || !checkedAddU64(binSize, step, tmp))
            return Result<std::string>::fail("Animation exceeds buffer caps.", "EXPORT",
                                             asset, kOp);
        binSize = tmp;
        std::uint64_t perJoint = 0;
        if (!checkedMulU64(animCount, 28ULL, perJoint) ||
            !checkedMulU64(perJoint, static_cast<std::uint64_t>(jointCount), step) ||
            !checkedAddU64(binSize, step, tmp))
            return Result<std::string>::fail("Animation exceeds buffer caps.", "EXPORT",
                                             asset, kOp);
        binSize = tmp;
    }
    if (binSize > kMaxBinBytes)
        return Result<std::string>::fail(
            "Emit buffer " + std::to_string(binSize) + " bytes exceeds the 512 MB cap.",
            "EXPORT", asset, kOp);

    // --- BIN pack ------------------------------------------------------------
    std::vector<std::uint8_t> bin;
    bin.reserve(static_cast<std::size_t>(binSize));
    const std::size_t posOff = bin.size();
    for (const auto& v : mesh.vertices) {
        pushF32le(bin, v.position.x);
        pushF32le(bin, v.position.y);
        pushF32le(bin, v.position.z);
    }
    const std::size_t nrmOff = bin.size();
    for (const auto& v : mesh.vertices) {
        pushF32le(bin, v.normal.x);
        pushF32le(bin, v.normal.y);
        pushF32le(bin, v.normal.z);
    }
    const std::size_t uvOff = bin.size();
    for (const auto& v : mesh.vertices) {
        pushF32le(bin, v.uv0.x);
        pushF32le(bin, v.uv0.y);
    }
    const std::size_t jointsOff = bin.size();
    for (const auto& v : mesh.vertices) {
        std::uint32_t jb[4] = {0, 0, 0, 0};
        for (std::size_t k = 0; k < v.influences.size() && k < 4; ++k)
            jb[k] = v.influences[k].bone;
        if (useU8Joints) {
            for (int k = 0; k < 4; ++k) pushU8(bin, static_cast<std::uint8_t>(jb[k]));
        } else {
            for (int k = 0; k < 4; ++k) {
                const std::uint16_t u = static_cast<std::uint16_t>(jb[k]);
                pushU8(bin, static_cast<std::uint8_t>(u & 0xFFu));
                pushU8(bin, static_cast<std::uint8_t>((u >> 8) & 0xFFu));
            }
        }
    }
    const std::size_t weightsOff = bin.size();
    for (const auto& v : mesh.vertices) {
        float wb[4] = {0, 0, 0, 0};
        for (std::size_t k = 0; k < v.influences.size() && k < 4; ++k)
            wb[k] = v.influences[k].weight;
        for (int k = 0; k < 4; ++k) pushF32le(bin, wb[k]);
    }
    const std::size_t idxOff = bin.size();
    for (const std::uint32_t ix : mesh.indices) {
        if (useU16Index) {
            const std::uint16_t u = static_cast<std::uint16_t>(ix);
            pushU8(bin, static_cast<std::uint8_t>(u & 0xFFu));
            pushU8(bin, static_cast<std::uint8_t>((u >> 8) & 0xFFu));
        } else {
            pushU32le(bin, ix);
        }
    }
    padTo4(bin);
    const std::size_t ibmOff = bin.size();
    for (const auto& m : bindInverse)
        // Inverse of the reader's mat4FromColumnMajor (R[rr][c] = S[rr*4+c]):
        // store verbatim row-major so S[rr*4+c] == m[rr][c] round-trips.
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) pushF32le(bin, m.m[r][c]);
    padTo4(bin);
    // --- animation clip pack (times, then per joint T block + R block) ------
    std::size_t animTimeOff = 0;
    std::vector<std::size_t> animTransOff(jointCount, 0);
    std::vector<std::size_t> animRotOff(jointCount, 0);
    if (hasAnim) {
        animTimeOff = bin.size();
        for (const float t : animTimes) pushF32le(bin, t);
        for (std::size_t j = 0; j < jointCount; ++j) {
            animTransOff[j] = bin.size();
            for (std::size_t fi = 0; fi < animCount; ++fi) {
                pushF32le(bin, animPos[fi][j].x);
                pushF32le(bin, animPos[fi][j].y);
                pushF32le(bin, animPos[fi][j].z);
            }
            animRotOff[j] = bin.size();
            for (std::size_t fi = 0; fi < animCount; ++fi) {
                const Quat q = Quat::fromEulerXyz(animRot[fi][j]).normalized();
                pushF32le(bin, q.x);
                pushF32le(bin, q.y);
                pushF32le(bin, q.z);
                pushF32le(bin, q.w);
            }
        }
        padTo4(bin);
    }

    // --- materials / textures (basename-only URIs) ---------------------------
    const std::size_t matCount = mesh.materials.empty() ? 1 : mesh.materials.size();
    std::vector<std::string> matNames(matCount);
    std::vector<float> matBase(matCount * 4, 1.0f);
    std::vector<float> matMetal(matCount, 0.0f);
    std::vector<float> matRough(matCount, 0.5f);
    std::vector<std::string> matUri(matCount);
    std::vector<std::string> matMime(matCount);
    std::size_t texturedMats = 0;
    for (std::size_t i = 0; i < matCount; ++i) {
        matNames[i] = !mesh.materials.empty() && !mesh.materials[i].name.empty()
                          ? mesh.materials[i].name
                          : ("material" + std::to_string(i));
        if (i < pbrMaterials.size()) {
            const PbrMaterial& pm = pbrMaterials[i];
            for (int k = 0; k < 4; ++k) matBase[i * 4 + static_cast<std::size_t>(k)] =
                pm.baseColor[k];
            matMetal[i] = pm.metallic;
            matRough[i] = pm.roughness;
            std::string base;
            if (!pm.albedoTexture.empty() && basenameTextureUri(pm.albedoTexture, base)) {
                matUri[i] = base;
                matMime[i] = endsWith(lowerOf(base), ".png") ? "image/png" : "image/jpeg";
                ++texturedMats;
            } else if (!mesh.materials.empty()) {
                // SMD-sourced DDS paths stay factors-only (no image bytes to
                // reference); the basename gate above keeps PNG/JPEG only.
                std::string base2;
                if (basenameTextureUri(mesh.materials[i].texturePath, base2)) {
                    matUri[i] = base2;
                    matMime[i] =
                        endsWith(lowerOf(base2), ".png") ? "image/png" : "image/jpeg";
                    ++texturedMats;
                }
            }
        } else if (!mesh.materials.empty()) {
            std::string base;
            if (basenameTextureUri(mesh.materials[i].texturePath, base)) {
                matUri[i] = base;
                matMime[i] = endsWith(lowerOf(base), ".png") ? "image/png" : "image/jpeg";
                ++texturedMats;
            }
        }
    }

    // --- JSON ----------------------------------------------------------------
    // Accessor order: 0 pos, 1 nrm, 2 uv, 3 joints, 4 weights, then one
    // indices accessor per primitive, then IBM last.
    const std::uint64_t uVerts = static_cast<std::uint64_t>(vertCount);
    const std::uint64_t uJoints = static_cast<std::uint64_t>(jointCount);
    const std::uint64_t posLen = uVerts * 12ULL;
    const std::uint64_t nrmLen = uVerts * 12ULL;
    const std::uint64_t uvLen = uVerts * 8ULL;
    const std::uint64_t jointsLen = uVerts * jointElem;
    const std::uint64_t weightsLen = uVerts * 16ULL;
    const std::uint64_t idxViewLen =
        static_cast<std::uint64_t>(indexCount) * idxElem +
        ((useU16Index && (indexCount % 2 != 0)) ? 2ULL : 0ULL);
    const std::uint64_t ibmLen = uJoints * 64ULL;
    const int idxComp = useU16Index ? 5123 : 5125;
    const int jointsComp = useU8Joints ? 5121 : 5123;
    // Animation accessor/view indices (only when hasAnim): time input first,
    // then per joint translation + rotation outputs.
    const std::size_t primCount = prims.size();
    const std::size_t animTimeAcc = 6 + primCount;
    const std::uint64_t animTimeLen = static_cast<std::uint64_t>(animCount) * 4ULL;
    const std::uint64_t animTransLen = static_cast<std::uint64_t>(animCount) * 12ULL;
    const std::uint64_t animRotLen = static_cast<std::uint64_t>(animCount) * 16ULL;
    const std::string animName = asset.empty() ? std::string("anim") : asset + "_anim";

    Vec3 posMin{0, 0, 0}, posMax{0, 0, 0};
    if (!mesh.vertices.empty()) {
        posMin = posMax = mesh.vertices[0].position;
        for (const auto& v : mesh.vertices) {
            if (v.position.x < posMin.x) posMin.x = v.position.x;
            if (v.position.y < posMin.y) posMin.y = v.position.y;
            if (v.position.z < posMin.z) posMin.z = v.position.z;
            if (v.position.x > posMax.x) posMax.x = v.position.x;
            if (v.position.y > posMax.y) posMax.y = v.position.y;
            if (v.position.z > posMax.z) posMax.z = v.position.z;
        }
    }

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"m2rig-smd2gltf\"},";
    js << "\"scene\":0,\"scenes\":[{\"nodes\":[0";
    for (std::size_t i = 0; i < jointCount; ++i)
        if (skeleton.bones[i].parentId == kNoParent) js << "," << (1 + i);
    js << "]}],\"nodes\":[";
    js << "{\"name\":\"" << escapeJson(asset.empty() ? mesh.name : asset)
       << "\",\"mesh\":0,\"skin\":0}";
    for (std::size_t i = 0; i < jointCount; ++i) {
        const Bone& b = skeleton.bones[i];
        Quat q = Quat::fromEulerXyz(b.localRotationEuler).normalized();
        js << ",{\"name\":\"" << escapeJson(b.name) << "\"";
        js << ",\"translation\":[" << fmtF(b.localPosition.x) << "," << fmtF(b.localPosition.y)
           << "," << fmtF(b.localPosition.z) << "]";
        js << ",\"rotation\":[" << fmtF(q.x) << "," << fmtF(q.y) << "," << fmtF(q.z) << ","
           << fmtF(q.w) << "]";
        js << ",\"scale\":[" << fmtF(b.localScale.x) << "," << fmtF(b.localScale.y) << ","
           << fmtF(b.localScale.z) << "]";
        if (!b.children.empty()) {
            js << ",\"children\":[";
            for (std::size_t k = 0; k < b.children.size(); ++k) {
                if (k > 0) js << ",";
                js << (1 + b.children[k]);
            }
            js << "]";
        }
        js << "}";
    }
    js << "],\"meshes\":[{\"name\":\"" << escapeJson(mesh.name) << "\",\"primitives\":[";
    for (std::size_t pi = 0; pi < prims.size(); ++pi) {
        if (pi > 0) js << ",";
        js << "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,"
              "\"WEIGHTS_0\":4}";
        js << ",\"indices\":" << (5 + pi) << ",\"mode\":4";
        if (!mesh.materials.empty()) js << ",\"material\":" << prims[pi].material;
        js << "}";
    }
    js << "]}],\"skins\":[{\"name\":\"" << escapeJson(asset.empty() ? "Armature" : asset)
       << "\",\"joints\":[";
    for (std::size_t i = 0; i < jointCount; ++i) {
        if (i > 0) js << ",";
        js << (1 + i);
    }
    const std::size_t ibmAcc = 5 + prims.size();
    js << "],\"inverseBindMatrices\":" << ibmAcc << "}],\"materials\":[";
    // Texture/image index per material (or -1).
    std::vector<int> matTex(matCount, -1);
    {
        int ti = 0;
        for (std::size_t i = 0; i < matCount; ++i)
            if (!matUri[i].empty()) matTex[i] = ti++;
    }
    for (std::size_t i = 0; i < matCount; ++i) {
        if (i > 0) js << ",";
        js << "{\"name\":\"" << escapeJson(matNames[i]) << "\",\"pbrMetallicRoughness\":{";
        js << "\"baseColorFactor\":[" << fmtF(matBase[i * 4]) << "," << fmtF(matBase[i * 4 + 1])
           << "," << fmtF(matBase[i * 4 + 2]) << "," << fmtF(matBase[i * 4 + 3]) << "]";
        js << ",\"metallicFactor\":" << fmtF(matMetal[i]);
        js << ",\"roughnessFactor\":" << fmtF(matRough[i]);
        if (matTex[i] >= 0) js << ",\"baseColorTexture\":{\"index\":" << matTex[i] << "}";
        js << "}}";
    }
    js << "]";
    if (texturedMats > 0) {
        js << ",\"textures\":[";
        bool first = true;
        for (std::size_t i = 0; i < matCount; ++i) {
            if (matTex[i] < 0) continue;
            if (!first) js << ",";
            first = false;
            js << "{\"source\":" << matTex[i] << ",\"name\":\"" << escapeJson(matNames[i])
               << "\"}";
        }
        js << "],\"images\":[";
        first = true;
        for (std::size_t i = 0; i < matCount; ++i) {
            if (matTex[i] < 0) continue;
            if (!first) js << ",";
            first = false;
            js << "{\"name\":\"" << escapeJson(matNames[i]) << "\",\"uri\":\""
               << escapeJson(matUri[i]) << "\",\"mimeType\":\"" << matMime[i] << "\"}";
        }
        js << "]";
    }
    if (hasAnim) {
        js << ",\"animations\":[{\"name\":\"" << escapeJson(animName) << "\",\"channels\":[";
        for (std::size_t j = 0; j < jointCount; ++j) {
            for (int k = 0; k < 2; ++k) {
                if (j > 0 || k > 0) js << ",";
                js << "{\"sampler\":" << (2 * j + static_cast<std::size_t>(k))
                   << ",\"target\":{\"node\":" << (1 + j) << ",\"path\":\""
                   << (k == 0 ? "translation" : "rotation") << "\"}}";
            }
        }
        js << "],\"samplers\":[";
        for (std::size_t j = 0; j < jointCount; ++j) {
            if (j > 0) js << ",";
            js << "{\"input\":" << animTimeAcc << ",\"interpolation\":\"LINEAR\",\"output\":"
               << (animTimeAcc + 1 + 2 * j) << "}";
            js << ",{\"input\":" << animTimeAcc << ",\"interpolation\":\"LINEAR\",\"output\":"
               << (animTimeAcc + 2 + 2 * j) << "}";
        }
        js << "]}]";
    }
    js << ",\"accessors\":[";
    js << "{\"bufferView\":0,\"componentType\":5126,\"count\":" << vertCount
       << ",\"type\":\"VEC3\",\"min\":[" << fmtF(posMin.x) << "," << fmtF(posMin.y) << ","
       << fmtF(posMin.z) << "],\"max\":[" << fmtF(posMax.x) << "," << fmtF(posMax.y) << ","
       << fmtF(posMax.z) << "]}";
    js << ",{\"bufferView\":1,\"componentType\":5126,\"count\":" << vertCount
       << ",\"type\":\"VEC3\"}";
    js << ",{\"bufferView\":2,\"componentType\":5126,\"count\":" << vertCount
       << ",\"type\":\"VEC2\"}";
    js << ",{\"bufferView\":3,\"componentType\":" << jointsComp << ",\"count\":" << vertCount
       << ",\"type\":\"VEC4\"}";
    js << ",{\"bufferView\":4,\"componentType\":5126,\"count\":" << vertCount
       << ",\"type\":\"VEC4\"}";
    for (std::size_t pi = 0; pi < prims.size(); ++pi) {
        const std::uint64_t accOff =
            static_cast<std::uint64_t>(prims[pi].start) * idxElem;
        js << ",{\"bufferView\":5,\"byteOffset\":" << accOff << ",\"componentType\":" << idxComp
           << ",\"count\":" << prims[pi].count << ",\"type\":\"SCALAR\"}";
    }
    js << ",{\"bufferView\":6,\"componentType\":5126,\"count\":" << jointCount
       << ",\"type\":\"MAT4\"}";
    if (hasAnim) {
        // Views are per buffer block (prim-independent): time = 7, then per
        // joint T/R pairs (accessor indices above DO shift with primCount).
        js << ",{\"bufferView\":7,\"componentType\":5126,\"count\":" << animCount
           << ",\"type\":\"SCALAR\"}";
        for (std::size_t j = 0; j < jointCount; ++j) {
            js << ",{\"bufferView\":" << (8 + 2 * j) << ",\"componentType\":5126,\"count\":"
               << animCount << ",\"type\":\"VEC3\"}";
            js << ",{\"bufferView\":" << (9 + 2 * j) << ",\"componentType\":5126,\"count\":"
               << animCount << ",\"type\":\"VEC4\"}";
        }
    }
    js << "],\"bufferViews\":[";
    js << "{\"buffer\":0,\"byteOffset\":" << posOff << ",\"byteLength\":" << posLen << "}";
    js << ",{\"buffer\":0,\"byteOffset\":" << nrmOff << ",\"byteLength\":" << nrmLen << "}";
    js << ",{\"buffer\":0,\"byteOffset\":" << uvOff << ",\"byteLength\":" << uvLen << "}";
    js << ",{\"buffer\":0,\"byteOffset\":" << jointsOff << ",\"byteLength\":" << jointsLen
       << "}";
    js << ",{\"buffer\":0,\"byteOffset\":" << weightsOff << ",\"byteLength\":" << weightsLen
       << "}";
    js << ",{\"buffer\":0,\"byteOffset\":" << idxOff << ",\"byteLength\":" << idxViewLen
       << "}";
    js << ",{\"buffer\":0,\"byteOffset\":" << ibmOff << ",\"byteLength\":" << ibmLen << "}";
    if (hasAnim) {
        js << ",{\"buffer\":0,\"byteOffset\":" << animTimeOff
           << ",\"byteLength\":" << animTimeLen << "}";
        for (std::size_t j = 0; j < jointCount; ++j) {
            js << ",{\"buffer\":0,\"byteOffset\":" << animTransOff[j]
               << ",\"byteLength\":" << animTransLen << "}";
            js << ",{\"buffer\":0,\"byteOffset\":" << animRotOff[j]
               << ",\"byteLength\":" << animRotLen << "}";
        }
    }
    js << "],\"buffers\":[{\"byteLength\":" << bin.size() << "}]}";
    const std::string json = js.str();
    if (json.size() > kMaxGltfJsonBytes)
        return Result<std::string>::fail("Emit JSON exceeds the 64 MB cap.", "EXPORT",
                                         asset, kOp);

    // --- GLB container (single BIN chunk) ------------------------------------
    const std::size_t jsonPadded = (json.size() + 3) & ~static_cast<std::size_t>(3);
    std::uint64_t total = 0, chunkSum = 0;
    if (!checkedAddU64(12ULL, 8ULL, chunkSum) ||
        !checkedAddU64(chunkSum, jsonPadded, chunkSum) ||
        !checkedAddU64(chunkSum, 8ULL, chunkSum) ||
        !checkedAddU64(chunkSum, bin.size(), total) || total > 0xFFFFFFFFULL)
        return Result<std::string>::fail("Emit container exceeds 4 GB.", "EXPORT", asset,
                                         kOp);
    std::vector<std::uint8_t> glb;
    glb.reserve(static_cast<std::size_t>(total));
    pushU32le(glb, kGlbMagic);
    pushU32le(glb, kGlbVersion2);
    pushU32le(glb, static_cast<std::uint32_t>(total));
    pushU32le(glb, static_cast<std::uint32_t>(jsonPadded));
    pushU32le(glb, kGlbJsonChunk);
    glb.insert(glb.end(), json.begin(), json.end());
    while (glb.size() % 4 != 0) glb.push_back(0x20);
    pushU32le(glb, static_cast<std::uint32_t>(bin.size()));
    pushU32le(glb, kGlbBinChunk);
    glb.insert(glb.end(), bin.begin(), bin.end());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return Result<std::string>::fail("Cannot open output file: " + path, "IO", asset,
                                         kOp);
    file.write(reinterpret_cast<const char*>(glb.data()),
               static_cast<std::streamsize>(glb.size()));
    file.close();
    if (!file)
        return Result<std::string>::fail("Failed writing output file: " + path, "IO",
                                         asset, kOp);

    std::ostringstream note;
    note << "glTF Y-up: no axis conversion (canonical is Y-up).";
    note << "\nWrote " << vertCount << " verts, " << (indexCount / 3) << " tris, "
         << jointCount << " joints, " << matCount << " materials (" << texturedMats
         << " textured, basename-only URIs).";
    if (hasAnim)
        note << "\nAnimation '" << animName << "': " << animCount
             << " frame(s) at 30 fps (input time = SmdFrame.time/30.0), per-joint LINEAR "
                "translation+rotation samplers (euler rad -> Quat::fromEulerXyz, normalized).";
    else
        note << "\nAnimation samplers omitted (no clip passed; use the animation overload "
                "with bakeClipFrames() SmdFrames output to emit samplers).";
    return Result<std::string>::ok(note.str());
}

}  // namespace m2rig
