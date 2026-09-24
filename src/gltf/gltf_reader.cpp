// Native glTF 2.0 reader on top of cgltf (jkuhlmann, MIT).
// Design notes:
// - cgltf only parses: every buffer byte is loaded by this file through
//   capped, relative-URI-only IO (cgltf_load_buffers is never called, so no
//   fopen-based path resolution can escape the allowlist).
// - cgltf_validate()/unpack helpers are never called either (see
//   CVE-2026-75148, integer overflow in the upstream bounds check with no
//   fixed release at our pin): every accessor read goes through the
//   overflow-checked accessorRange() below, the single bounds authority.
// - glTF is Y-up by spec and canonical is Y-up, so there is NO axis
//   arbitration (unlike FBX). diagnoseOrientation still runs as a warn-only
//   gate recorded in conversionNote, never a blocker.
// - Mesh-instance world transforms are baked into vertices (positions plus
//   inverse-transpose normals); joint locals and inverse-bind matrices are
//   stored exactly as authored, so the bind pose is preserved bit-faithfully.
#include "m2rig/gltf/gltf_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _MSC_VER
// Third-party header: its warnings must not break our /W4 /WX build.
#pragma warning(push, 0)
#endif
#include <cgltf.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "m2rig/coordsys.hpp"
#include "m2rig/math.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"  // SmdFrame/SmdBonePose (animation timeline output)

namespace m2rig {

namespace {

// Frame convention shared with the writer (see ConvertedGltf::frames):
// glTF keyframe times are seconds on a 30 fps timeline; SMD frames are
// integer indices, so frame = round(time * 30) (half up) and emit writes
// input times as frame / 30.0. Documented here, on the member, and in the
// conversion note appended below.
constexpr double kGltfAnimFps = 30.0;

constexpr std::uint64_t kMaxGltfJsonBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxBinBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxGlbBytes = kMaxGltfJsonBytes + kMaxBinBytes + 1024ULL;
constexpr std::size_t kMaxImages = 4096;

constexpr std::uint32_t kGlbMagic = 0x46546C67u;      // 'glTF' little-endian
constexpr std::uint32_t kGlbVersion2 = 2u;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534Au;  // 'JSON'
constexpr std::uint32_t kGlbBinChunk = 0x004E4942u;   // 'BIN\0'

bool checkedAdd(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    out = a + b;
    return out >= a;
}

bool checkedMul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    out = a * b;
    return out / a == b;
}

bool checkedRange(std::uint64_t base, std::uint64_t len, std::uint64_t bound) {
    std::uint64_t end = 0;
    return checkedAdd(base, len, end) && end <= bound;
}

std::string cstr(const char* p) { return p ? std::string(p) : std::string(); }

std::uint32_t readU32le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

float readF32le(const std::uint8_t* p) {
    float f = 0.0f;
    std::memcpy(&f, p, sizeof(float));
    return f;
}

std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Relative-URI-only gate: rejects .. traversal (raw and backslash forms),
// absolute paths, URI schemes, drive letters and percent-encoded paths
// (our loader never percent-decodes, so encoded input cannot resolve to a
// real file — reject it rather than guess). data: URIs are rejected with
// an actionable message (use sidecar files or .glb instead).
bool isUriPathSafe(const std::string& uri, std::string& reason) {
    if (uri.empty()) {
        reason = "empty URI";
        return false;
    }
    if (uri.starts_with("data:")) {
        reason = "data: URIs are not supported (use a sidecar file or .glb)";
        return false;
    }
    if (uri.find('%') != std::string::npos) {
        reason = "percent-encoded URIs are not supported";
        return false;
    }
    if (uri.find('\\') != std::string::npos) {
        reason = "backslashes are not valid in glTF URIs";
        return false;
    }
    if (uri[0] == '/') {
        reason = "absolute paths are not allowed (relative URIs only)";
        return false;
    }
    if (uri.find("..") != std::string::npos) {
        reason = "'..' traversal is not allowed (relative URIs only)";
        return false;
    }
    if (uri.find(':') != std::string::npos) {
        reason = "URI schemes and drive letters are not allowed (relative URIs only)";
        return false;
    }
    return true;
}

Result<std::vector<std::uint8_t>> readCappedFile(const std::string& path, std::uint64_t cap,
                                                 const std::string& asset,
                                                 const std::string& op) {
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return Result<std::vector<std::uint8_t>>::fail("Cannot open file: " + path, "IO", asset,
                                                       op);
    const std::streamsize end = file.tellg();
    if (end < 0)
        return Result<std::vector<std::uint8_t>>::fail("Cannot stat file: " + path, "IO", asset,
                                                       op);
    const std::uint64_t size = static_cast<std::uint64_t>(end);
    if (size == 0 || size > cap)
        return Result<std::vector<std::uint8_t>>::fail(
            "File " + path + " has size " + std::to_string(size) + " bytes (cap " +
                std::to_string(cap) + ").",
            "FORMAT", asset, op);
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(out.data()), end))
        return Result<std::vector<std::uint8_t>>::fail("Failed reading file: " + path, "IO",
                                                       asset, op);
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

struct GlbView {
    const std::uint8_t* json = nullptr;
    std::size_t jsonLen = 0;
    const std::uint8_t* bin = nullptr;
    std::size_t binLen = 0;
    bool hasBin = false;
};

// Validates the GLB container (magic, version, chunk headers and lengths)
// with overflow-checked arithmetic BEFORE the bytes reach cgltf_parse.
Result<GlbView> splitGlb(const std::vector<std::uint8_t>& bytes, const std::string& asset) {
    constexpr const char* kOp = "gltf.glb";
    if (bytes.size() < 12)
        return Result<GlbView>::fail("Not a GLB file (shorter than the 12-byte header).",
                                     "FORMAT", asset, kOp);
    const std::uint8_t* p = bytes.data();
    if (readU32le(p) != kGlbMagic)
        return Result<GlbView>::fail("Not a GLB file (bad 'glTF' magic).", "FORMAT", asset,
                                     kOp);
    const std::uint32_t version = readU32le(p + 4);
    if (version != kGlbVersion2)
        return Result<GlbView>::fail(
            "Unsupported glTF version " + std::to_string(version) + " (only 2.0).", "FORMAT",
            asset, kOp);
    if (static_cast<std::uint64_t>(readU32le(p + 8)) != bytes.size())
        return Result<GlbView>::fail("GLB length prefix does not match the file size.",
                                     "FORMAT", asset, kOp);
    if (bytes.size() < 20)
        return Result<GlbView>::fail("GLB truncated inside the JSON chunk header.", "FORMAT",
                                     asset, kOp);
    const std::uint64_t jsonLen = readU32le(p + 12);
    if (jsonLen > kMaxGltfJsonBytes)
        return Result<GlbView>::fail("GLB JSON chunk exceeds the 64 MB cap.", "FORMAT", asset,
                                     kOp);
    if (readU32le(p + 16) != kGlbJsonChunk)
        return Result<GlbView>::fail("GLB first chunk is not JSON.", "FORMAT", asset, kOp);
    std::uint64_t jsonEnd = 0;
    if (!checkedAdd(20ULL, jsonLen, jsonEnd) || jsonEnd > bytes.size())
        return Result<GlbView>::fail("GLB JSON chunk overruns the file.", "FORMAT", asset,
                                     kOp);
    GlbView view;
    view.json = p + 20;
    view.jsonLen = static_cast<std::size_t>(jsonLen);
    const std::size_t jsonEndSize = static_cast<std::size_t>(jsonEnd);
    if (jsonEndSize == bytes.size()) return Result<GlbView>::ok(view);  // no BIN chunk
    if (bytes.size() - jsonEndSize < 8)
        return Result<GlbView>::fail("GLB truncated inside the BIN chunk header.", "FORMAT",
                                     asset, kOp);
    const std::uint8_t* bq = p + jsonEndSize;
    const std::uint64_t binLen = readU32le(bq);
    if (binLen > kMaxBinBytes)
        return Result<GlbView>::fail("GLB BIN chunk exceeds the 512 MB cap.", "FORMAT", asset,
                                     kOp);
    if (readU32le(bq + 4) != kGlbBinChunk)
        return Result<GlbView>::fail("GLB second chunk is not BIN.", "FORMAT", asset, kOp);
    std::uint64_t binEnd = 0;
    if (!checkedAdd(static_cast<std::uint64_t>(jsonEndSize) + 8ULL, binLen, binEnd) ||
        binEnd != bytes.size())
        return Result<GlbView>::fail("GLB BIN chunk overruns the file or has trailing bytes.",
                                     "FORMAT", asset, kOp);
    view.bin = bq + 8;
    view.binLen = static_cast<std::size_t>(binLen);
    view.hasBin = true;
    return Result<GlbView>::ok(view);
}

const char* cgltfResultName(cgltf_result r) {
    switch (r) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "IO error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy glTF (only 2.0 is supported)";
        default: return "unknown error";
    }
}

// Owns the parsed document plus every buffer byte it points at. Buffer data
// pointers are nulled before cgltf_free so the free path can never release
// (or double-release) memory owned here, whatever the header version does.
struct ParsedGltf {
    cgltf_data* data = nullptr;
    std::vector<std::uint8_t> owned;
    std::vector<std::vector<std::uint8_t>> sidecars;
    std::unordered_map<const cgltf_buffer*, std::uint64_t> loadedSizes;

    ~ParsedGltf() {
        if (data) {
            for (std::size_t i = 0; i < data->buffers_count; ++i) data->buffers[i].data = nullptr;
            cgltf_free(data);
        }
    }
    ParsedGltf(const ParsedGltf&) = delete;
    ParsedGltf& operator=(const ParsedGltf&) = delete;
    ParsedGltf() = default;
};

// Single bounds authority for every accessor read (see file header note on
// CVE-2026-75148): view range inside the LOADED buffer size, then the
// accessor span inside the view, all in overflow-checked uint64 arithmetic.
struct AccessorBytes {
    const std::uint8_t* base = nullptr;  // first element
    std::uint64_t stride = 0;            // bytes per element
    std::uint64_t count = 0;
};

Result<AccessorBytes> accessorRange(
    const cgltf_accessor* acc, std::uint64_t elemSize,
    const std::unordered_map<const cgltf_buffer*, std::uint64_t>& loaded,
    const std::string& asset, const std::string& what) {
    constexpr const char* kOp = "gltf.accessor";
    if (!acc->buffer_view)
        return Result<AccessorBytes>::fail(what + " accessor has no buffer view.", "FORMAT",
                                           asset, kOp);
    const cgltf_buffer_view* view = acc->buffer_view;
    if (!view->buffer)
        return Result<AccessorBytes>::fail(what + " buffer view has no buffer.", "FORMAT",
                                           asset, kOp);
    const auto it = loaded.find(view->buffer);
    if (it == loaded.end() || view->buffer->data == nullptr)
        return Result<AccessorBytes>::fail(what + " buffer data was not loaded.", "FORMAT",
                                           asset, kOp);
    const auto* base = static_cast<const std::uint8_t*>(view->buffer->data);
    const std::uint64_t bufSize = it->second;
    if (!checkedRange(view->offset, view->size, bufSize))
        return Result<AccessorBytes>::fail(what + " buffer view range overruns its buffer.",
                                           "FORMAT", asset, kOp);
    const std::uint64_t stride = acc->stride ? static_cast<std::uint64_t>(acc->stride) : elemSize;
    if (stride < elemSize)
        return Result<AccessorBytes>::fail(what + " accessor stride is smaller than one element.",
                                           "FORMAT", asset, kOp);
    std::uint64_t span = 0;
    if (acc->count > 0) {
        std::uint64_t step = 0;
        if (!checkedMul(static_cast<std::uint64_t>(acc->count - 1), stride, step) ||
            !checkedAdd(step, elemSize, span))
            return Result<AccessorBytes>::fail(what + " accessor range overflows.", "FORMAT",
                                               asset, kOp);
    }
    std::uint64_t absOff = 0;
    std::uint64_t absEnd = 0;
    if (!checkedAdd(view->offset, acc->offset, absOff) || !checkedAdd(absOff, span, absEnd) ||
        absEnd > bufSize)
        return Result<AccessorBytes>::fail(what + " accessor range overruns its buffer.",
                                           "FORMAT", asset, kOp);
    AccessorBytes out;
    out.base = base + static_cast<std::size_t>(absOff);
    out.stride = stride;
    out.count = acc->count;
    return Result<AccessorBytes>::ok(out);
}

const std::uint8_t* elementAt(const AccessorBytes& range, std::uint64_t index) {
    // Safe by construction: (count-1)*stride+elem was range-checked, and
    // index < count, so index*stride cannot overflow past the checked span.
    return range.base + static_cast<std::size_t>(index * range.stride);
}

ResultVoid requireFloatVec(const cgltf_accessor* acc, cgltf_type type, const std::string& what,
                           const std::string& asset) {
    constexpr const char* kOp = "gltf.mesh";
    if (acc->type != type)
        return ResultVoid::fail(what + " has the wrong accessor type.", "FORMAT", asset, kOp);
    if (acc->component_type != cgltf_component_type_r_32f)
        return ResultVoid::fail(what + " must be float data.", "FORMAT", asset, kOp);
    if (acc->normalized)
        return ResultVoid::fail(what + " must not be normalized.", "FORMAT", asset, kOp);
    if (acc->is_sparse)
        return ResultVoid::fail(what + " uses a sparse accessor (NOT_SUPPORTED_YET).",
                                "FORMAT", asset, kOp);
    if (acc->count == 0)
        return ResultVoid::fail(what + " is empty.", "FORMAT", asset, kOp);
    return ResultVoid::ok();
}

// Column-major cgltf matrix (column vectors, v' = M v) -> row-major
// canonical Mat4 (row vectors, v' = v * R) with R = M transposed, so the
// translation column lands in m[3][0..2]. A verbatim copy would park the
// translation in column 3 and apply inverse rotations downstream.
Mat4 mat4FromColumnMajor(const cgltf_float m[16]) {
    Mat4 r;
    for (int rr = 0; rr < 4; ++rr)
        for (int c = 0; c < 4; ++c) r.m[rr][c] = m[rr * 4 + c];
    return r;
}

bool isIdentityMat(const Mat4& m, float eps) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            const float want = (r == c) ? 1.0f : 0.0f;
            if (!std::isfinite(m.m[r][c]) || std::fabs(m.m[r][c] - want) > eps) return false;
        }
    return true;
}

// glTF node TRS (or matrix) -> canonical local position/euler/scale.
// Rotation quaternions go through Quat->matrix->eulerXyzFromRotation, the
// exact inverse of the canonical rotationEulerXyz composer.
Result<BoneDefinition> jointLocal(const cgltf_node* node, std::size_t jointIndex,
                                  const std::string& asset) {
    constexpr const char* kOp = "gltf.skeleton";
    BoneDefinition def;
    def.name = cstr(node->name);
    if (def.name.empty()) def.name = "joint" + std::to_string(jointIndex);
    if (node->has_matrix &&
        (node->has_translation || node->has_rotation || node->has_scale))
        return Result<BoneDefinition>::fail(
            "Joint '" + def.name + "' has both matrix and TRS (mutually exclusive).",
            "FORMAT", asset, kOp);
    if (node->has_matrix) {
        for (int i = 0; i < 16; ++i)
            if (!std::isfinite(node->matrix[i]))
                return Result<BoneDefinition>::fail(
                    "Joint '" + def.name + "' has a non-finite node matrix.", "FORMAT", asset,
                    kOp);
        const Vec3 t{node->matrix[12], node->matrix[13], node->matrix[14]};
        const Vec3 rows[3] = {{node->matrix[0], node->matrix[4], node->matrix[8]},
                              {node->matrix[1], node->matrix[5], node->matrix[9]},
                              {node->matrix[2], node->matrix[6], node->matrix[10]}};
        const Vec3 s{length(rows[0]), length(rows[1]), length(rows[2])};
        if (s.x < 1e-9f || s.y < 1e-9f || s.z < 1e-9f)
            return Result<BoneDefinition>::fail(
                "Joint '" + def.name + "' has a degenerate (zero-scale) node matrix.",
                "FORMAT", asset, kOp);
        Mat4 rot = Mat4::identity();
        for (int r = 0; r < 3; ++r) {
            const Vec3 n = rows[r] / (r == 0 ? s.x : (r == 1 ? s.y : s.z));
            rot.m[r][0] = n.x;
            rot.m[r][1] = n.y;
            rot.m[r][2] = n.z;
        }
        def.localPosition = t;
        def.localScale = s;
        def.localRotationEuler = rot.eulerXyzFromRotation();
        return Result<BoneDefinition>::ok(std::move(def));
    }
    Vec3 t{0, 0, 0};
    Vec3 s{1, 1, 1};
    if (node->has_translation) {
        t = {node->translation[0], node->translation[1], node->translation[2]};
        if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z))
            return Result<BoneDefinition>::fail(
                "Joint '" + def.name + "' has a non-finite translation.", "FORMAT", asset,
                kOp);
    }
    if (node->has_scale) {
        s = {node->scale[0], node->scale[1], node->scale[2]};
        if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z))
            return Result<BoneDefinition>::fail(
                "Joint '" + def.name + "' has a non-finite scale.", "FORMAT", asset, kOp);
        if (std::fabs(s.x) < 1e-9f || std::fabs(s.y) < 1e-9f || std::fabs(s.z) < 1e-9f)
            return Result<BoneDefinition>::fail(
                "Joint '" + def.name + "' has a degenerate (zero) scale.", "FORMAT", asset,
                kOp);
    }
    Vec3 euler{0, 0, 0};
    if (node->has_rotation) {
        const float qx = node->rotation[0];
        const float qy = node->rotation[1];
        const float qz = node->rotation[2];
        const float qw = node->rotation[3];
        if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) ||
            !std::isfinite(qw))
            return Result<BoneDefinition>::fail(
                "Joint '" + def.name + "' has a non-finite rotation quaternion.", "FORMAT",
                asset, kOp);
        const float len =
            std::sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
        if (len < 1e-6f)
            return Result<BoneDefinition>::fail(
                "Joint '" + def.name + "' has a zero-length rotation quaternion.", "FORMAT",
                asset, kOp);
        const Quat q{qx / len, qy / len, qz / len, qw / len};
        euler = q.toMatrix().eulerXyzFromRotation();
    }
    def.localPosition = t;
    def.localRotationEuler = euler;
    def.localScale = s;
    return Result<BoneDefinition>::ok(std::move(def));
}

// Accumulates face normals only into flagged vertices (mixed file/missing
// case). computeNormals() would silently clobber file normals, so it runs
// only when NO primitive supplied normals at all.
void fillMissingNormals(Mesh& mesh, const std::vector<char>& missing) {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t ids[3] = {mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]};
        if (ids[0] >= mesh.vertices.size() || ids[1] >= mesh.vertices.size() ||
            ids[2] >= mesh.vertices.size())
            continue;
        const Vec3 n = cross(mesh.vertices[ids[1]].position - mesh.vertices[ids[0]].position,
                             mesh.vertices[ids[2]].position - mesh.vertices[ids[0]].position);
        for (int k = 0; k < 3; ++k)
            if (missing[ids[k]]) mesh.vertices[ids[k]].normal += n;
    }
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        if (!missing[i]) continue;
        const float l = length(mesh.vertices[i].normal);
        mesh.vertices[i].normal =
            (l > 1e-12f) ? mesh.vertices[i].normal / l : Vec3{0, 0, 1};
    }
}

}  // namespace

Result<ConvertedGltf> readGltfFile(const std::string& path, const std::string& assetName) {
    const std::string asset = assetName.empty() ? path : assetName;
    const std::string ext = lowerOf([&] {
        const auto pos = path.find_last_of('.');
        return (pos == std::string::npos) ? std::string() : path.substr(pos);
    }());
    const bool wantGlb = (ext == ".glb");
    if (ext != ".gltf" && ext != ".glb")
        return Result<ConvertedGltf>::fail(
            "Unknown glTF extension '" + ext + "' (expected .gltf or .glb).", "FORMAT",
            asset, "gltf.read");

    auto bytesRes = readCappedFile(path, wantGlb ? kMaxGlbBytes : kMaxGltfJsonBytes, asset,
                                   "gltf.read");
    if (!bytesRes)
        return Result<ConvertedGltf>::fail(bytesRes.error());
    ParsedGltf parsed;
    parsed.owned = std::move(bytesRes.value());

    // Container validation for .glb BEFORE parsing (caps + chunk geometry).
    GlbView glb;
    bool isGlb = false;
    if (wantGlb) {
        auto glbRes = splitGlb(parsed.owned, asset);
        if (!glbRes) return Result<ConvertedGltf>::fail(glbRes.error());
        glb = glbRes.value();
        isGlb = true;
    }

    const std::string jsonText = isGlb
                                     ? std::string(reinterpret_cast<const char*>(glb.json),
                                                   glb.jsonLen)
                                     : std::string(reinterpret_cast<const char*>(
                                                       parsed.owned.data()),
                                                   parsed.owned.size());

    // Compression extensions are never decoded: fail loudly, never silently.
    // (Raw-JSON scan, so detection does not depend on struct fields that
    // only exist at newer pins.)
    for (const char* token :
         {"KHR_draco_mesh_compression", "EXT_meshopt_compression", "KHR_meshopt_compression"}) {
        if (jsonText.find(token) != std::string::npos)
            return Result<ConvertedGltf>::fail(
                std::string("glTF uses ") + token + " mesh compression (NOT_SUPPORTED_YET).",
                "FORMAT", asset, "gltf.parse");
    }

    cgltf_options opts{};
    cgltf_data* raw = nullptr;
    const cgltf_result pres =
        cgltf_parse(&opts, parsed.owned.data(), parsed.owned.size(), &raw);
    if (pres != cgltf_result_success || !raw)
        return Result<ConvertedGltf>::fail(
            std::string("cgltf parse failed: ") + cgltfResultName(pres) + ".", "FORMAT",
            asset, "gltf.parse");
    parsed.data = raw;
    cgltf_data* data = raw;

    if (data->extensions_required_count > 0) {
        std::string names;
        for (std::size_t i = 0; i < data->extensions_required_count && i < 4; ++i) {
            if (i > 0) names += ", ";
            names += "'" + cstr(data->extensions_required[i]) + "'";
        }
        return Result<ConvertedGltf>::fail(
            "glTF requires extension(s) " + names + " (extensionsRequired NOT_SUPPORTED_YET).",
            "FORMAT", asset, "gltf.parse");
    }
    if (data->animations_count > 1)
        return Result<ConvertedGltf>::fail(
            "glTF contains " + std::to_string(data->animations_count) +
                " animations (only single-animation assets are supported; export one clip "
                "instead).",
            "FORMAT", asset, "gltf.parse");
    if (data->images_count > kMaxImages)
        return Result<ConvertedGltf>::fail(
            "glTF declares " + std::to_string(data->images_count) +
                " images (cap 4096).",
            "FORMAT", asset, "gltf.parse");
    if (data->skins_count == 0)
        return Result<ConvertedGltf>::fail(
            "glTF has no skins (static meshes are NOT_SUPPORTED_YET in 29a).", "FORMAT",
            asset, "gltf.skin");
    if (data->skins_count > 1)
        return Result<ConvertedGltf>::fail(
            "glTF has " + std::to_string(data->skins_count) +
                " skins (only single-skin assets are supported).",
            "FORMAT", asset, "gltf.skin");
    cgltf_skin* skin = &data->skins[0];
    const std::size_t jointCount = skin->joints_count;
    if (jointCount == 0)
        return Result<ConvertedGltf>::fail("glTF skin has no joints.", "FORMAT", asset,
                                           "gltf.skin");
    if (!skin->inverse_bind_matrices)
        return Result<ConvertedGltf>::fail(
            "glTF skin lacks inverseBindMatrices (bind pose undefined).", "FORMAT", asset,
            "gltf.skin");

    // --- buffers: our own capped IO, never cgltf_load_buffers ----------------
    const std::string baseDir = std::filesystem::path(path).parent_path().string();
    for (std::size_t bi = 0; bi < data->buffers_count; ++bi) {
        cgltf_buffer* buf = &data->buffers[bi];
        const std::string uri = cstr(buf->uri);
        if (uri.empty()) {
            // GLB-embedded BIN chunk (buffer 0). data->bin points into owned.
            if (!isGlb || !glb.hasBin || bi != 0 || data->bin == nullptr)
                return Result<ConvertedGltf>::fail(
                    "glTF buffer " + std::to_string(bi) +
                        " has no URI (only sidecar .bin files and the GLB BIN chunk are "
                        "supported).",
                    "FORMAT", asset, "gltf.read");
            if (buf->size != glb.binLen)
                return Result<ConvertedGltf>::fail(
                    "GLB buffer byteLength (" + std::to_string(buf->size) +
                        ") does not match the BIN chunk (" + std::to_string(glb.binLen) +
                        ").",
                    "FORMAT", asset, "gltf.read");
            buf->data = const_cast<void*>(data->bin);
            parsed.loadedSizes[buf] = glb.binLen;
            continue;
        }
        std::string reason;
        if (!isUriPathSafe(uri, reason))
            return Result<ConvertedGltf>::fail(
                "glTF buffer URI '" + uri + "' rejected: " + reason + ".", "FORMAT", asset,
                "gltf.read");
        if (!endsWith(lowerOf(uri), ".bin"))
            return Result<ConvertedGltf>::fail(
                "glTF buffer URI '" + uri +
                    "' is not a .bin file (allowlist: sidecar buffers must be .bin).",
                "FORMAT", asset, "gltf.read");
        const std::string fullPath =
            baseDir.empty() ? uri : (std::filesystem::path(baseDir) / uri).string();
        auto binRes = readCappedFile(fullPath, kMaxBinBytes, asset, "gltf.read");
        if (!binRes) return Result<ConvertedGltf>::fail(binRes.error());
        if (binRes.value().size() != buf->size)
            return Result<ConvertedGltf>::fail(
                "Buffer '" + uri + "' size mismatch (declared " + std::to_string(buf->size) +
                    ", file has " + std::to_string(binRes.value().size()) + ").",
                "FORMAT", asset, "gltf.read");
        if (buf->size == 0)
            return Result<ConvertedGltf>::fail("Buffer '" + uri + "' is empty.", "FORMAT",
                                               asset, "gltf.read");
        parsed.sidecars.push_back(std::move(binRes.value()));
        buf->data = parsed.sidecars.back().data();
        parsed.loadedSizes[buf] = buf->size;
    }

    // --- skeleton: skin joints in order, nearest-joint-ancestor parents -----
    std::unordered_map<const cgltf_node*, std::uint32_t> jointIndex;
    for (std::size_t i = 0; i < jointCount; ++i) {
        if (!skin->joints[i])
            return Result<ConvertedGltf>::fail("glTF skin joint " + std::to_string(i) +
                                                   " is null.",
                                               "FORMAT", asset, "gltf.skeleton");
        jointIndex[skin->joints[i]] = static_cast<std::uint32_t>(i);
    }
    std::vector<BoneDefinition> defs;
    defs.reserve(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        auto localRes = jointLocal(skin->joints[i], i, asset);
        if (!localRes) return Result<ConvertedGltf>::fail(localRes.error());
        BoneDefinition def = std::move(localRes.value());
        def.parentId = kNoParent;
        for (const cgltf_node* p = skin->joints[i]->parent; p; p = p->parent) {
            const auto it = jointIndex.find(p);
            if (it != jointIndex.end()) {
                def.parentId = static_cast<std::int32_t>(it->second);
                break;
            }
        }
        defs.push_back(std::move(def));
    }
    auto builtRes = buildSkeleton(asset, defs);
    if (!builtRes) return Result<ConvertedGltf>::fail(builtRes.error());
    Skeleton skeleton = std::move(builtRes.value());
    skeleton.name = asset;

    // The joint hierarchy composes from the root joint in our Skeleton, so a
    // non-identity transform above a root joint would silently offset the
    // bind. Reject nested armatures explicitly instead.
    for (std::size_t i = 0; i < jointCount; ++i) {
        if (defs[i].parentId != kNoParent) continue;
        const cgltf_node* above = skin->joints[i]->parent;
        if (!above) continue;
        std::array<cgltf_float, 16> w{};
        cgltf_node_transform_world(above, w.data());
        if (!isIdentityMat(mat4FromColumnMajor(w.data()), 1e-5f))
            return Result<ConvertedGltf>::fail(
                "Skeleton root '" + defs[i].name +
                    "' sits under a non-identity transform (nested armatures are "
                    "NOT_SUPPORTED_YET).",
                "FORMAT", asset, "gltf.skeleton");
    }

    // --- inverse bind matrices (stored as authored, bit-faithful) -----------
    const cgltf_accessor* ibm = skin->inverse_bind_matrices;
    if (ibm->type != cgltf_type_mat4 || ibm->component_type != cgltf_component_type_r_32f ||
        ibm->normalized || ibm->is_sparse || ibm->count != jointCount)
        return Result<ConvertedGltf>::fail(
            "inverseBindMatrices must be a non-sparse float MAT4 accessor with one entry per "
            "joint.",
            "FORMAT", asset, "gltf.skin");
    auto ibmRes = accessorRange(ibm, 64, parsed.loadedSizes, asset, "inverseBindMatrices");
    if (!ibmRes) return Result<ConvertedGltf>::fail(ibmRes.error());
    std::vector<Mat4> bindInverse;
    bindInverse.reserve(jointCount);
    for (std::uint64_t j = 0; j < ibmRes.value().count; ++j) {
        const std::uint8_t* e = elementAt(ibmRes.value(), j);
        cgltf_float col[16];
        for (int k = 0; k < 16; ++k) {
            col[k] = readF32le(e + static_cast<std::size_t>(k) * 4U);
            if (!std::isfinite(col[k]))
                return Result<ConvertedGltf>::fail("inverseBindMatrices contains non-finite data.",
                                                   "FORMAT", asset, "gltf.skin");
        }
        bindInverse.push_back(mat4FromColumnMajor(col));
    }

    // --- meshes --------------------------------------------------------------
    ConvertedGltf out;
    out.mesh.name = asset;
    std::vector<std::string> notes;
    notes.push_back("glTF Y-up: no axis conversion (canonical is Y-up).");
    std::unordered_set<const cgltf_mesh*> seenMeshes;
    std::unordered_map<std::string, std::uint32_t> materialIndex;
    // Shared-vertex prims (one POSITION accessor referenced by several
    // primitives, e.g. our smd2gltf multi-material emit): base vertex + the
    // full accessor tuple, so the second prim reuses verts instead of
    // duplicating them. Tuple mismatch falls through to the duplicate path.
    struct SharedPrim {
        std::uint32_t base = 0;
        std::uint64_t count = 0;
        const cgltf_accessor* nrm = nullptr;
        const cgltf_accessor* uv = nullptr;
        const cgltf_accessor* joints = nullptr;
        const cgltf_accessor* weights = nullptr;
    };
    std::unordered_map<const cgltf_accessor*, SharedPrim> sharedByPosition;
    std::size_t primOrdinal = 0;
    std::size_t missingUvPrims = 0;
    std::size_t extraUvSets = 0;
    std::size_t ignoredColorPrims = 0;
    RepairStats repairTotal;
    std::vector<char> missingNormal;  // parallel to out.mesh.vertices
    bool anyFileNormals = false;
    bool anyMissingNormals = false;

    auto materialFor = [&](const cgltf_material* mat, std::size_t fallback,
                           std::vector<std::string>& noteSink) -> Result<std::uint32_t> {
        constexpr const char* kOp = "gltf.mesh";
        std::string matName = mat ? cstr(mat->name) : std::string();
        if (matName.empty())
            matName = mat ? ("material" + std::to_string(fallback)) : "default.dds";
        const auto found = materialIndex.find(matName);
        if (found != materialIndex.end()) return Result<std::uint32_t>::ok(found->second);
        MaterialRef ref;
        ref.name = matName;
        ref.texturePath = matName;
        PbrMaterial pm;
        pm.name = matName;
        pm.baseColor[0] = pm.baseColor[1] = pm.baseColor[2] = 1.0f;
        pm.baseColor[3] = 1.0f;
        pm.metallic = 1.0f;  // glTF spec defaults when pbrMetallicRoughness is absent
        pm.roughness = 1.0f;
        if (mat && mat->has_pbr_metallic_roughness) {
            const auto& pbr = mat->pbr_metallic_roughness;
            for (int k = 0; k < 4; ++k) {
                if (!std::isfinite(pbr.base_color_factor[k]))
                    return Result<std::uint32_t>::fail(
                        "Material '" + matName + "' has non-finite baseColorFactor.",
                        "FORMAT", asset, kOp);
                pm.baseColor[k] = pbr.base_color_factor[k];
            }
            if (!std::isfinite(pbr.metallic_factor) || !std::isfinite(pbr.roughness_factor))
                return Result<std::uint32_t>::fail(
                    "Material '" + matName + "' has non-finite metallic/roughness factors.",
                    "FORMAT", asset, kOp);
            pm.metallic = pbr.metallic_factor;
            pm.roughness = pbr.roughness_factor;
            const cgltf_texture_view& bct = pbr.base_color_texture;
            if (bct.texture && bct.texture->image) {
                const cgltf_image* img = bct.texture->image;
                const std::string imgName = cstr(img->name);
                if (img->buffer_view) {
                    noteSink.push_back("Embedded image '" + imgName +
                                       "' has no external URI (texture path left as '" +
                                       matName + "').");
                } else {
                    const std::string uri = cstr(img->uri);
                    std::string reason;
                    if (!isUriPathSafe(uri, reason)) {
                        noteSink.push_back("Image '" + imgName + "' URI rejected (" + reason +
                                           "); texture path left as '" + matName + "'.");
                    } else {
                        // PNG/JPEG+.bin allowlist: only external PNG/JPEG
                        // images may become texture paths.
                        const std::string lowUri = lowerOf(uri);
                        if (!endsWith(lowUri, ".png") && !endsWith(lowUri, ".jpg") &&
                            !endsWith(lowUri, ".jpeg"))
                            return Result<std::uint32_t>::fail(
                                "Image '" + imgName + "' URI '" + uri +
                                    "' is not a PNG/JPEG file (allowlist: PNG/JPEG only).",
                                "FORMAT", asset, kOp);
                        const std::string mime = lowerOf(cstr(img->mime_type));
                        if (!mime.empty() && mime != "image/png" && mime != "image/jpeg")
                            return Result<std::uint32_t>::fail(
                                "Image '" + imgName + "' has unsupported MIME type '" + mime +
                                    "' (allowlist: image/png, image/jpeg).",
                                "FORMAT", asset, kOp);
                        ref.texturePath = uri;
                        pm.albedoTexture = uri;
                    }
                }
            }
        }
        const std::uint32_t idx = static_cast<std::uint32_t>(out.mesh.materials.size());
        out.mesh.materials.push_back(std::move(ref));
        out.pbrMaterials.push_back(std::move(pm));
        materialIndex[matName] = idx;
        return Result<std::uint32_t>::ok(idx);
    };

    for (std::size_t ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        if (node->skin && node->skin != skin)
            return Result<ConvertedGltf>::fail(
                "Node '" + cstr(node->name) +
                    "' references a different skin (multi-skin assets are NOT_SUPPORTED_YET).",
                "FORMAT", asset, "gltf.skin");
        if (node->has_mesh_gpu_instancing)
            return Result<ConvertedGltf>::fail(
                "Node '" + cstr(node->name) +
                    "' uses GPU instancing (EXT_mesh_gpu_instancing NOT_SUPPORTED_YET).",
                "FORMAT", asset, "gltf.mesh");
        const bool firstInstance = seenMeshes.insert(node->mesh).second;
        if (!firstInstance) {
            notes.push_back("Mesh '" + cstr(node->mesh->name) +
                            "' is instanced more than once (flattened to a single copy).");
            continue;
        }
        ++out.meshCount;
        // Bake the instance world transform into vertices (positions plus
        // inverse-transpose normals); joints and inverse-bind matrices stay
        // exactly as authored, so the bind pose is preserved.
        std::array<cgltf_float, 16> w{};
        cgltf_node_transform_world(node, w.data());
        const Mat4 world = mat4FromColumnMajor(w.data());
        for (int k = 0; k < 16; ++k)
            if (!std::isfinite(w[k]))
                return Result<ConvertedGltf>::fail(
                    "Mesh instance '" + cstr(node->name) + "' has a non-finite transform.",
                    "FORMAT", asset, "gltf.mesh");
        const Mat4 worldInv = world.inverseGeneral();

        const cgltf_mesh* mesh = node->mesh;
        for (std::size_t pi = 0; pi < mesh->primitives_count; ++pi, ++primOrdinal) {
            const cgltf_primitive* prim = &mesh->primitives[pi];
            const std::string primWhat =
                "primitive " + std::to_string(pi) + " of mesh '" + cstr(mesh->name) + "'";
            if (prim->type != cgltf_primitive_type_triangles)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat + " has non-triangle topology (only TRIANGLES).",
                    "FORMAT", asset, "gltf.mesh");
            if (prim->targets_count > 0)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat + " has morph targets (NOT_SUPPORTED_YET).",
                    "FORMAT", asset, "gltf.mesh");
            if (prim->has_draco_mesh_compression)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat +
                        " uses KHR_draco_mesh_compression (NOT_SUPPORTED_YET).",
                    "FORMAT", asset, "gltf.mesh");
            if (!prim->indices)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat +
                        " is non-indexed (indexed triangle primitives ONLY).",
                    "FORMAT", asset, "gltf.mesh");

            const cgltf_accessor* pos = nullptr;
            const cgltf_accessor* nrm = nullptr;
            const cgltf_accessor* uv = nullptr;
            const cgltf_accessor* joints = nullptr;
            const cgltf_accessor* weights = nullptr;
            for (std::size_t ai = 0; ai < prim->attributes_count; ++ai) {
                const cgltf_attribute* attr = &prim->attributes[ai];
                if (!attr->data) continue;
                switch (attr->type) {
                    case cgltf_attribute_type_position: pos = attr->data; break;
                    case cgltf_attribute_type_normal: nrm = attr->data; break;
                    case cgltf_attribute_type_texcoord:
                        if (attr->index == 0) {
                            if (!uv)
                                uv = attr->data;
                            else
                                ++extraUvSets;
                        } else {
                            ++extraUvSets;
                        }
                        break;
                    case cgltf_attribute_type_joints:
                        if (attr->index == 0) {
                            joints = attr->data;
                        } else {
                            return Result<ConvertedGltf>::fail(
                                "glTF " + primWhat +
                                    " uses 8-influence skinning (JOINTS_1; only JOINTS_0/"
                                    "WEIGHTS_0 are supported).",
                                "FORMAT", asset, "gltf.skin");
                        }
                        break;
                    case cgltf_attribute_type_weights:
                        if (attr->index == 0) {
                            weights = attr->data;
                        } else {
                            return Result<ConvertedGltf>::fail(
                                "glTF " + primWhat +
                                    " uses 8-influence skinning (WEIGHTS_1; only JOINTS_0/"
                                    "WEIGHTS_0 are supported).",
                                "FORMAT", asset, "gltf.skin");
                        }
                        break;
                    case cgltf_attribute_type_color: ++ignoredColorPrims; break;
                    default: break;  // tangents/customs: tangents are recomputed below
                }
            }
            if (!pos)
                return Result<ConvertedGltf>::fail("glTF " + primWhat + " lacks POSITION.",
                                                   "FORMAT", asset, "gltf.mesh");
            if (!joints || !weights)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat + " of the skinned mesh lacks JOINTS_0/WEIGHTS_0.",
                    "FORMAT", asset, "gltf.skin");
            if (auto r = requireFloatVec(pos, cgltf_type_vec3, primWhat + " POSITION", asset);
                !r)
                return Result<ConvertedGltf>::fail(r.error());
            if (nrm) {
                if (auto r =
                        requireFloatVec(nrm, cgltf_type_vec3, primWhat + " NORMAL", asset);
                    !r)
                    return Result<ConvertedGltf>::fail(r.error());
                if (nrm->count != pos->count)
                    return Result<ConvertedGltf>::fail(
                        "glTF " + primWhat + " NORMAL count differs from POSITION count.",
                        "FORMAT", asset, "gltf.mesh");
            }
            if (uv) {
                if (auto r =
                        requireFloatVec(uv, cgltf_type_vec2, primWhat + " TEXCOORD_0", asset);
                    !r)
                    return Result<ConvertedGltf>::fail(r.error());
                if (uv->count != pos->count)
                    return Result<ConvertedGltf>::fail(
                        "glTF " + primWhat + " TEXCOORD_0 count differs from POSITION count.",
                        "FORMAT", asset, "gltf.mesh");
            } else {
                ++missingUvPrims;
            }
            if (joints->type != cgltf_type_vec4 ||
                (joints->component_type != cgltf_component_type_r_8u &&
                 joints->component_type != cgltf_component_type_r_16u) ||
                joints->normalized || joints->is_sparse || joints->count == 0)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat +
                        " JOINTS_0 must be a non-sparse, non-normalized UBYTE/USHORT VEC4.",
                    "FORMAT", asset, "gltf.skin");
            if (joints->count != pos->count)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat + " JOINTS_0 count differs from POSITION count.",
                    "FORMAT", asset, "gltf.skin");
            if (auto r =
                    requireFloatVec(weights, cgltf_type_vec4, primWhat + " WEIGHTS_0", asset);
                !r)
                return Result<ConvertedGltf>::fail(r.error());
            if (weights->count != pos->count)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat + " WEIGHTS_0 count differs from POSITION count.",
                    "FORMAT", asset, "gltf.skin");
            const cgltf_accessor* idx = prim->indices;
            if (idx->type != cgltf_type_scalar ||
                (idx->component_type != cgltf_component_type_r_8u &&
                 idx->component_type != cgltf_component_type_r_16u &&
                 idx->component_type != cgltf_component_type_r_32u) ||
                idx->is_sparse || idx->count == 0)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat +
                        " indices must be a non-sparse U8/U16/U32 SCALAR accessor.",
                    "FORMAT", asset, "gltf.mesh");
            if (idx->count % 3 != 0)
                return Result<ConvertedGltf>::fail(
                    "glTF " + primWhat + " index count is not a multiple of 3.", "FORMAT",
                    asset, "gltf.mesh");

            const std::uint64_t vertCount = pos->count;
            std::uint32_t base = 0;
            bool sharedPrim = false;
            if (const auto hit = sharedByPosition.find(pos);
                hit != sharedByPosition.end() && hit->second.count == vertCount &&
                hit->second.nrm == nrm && hit->second.uv == uv &&
                hit->second.joints == joints && hit->second.weights == weights) {
                // Shared vertex block: reuse the already-read verts; indices
                // below remap onto base exactly like the first prim did.
                base = hit->second.base;
                sharedPrim = true;
            } else {
                std::uint64_t baseSum = 0;
                if (!checkedAdd(out.mesh.vertices.size(), vertCount, baseSum) ||
                    baseSum > 0xFFFFFFFFULL)
                    return Result<ConvertedGltf>::fail("glTF " + primWhat + " exceeds 4G vertices.",
                                                       "FORMAT", asset, "gltf.mesh");
                base = static_cast<std::uint32_t>(out.mesh.vertices.size());
            }

            auto posRes = accessorRange(pos, 12, parsed.loadedSizes, asset, primWhat + " POSITION");
            auto nrmRes = nrm ? accessorRange(nrm, 12, parsed.loadedSizes, asset,
                                              primWhat + " NORMAL")
                              : Result<AccessorBytes>::ok(AccessorBytes{});
            auto uvRes = uv ? accessorRange(uv, 8, parsed.loadedSizes, asset,
                                            primWhat + " TEXCOORD_0")
                            : Result<AccessorBytes>::ok(AccessorBytes{});
            const std::uint64_t jointElem =
                (joints->component_type == cgltf_component_type_r_8u) ? 4ULL : 8ULL;
            auto jointsRes = accessorRange(joints, jointElem, parsed.loadedSizes, asset,
                                           primWhat + " JOINTS_0");
            auto weightsRes = accessorRange(weights, 16, parsed.loadedSizes, asset,
                                            primWhat + " WEIGHTS_0");
            std::uint64_t idxElem = 4;
            if (idx->component_type == cgltf_component_type_r_8u)
                idxElem = 1;
            else if (idx->component_type == cgltf_component_type_r_16u)
                idxElem = 2;
            auto idxRes =
                accessorRange(idx, idxElem, parsed.loadedSizes, asset, primWhat + " indices");
            if (!posRes) return Result<ConvertedGltf>::fail(posRes.error());
            if (!nrmRes) return Result<ConvertedGltf>::fail(nrmRes.error());
            if (!uvRes) return Result<ConvertedGltf>::fail(uvRes.error());
            if (!jointsRes) return Result<ConvertedGltf>::fail(jointsRes.error());
            if (!weightsRes) return Result<ConvertedGltf>::fail(weightsRes.error());
            if (!idxRes) return Result<ConvertedGltf>::fail(idxRes.error());

            const bool isU8Joints = (joints->component_type == cgltf_component_type_r_8u);
            // Shared prims skip the vertex loop entirely (zero iterations);
            // the index loop below remaps onto the cached base.
            for (std::uint64_t vi = 0; vi < vertCount && !sharedPrim; ++vi) {
                Vertex v;
                const std::uint8_t* pe = elementAt(posRes.value(), vi);
                v.position = {readF32le(pe), readF32le(pe + 4), readF32le(pe + 8)};
                if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) ||
                    !std::isfinite(v.position.z))
                    return Result<ConvertedGltf>::fail(
                        "glTF " + primWhat + " POSITION contains non-finite data.", "FORMAT",
                        asset, "gltf.mesh");
                if (nrm) {
                    const std::uint8_t* ne = elementAt(nrmRes.value(), vi);
                    v.normal = {readF32le(ne), readF32le(ne + 4), readF32le(ne + 8)};
                    if (!std::isfinite(v.normal.x) || !std::isfinite(v.normal.y) ||
                        !std::isfinite(v.normal.z))
                        return Result<ConvertedGltf>::fail(
                            "glTF " + primWhat + " NORMAL contains non-finite data.",
                            "FORMAT", asset, "gltf.mesh");
                    missingNormal.push_back(0);
                    anyFileNormals = true;
                } else {
                    v.normal = {0, 0, 0};
                    missingNormal.push_back(1);
                    anyMissingNormals = true;
                }
                if (uv) {
                    const std::uint8_t* te = elementAt(uvRes.value(), vi);
                    v.uv0 = {readF32le(te), readF32le(te + 4)};
                    if (!std::isfinite(v.uv0.x) || !std::isfinite(v.uv0.y))
                        return Result<ConvertedGltf>::fail(
                            "glTF " + primWhat + " TEXCOORD_0 contains non-finite data.",
                            "FORMAT", asset, "gltf.mesh");
                }
                const std::uint8_t* je = elementAt(jointsRes.value(), vi);
                const std::uint8_t* we = elementAt(weightsRes.value(), vi);
                for (int k = 0; k < 4; ++k) {
                    std::uint64_t joint = 0;
                    if (isU8Joints) {
                        joint = je[k];
                    } else {
                        joint = static_cast<std::uint64_t>(je[k * 2]) |
                                (static_cast<std::uint64_t>(je[k * 2 + 1]) << 8);
                    }
                    const float wt = readF32le(we + static_cast<std::size_t>(k) * 4U);
                    if (!std::isfinite(wt))
                        return Result<ConvertedGltf>::fail(
                            "glTF " + primWhat + " WEIGHTS_0 contains non-finite data.",
                            "FORMAT", asset, "gltf.skin");
                    if (wt <= 0.0f) continue;
                    if (joint >= jointCount)
                        return Result<ConvertedGltf>::fail(
                            "glTF " + primWhat + " JOINTS_0 references joint " +
                                std::to_string(joint) + " (>= " +
                                std::to_string(jointCount) + " joints).",
                            "FORMAT", asset, "gltf.skin");
                    v.influences.push_back(
                        {static_cast<std::uint32_t>(joint), wt});
                }
                RepairStats rs;
                const std::vector<BoneInfluence> before = v.influences;
                if (!repairVertexInfluences(v.influences, kMetin2MaxInfluences, &rs))
                    return Result<ConvertedGltf>::fail(
                        "glTF " + primWhat + " vertex " + std::to_string(vi) +
                            " has no usable skin weights.",
                        "FORMAT", asset, "gltf.skin");
                // repairVertexInfluences normalizes, so an exact re-read of
                // the repaired list detects whether this vertex changed.
                repairTotal.removedMass += rs.removedMass;
                bool changed = (v.influences.size() != before.size());
                for (std::size_t bi = 0; !changed && bi < v.influences.size(); ++bi)
                    changed = (v.influences[bi].bone != before[bi].bone ||
                               v.influences[bi].weight != before[bi].weight);
                if (changed) ++repairTotal.verticesChanged;
                // Bake the instance world transform (positions +
                // inverse-transpose normals for correctness under
                // non-uniform scale).
                v.position = world.transformPoint(v.position);
                if (nrm) {
                    Vec3 nb;
                    nb.x = v.normal.x * worldInv.m[0][0] + v.normal.y * worldInv.m[1][0] +
                           v.normal.z * worldInv.m[2][0];
                    nb.y = v.normal.x * worldInv.m[0][1] + v.normal.y * worldInv.m[1][1] +
                           v.normal.z * worldInv.m[2][1];
                    nb.z = v.normal.x * worldInv.m[0][2] + v.normal.y * worldInv.m[1][2] +
                           v.normal.z * worldInv.m[2][2];
                    if (length(nb) < 1e-12f)
                        return Result<ConvertedGltf>::fail(
                            "glTF " + primWhat +
                                " mesh-instance transform is degenerate for normals.",
                            "FORMAT", asset, "gltf.mesh");
                    v.normal = normalized(nb);
                }
                out.mesh.vertices.push_back(std::move(v));
            }
            if (!sharedPrim)
                // NOTE: overwrite-on-fallthrough is intentional. An A,B,A
                // prim pattern re-duplicates A (correct output, wasted
                // memory) instead of aliasing B's verts; removedMass on the
                // fallthrough path is not accumulated (benign: our emit is
                // repair-no-op, third-party shared files under-report).
                sharedByPosition[pos] = SharedPrim{base, vertCount, nrm, uv, joints, weights};

            auto matRes = materialFor(prim->material, primOrdinal, notes);
            if (!matRes) return Result<ConvertedGltf>::fail(matRes.error());
            const std::uint32_t matIdx = matRes.value();
            SubMesh sm;
            sm.name = "part" + std::to_string(out.mesh.subMeshes.size());
            sm.materialIndex = matIdx;
            sm.startIndex = out.mesh.indices.size();
            for (std::uint64_t ii = 0; ii < idx->count; ++ii) {
                const std::uint8_t* ie = elementAt(idxRes.value(), ii);
                std::uint64_t idxRaw = 0;
                if (idxElem == 1) {
                    idxRaw = ie[0];
                } else if (idxElem == 2) {
                    std::uint16_t u = 0;
                    std::memcpy(&u, ie, sizeof(u));
                    idxRaw = u;
                } else {
                    std::uint32_t u = 0;
                    std::memcpy(&u, ie, sizeof(u));
                    idxRaw = u;
                }
                if (idxRaw >= vertCount)
                    return Result<ConvertedGltf>::fail(
                        "glTF " + primWhat + " index " + std::to_string(idxRaw) +
                            " is out of range (" + std::to_string(vertCount) + " vertices).",
                        "FORMAT", asset, "gltf.mesh");
                const std::uint64_t biased = static_cast<std::uint64_t>(base) + idxRaw;
                if (biased > 0xFFFFFFFFULL)
                    return Result<ConvertedGltf>::fail(
                        "glTF " + primWhat + " exceeds 4G vertices.", "FORMAT", asset,
                        "gltf.mesh");
                out.mesh.indices.push_back(static_cast<std::uint32_t>(biased));
            }
            sm.indexCount = out.mesh.indices.size() - sm.startIndex;
            if (sm.indexCount > 0) out.mesh.subMeshes.push_back(std::move(sm));
        }
        if (!isIdentityMat(world, 1e-6f))
            notes.push_back("Mesh instance '" + cstr(node->name) +
                            "' world transform baked into vertices.");
    }

    if (out.mesh.vertices.empty() || out.mesh.indices.empty())
        return Result<ConvertedGltf>::fail("glTF has no usable mesh geometry.", "FORMAT",
                                           asset, "gltf.mesh");
    if (data->meshes_count > out.meshCount)
        notes.push_back(std::to_string(data->meshes_count - out.meshCount) +
                        " unreferenced mesh(es) skipped.");
    if (missingUvPrims > 0)
        notes.push_back("TEXCOORD_0 absent on " + std::to_string(missingUvPrims) +
                        " primitive(s) (uv zeroed).");
    if (extraUvSets > 0)
        notes.push_back(std::to_string(extraUvSets) + " extra UV set(s) ignored (uv0 kept).");
    if (ignoredColorPrims > 0)
        notes.push_back("Vertex colors on " + std::to_string(ignoredColorPrims) +
                        " primitive(s) ignored.");

    // File normals are preserved verbatim (no-silent-recompute rule):
    // computeNormals runs only when nothing supplied normals.
    if (!anyFileNormals) {
        computeNormals(out.mesh);
        notes.push_back("NORMAL absent on all primitives (normals computed).");
    } else if (anyMissingNormals) {
        fillMissingNormals(out.mesh, missingNormal);
        notes.push_back("NORMAL absent on some primitives (missing normals computed).");
    }
    computeBounds(out.mesh);
    computeTangents(out.mesh);

    // One-way orient gate (warn-only): Y-up needs no conversion, but an
    // insane result is still reported instead of passing silently.
    const OrientationReport orient = diagnoseOrientation(out.mesh, skeleton);
    notes.push_back("Orient: " + orient.verdictLine());
    for (const auto& f : orient.findings)
        notes.push_back(std::string("Orient warning [") + f.id + "]: " + f.message);
    {
        std::ostringstream mass;
        mass << "Skin repair: " << repairTotal.verticesChanged << " verts changed, dropped mass "
             << repairTotal.removedMass << ".";
        notes.push_back(mass.str());
    }

    // --- animations: single LINEAR translation/rotation clip --------------
    // Each channel keyframe time (seconds) maps to frame = round(time * 30)
    // (half up, kGltfAnimFps). The timeline is the sorted union of those
    // frames; every frame poses ALL bones (bind locals for bones, or
    // position/rotation components, without a key at that frame — no
    // interpolation between keys, missing channels hold bind). Rotation
    // quaternions go through Quat->matrix->eulerXyzFromRotation, the exact
    // inverse of the canonical composer (same path as TRS joints above).
    // Morph/weights/scale targets and non-LINEAR interpolation fail per
    // offending channel, never silently skipped. An empty `animations`
    // array yields no frames (not a failure).
    if (data->animations_count == 1) {
        const cgltf_animation* anim = &data->animations[0];
        const std::string animName = cstr(anim->name);
        const std::string animWhat =
            "animation '" + (animName.empty() ? std::string("anim0") : animName) + "'";
        struct AnimKey {
            int frame = 0;
            std::uint32_t joint = 0;
            bool isRotation = false;
            Vec3 value{0, 0, 0};
        };
        std::vector<AnimKey> keys;
        std::vector<char> hasTranslation(jointCount, 0);
        std::vector<char> hasRotation(jointCount, 0);
        std::size_t keyTotal = 0;
        for (cgltf_size ci = 0; ci < anim->channels_count; ++ci) {
            const cgltf_animation_channel& ch = anim->channels[ci];
            const std::string chWhat = animWhat + " channel " + std::to_string(ci);
            if (!ch.sampler)
                return Result<ConvertedGltf>::fail(chWhat + " has no sampler.", "FORMAT",
                                                   asset, "gltf.anim");
            if (!ch.target_node)
                return Result<ConvertedGltf>::fail(
                    chWhat + " has no target node (only skin-joint targets are supported).",
                    "FORMAT", asset, "gltf.anim");
            const auto jit = jointIndex.find(ch.target_node);
            if (jit == jointIndex.end())
                return Result<ConvertedGltf>::fail(
                    chWhat + " targets node '" + cstr(ch.target_node->name) +
                        "' which is not a skin joint (only skin-joint targets are supported).",
                    "FORMAT", asset, "gltf.anim");
            const std::uint32_t joint = jit->second;
            bool isRotation = false;
            if (ch.target_path == cgltf_animation_path_type_translation) {
                isRotation = false;
            } else if (ch.target_path == cgltf_animation_path_type_rotation) {
                isRotation = true;
            } else if (ch.target_path == cgltf_animation_path_type_scale) {
                return Result<ConvertedGltf>::fail(
                    chWhat + " targets 'scale' (only translation/rotation are supported; "
                             "scale channels are NOT_SUPPORTED_YET).",
                    "FORMAT", asset, "gltf.anim");
            } else if (ch.target_path == cgltf_animation_path_type_weights) {
                return Result<ConvertedGltf>::fail(
                    chWhat + " targets morph-target weights (morph animation is "
                             "NOT_SUPPORTED_YET).",
                    "FORMAT", asset, "gltf.anim");
            } else {
                return Result<ConvertedGltf>::fail(
                    chWhat + " has an unknown target path (only translation/rotation are "
                             "supported).",
                    "FORMAT", asset, "gltf.anim");
            }
            if ((isRotation ? hasRotation[joint] : hasTranslation[joint]) != 0)
                return Result<ConvertedGltf>::fail(
                    chWhat + " duplicates the " +
                        std::string(isRotation ? "rotation" : "translation") + " channel for "
                        "joint '" + defs[joint].name + "' (one channel per joint+path).",
                    "FORMAT", asset, "gltf.anim");
            (isRotation ? hasRotation[joint] : hasTranslation[joint]) = 1;
            const cgltf_animation_sampler* sm = ch.sampler;
            if (sm->interpolation == cgltf_interpolation_type_step)
                return Result<ConvertedGltf>::fail(
                    chWhat + " uses STEP interpolation (only LINEAR is supported).",
                    "FORMAT", asset, "gltf.anim");
            if (sm->interpolation == cgltf_interpolation_type_cubic_spline)
                return Result<ConvertedGltf>::fail(
                    chWhat + " uses CUBICSPLINE interpolation (only LINEAR is supported).",
                    "FORMAT", asset, "gltf.anim");
            if (sm->interpolation != cgltf_interpolation_type_linear)
                return Result<ConvertedGltf>::fail(
                    chWhat + " has an unknown interpolation (only LINEAR is supported).",
                    "FORMAT", asset, "gltf.anim");
            if (!sm->input || !sm->output)
                return Result<ConvertedGltf>::fail(
                    chWhat + " has a null input/output accessor.", "FORMAT", asset,
                    "gltf.anim");
            const cgltf_accessor* inAcc = sm->input;
            const cgltf_accessor* outAcc = sm->output;
            if (inAcc->type != cgltf_type_scalar ||
                inAcc->component_type != cgltf_component_type_r_32f || inAcc->normalized ||
                inAcc->is_sparse || inAcc->count == 0)
                return Result<ConvertedGltf>::fail(
                    chWhat + " input times must be a non-empty, non-sparse, non-normalized "
                             "float SCALAR accessor.",
                    "FORMAT", asset, "gltf.anim");
            const cgltf_type wantType = isRotation ? cgltf_type_vec4 : cgltf_type_vec3;
            if (outAcc->type != wantType ||
                outAcc->component_type != cgltf_component_type_r_32f ||
                outAcc->normalized || outAcc->is_sparse || outAcc->count == 0)
                return Result<ConvertedGltf>::fail(
                    chWhat + (isRotation ? " rotation output must be a non-empty, non-sparse, "
                                           "non-normalized float VEC4 accessor."
                                         : " translation output must be a non-empty, "
                                           "non-sparse, non-normalized float VEC3 accessor."),
                    "FORMAT", asset, "gltf.anim");
            if (outAcc->count != inAcc->count)
                return Result<ConvertedGltf>::fail(
                    chWhat + " input/output key counts differ (" +
                        std::to_string(inAcc->count) + " vs " +
                        std::to_string(outAcc->count) + ").",
                    "FORMAT", asset, "gltf.anim");
            auto inRes =
                accessorRange(inAcc, 4, parsed.loadedSizes, asset, chWhat + " input");
            auto outRes = accessorRange(outAcc, isRotation ? 16ULL : 12ULL,
                                        parsed.loadedSizes, asset, chWhat + " output");
            if (!inRes) return Result<ConvertedGltf>::fail(inRes.error());
            if (!outRes) return Result<ConvertedGltf>::fail(outRes.error());
            double prevTime = 0.0;
            bool havePrev = false;
            for (cgltf_size k = 0; k < inAcc->count; ++k) {
                const float tRaw = readF32le(elementAt(inRes.value(), k));
                if (!std::isfinite(tRaw) || tRaw < 0.0f)
                    return Result<ConvertedGltf>::fail(
                        chWhat + " key " + std::to_string(k) +
                            " has a non-finite or negative time (seconds must be >= 0).",
                        "FORMAT", asset, "gltf.anim");
                const double t = static_cast<double>(tRaw);
                if (havePrev && !(t > prevTime))
                    return Result<ConvertedGltf>::fail(
                        chWhat + " keyframe times must be strictly increasing.", "FORMAT",
                        asset, "gltf.anim");
                havePrev = true;
                prevTime = t;
                const double df = t * kGltfAnimFps;
                if (df > 2147483647.0)
                    return Result<ConvertedGltf>::fail(
                        chWhat + " key " + std::to_string(k) +
                            " maps past INT_MAX frames at 30 fps.", "FORMAT", asset,
                        "gltf.anim");
                const int frame = static_cast<int>(std::floor(df + 0.5));
                const std::uint8_t* e = elementAt(outRes.value(), k);
                if (!isRotation) {
                    const Vec3 v{readF32le(e), readF32le(e + 4), readF32le(e + 8)};
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                        return Result<ConvertedGltf>::fail(
                            chWhat + " key " + std::to_string(k) +
                                " has non-finite translation.", "FORMAT", asset, "gltf.anim");
                    keys.push_back({frame, joint, false, v});
                } else {
                    const float qx = readF32le(e);
                    const float qy = readF32le(e + 4);
                    const float qz = readF32le(e + 8);
                    const float qw = readF32le(e + 12);
                    if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) ||
                        !std::isfinite(qw))
                        return Result<ConvertedGltf>::fail(
                            chWhat + " key " + std::to_string(k) +
                                " has a non-finite rotation quaternion.", "FORMAT", asset,
                            "gltf.anim");
                    const float len =
                        std::sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
                    if (len < 1e-6f)
                        return Result<ConvertedGltf>::fail(
                            chWhat + " key " + std::to_string(k) +
                                " has a zero-length rotation quaternion.", "FORMAT", asset,
                            "gltf.anim");
                    const Quat q{qx / len, qy / len, qz / len, qw / len};
                    keys.push_back({frame, joint, true, q.toMatrix().eulerXyzFromRotation()});
                }
                ++keyTotal;
            }
        }
        // Stable by frame only: sub-frame keys colliding on one frame keep
        // time order, so the latest time wins (documented, deterministic).
        std::stable_sort(keys.begin(), keys.end(),
                         [](const AnimKey& a, const AnimKey& b) { return a.frame < b.frame; });
        std::vector<int> uniqFrames;
        for (const auto& k : keys)
            if (uniqFrames.empty() || uniqFrames.back() != k.frame)
                uniqFrames.push_back(k.frame);
        std::vector<SmdFrame> animFrames;
        animFrames.reserve(uniqFrames.size());
        std::size_t cursor = 0;
        for (const int f : uniqFrames) {
            std::vector<Vec3> ps(jointCount);
            std::vector<Vec3> es(jointCount);
            for (std::size_t j = 0; j < jointCount; ++j) {
                ps[j] = defs[j].localPosition;
                es[j] = defs[j].localRotationEuler;
            }
            while (cursor < keys.size() && keys[cursor].frame == f) {
                if (keys[cursor].isRotation)
                    es[keys[cursor].joint] = keys[cursor].value;
                else
                    ps[keys[cursor].joint] = keys[cursor].value;
                ++cursor;
            }
            SmdFrame sf;
            sf.time = f;
            sf.poses.reserve(jointCount);
            for (std::size_t j = 0; j < jointCount; ++j)
                sf.poses.push_back({static_cast<std::uint32_t>(j), ps[j], es[j]});
            animFrames.push_back(std::move(sf));
        }
        out.frames = std::move(animFrames);
        notes.push_back(
            "Animation '" + (animName.empty() ? std::string("anim0") : animName) + "': " +
            std::to_string(anim->channels_count) + " channel(s), " +
            std::to_string(keyTotal) + " key(s) -> " + std::to_string(out.frames.size()) +
            " frame(s) at 30 fps (frame = round(time*30)); rotation via "
            "Quat->matrix->eulerXyzFromRotation (same as TRS joints); bones without keys "
            "hold bind.");
    }

    out.skeleton = std::move(skeleton);
    out.bindInverse = std::move(bindInverse);
    out.removedMass = repairTotal.removedMass;
    out.repairedVertices = repairTotal.verticesChanged;
    std::ostringstream note;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i > 0) note << "\n";
        note << notes[i];
    }
    out.conversionNote = note.str();
    return Result<ConvertedGltf>::ok(std::move(out));
}

}  // namespace m2rig
