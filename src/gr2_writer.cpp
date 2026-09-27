// Native GR2 writer implementation — inverse of Gr2DeepParser.
// Every layout decision mirrors a parser read path 1:1 (see gr2_writer.hpp
// for the format contract). No third-party emit dependency: the buffer is
// packed by hand with explicit little-endian writers and the CRC32 is a
// standard IEEE (polynomial 0xEDB88320) table implementation.
#include "m2rig/gr2_writer.hpp"

#include <cstring>
#include <fstream>

#include "m2rig/gr2_deep_parser.hpp"  // Gr2SectionType, GR2_METIN2_MAGIC

namespace m2rig {

namespace {

constexpr std::uint32_t kGr2Version = 2u;  // Granny 2.x container version
constexpr std::uint32_t kHeaderSize = 32u;
constexpr std::uint32_t kSectionEntrySize = 16u;
constexpr std::uint32_t kMaxVertices = 65535u;  // uint16 index limit (parser reads u16)

// Standard IEEE CRC32 (polynomial 0xEDB88320), table built once.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// Little-endian byte sink. Every append is explicit (memcpy for floats,
// shifts for integers) so the writer is endian-correct by construction.
struct ByteWriter {
    std::vector<std::uint8_t> buf;

    void u8(std::uint8_t v) { buf.push_back(v); }
    void u16(std::uint16_t v) {
        buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    }
    void u32(std::uint32_t v) {
        buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v) {
        std::uint32_t u = 0;
        std::memcpy(&u, &v, sizeof(u));
        u32(u);
    }
    void vec2(const Vec2& v) {
        f32(v.x);
        f32(v.y);
    }
    void vec3(const Vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    // Row-major m[row][col], 16 floats — the exact inverse of readMatrix.
    void mat4(const Mat4& m) {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) f32(m.m[r][c]);
    }
    // uint32 length + raw bytes (no NUL terminator) — inverse of readString.
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
    }
    void bytes(const std::uint8_t* p, std::size_t n) { buf.insert(buf.end(), p, p + n); }
    void align4() {
        while (buf.size() % 4 != 0) u8(0);
    }
};

bool allFinite(const Mesh& mesh) {
    for (const auto& v : mesh.vertices) {
        if (!isFiniteF(v.position.x) || !isFiniteF(v.position.y) || !isFiniteF(v.position.z) ||
            !isFiniteF(v.normal.x) || !isFiniteF(v.normal.y) || !isFiniteF(v.normal.z) ||
            !isFiniteF(v.uv0.x) || !isFiniteF(v.uv0.y))
            return false;
    }
    return true;
}

// --- Section payloads (each is the exact inverse of its parse function) ----

// Inverse of Gr2DeepParser::parseSkeleton:
//   boneCount(u32) then per bone: name(str), parentIndex(i32),
//   localTransform(mat4), inverseBindTransform(mat4), length(f32).
std::vector<std::uint8_t> buildSkeletonSection(const Skeleton& skeleton) {
    ByteWriter w;
    w.u32(static_cast<std::uint32_t>(skeleton.bones.size()));
    for (const auto& b : skeleton.bones) {
        w.str(b.name);
        w.i32(b.parentId);
        w.mat4(Mat4::compose(b.localPosition, b.localRotationEuler, b.localScale));
        w.mat4(b.inverseBindTransform);
        w.f32(b.length);
    }
    return std::move(w.buf);
}

// Inverse of Gr2DeepParser::parseMesh:
//   meshCount(u32) then per mesh: name(str), materialName(str),
//   vertexCount(u32), triangleCount(u32), vertexFormat(u32),
//   positions(vertexCount*vec3), normals(vertexCount*vec3),
//   uvs(vertexCount*vec2), indices(triangleCount*3*u16).
std::vector<std::uint8_t> buildMeshSection(const Mesh& mesh) {
    ByteWriter w;
    w.u32(1u);  // single mesh: canonical subMeshes share one vertex/index buffer
    w.str(mesh.name);
    w.str(mesh.materials.empty() ? std::string() : mesh.materials[0].name);
    w.u32(static_cast<std::uint32_t>(mesh.vertices.size()));
    w.u32(static_cast<std::uint32_t>(mesh.triangleCount()));
    w.u32(1u);  // vertexFormat tag (parser stores, does not interpret)
    for (const auto& v : mesh.vertices) w.vec3(v.position);
    for (const auto& v : mesh.vertices) w.vec3(v.normal);
    for (const auto& v : mesh.vertices) w.vec2(v.uv0);
    for (const std::uint32_t idx : mesh.indices) w.u16(static_cast<std::uint16_t>(idx));
    return std::move(w.buf);
}

// MeshBinding (type 3): the parser's documented skinning section. Layout:
//   vertexCount(u32) then per vertex: influenceCount(u8),
//   per influence: boneIndex(u16), weight(f32).
// The parser flags the section (hasWeights) without expanding it; the bytes
// are written for external tools and verified structurally on round-trip.
std::vector<std::uint8_t> buildMeshBindingSection(const Mesh& mesh) {
    ByteWriter w;
    w.u32(static_cast<std::uint32_t>(mesh.vertices.size()));
    for (const auto& v : mesh.vertices) {
        w.u8(static_cast<std::uint8_t>(v.influences.size()));
        for (const auto& inf : v.influences) {
            w.u16(static_cast<std::uint16_t>(inf.bone));
            w.f32(inf.weight);
        }
    }
    return std::move(w.buf);
}

// Inverse of Gr2DeepParser::parseMaterial:
//   matCount(u32) then per material: name(str), texturePath(str),
//   diffuse(vec3), specular(vec3), shininess(f32).
// Canonical MaterialRef carries no color/shininess channels, so defaults
// (white diffuse, black specular, 0 shininess) are written explicitly.
std::vector<std::uint8_t> buildMaterialSection(const Mesh& mesh) {
    ByteWriter w;
    w.u32(static_cast<std::uint32_t>(mesh.materials.size()));
    for (const auto& m : mesh.materials) {
        w.str(m.name);
        w.str(m.texturePath);
        w.vec3(Vec3{1.0f, 1.0f, 1.0f});  // diffuse
        w.vec3(Vec3{0.0f, 0.0f, 0.0f});  // specular
        w.f32(0.0f);                     // shininess
    }
    return std::move(w.buf);
}

// Inverse of Gr2DeepParser::parseAnimation:
//   animCount(u32) then per animation: name(str), duration(f32),
//   frameRate(f32), frameCount(u32), then frameCount*boneCount mat4 locals.
// SmdBonePose (pos + euler) composes with scale=1 so the parser's
// eulerXyzFromRotation decomposition is the exact inverse.
std::vector<std::uint8_t> buildAnimationSection(const std::vector<SmdFrame>& animFrames,
                                                const Skeleton& skeleton) {
    ByteWriter w;
    w.u32(1u);  // single animation
    w.str("anim");
    const float frameRate = 30.0f;
    w.f32(animFrames.empty() ? 0.0f : static_cast<float>(animFrames.size()) / frameRate);
    w.f32(frameRate);
    w.u32(static_cast<std::uint32_t>(animFrames.size()));
    for (const auto& frame : animFrames) {
        for (std::size_t b = 0; b < skeleton.bones.size(); ++b) {
            const SmdBonePose* pose = nullptr;
            for (const auto& p : frame.poses) {
                if (p.boneId == b) {
                    pose = &p;
                    break;
                }
            }
            if (pose) {
                w.mat4(Mat4::compose(pose->position, pose->rotation, Vec3{1.0f, 1.0f, 1.0f}));
            } else {
                // Missing pose: fall back to the skeleton bind pose (never
                // identity, which would snap the bone to the origin).
                const Bone& bone = skeleton.bones[b];
                w.mat4(Mat4::compose(bone.localPosition, bone.localRotationEuler, bone.localScale));
            }
        }
    }
    return std::move(w.buf);
}

}  // namespace

Result<std::vector<std::uint8_t>> Gr2Writer::serialize(
    const Mesh& mesh, const Skeleton& skeleton, const std::vector<SmdFrame>& animFrames,
    const Options& options) {
    // Fail-closed validation (never partial output, never silent truncation).
    if (mesh.vertices.empty())
        return Result<std::vector<std::uint8_t>>::fail("Mesh has no vertices.", "GR2_WRITE");
    if (skeleton.bones.empty())
        return Result<std::vector<std::uint8_t>>::fail("Skeleton has no bones.", "GR2_WRITE");
    if (mesh.vertices.size() > kMaxVertices)
        return Result<std::vector<std::uint8_t>>::fail(
            "Mesh has " + std::to_string(mesh.vertices.size()) +
                " vertices; GR2 indices are uint16 (max " + std::to_string(kMaxVertices) + ").",
            "GR2_WRITE");
    if (!allFinite(mesh))
        return Result<std::vector<std::uint8_t>>::fail(
            "Mesh contains NaN/inf position/normal/uv data.", "GR2_WRITE");
    if (mesh.indices.size() % 3 != 0)
        return Result<std::vector<std::uint8_t>>::fail(
            "Index count is not a multiple of 3.", "GR2_WRITE");

    // Build section payloads (order preserved into the section table).
    struct Section {
        Gr2SectionType type;
        std::vector<std::uint8_t> data;
    };
    std::vector<Section> sections;
    if (options.writeSkeleton)
        sections.push_back({Gr2SectionType::Skeleton, buildSkeletonSection(skeleton)});
    if (options.writeMesh)
        sections.push_back({Gr2SectionType::Mesh, buildMeshSection(mesh)});
    if (options.writeMeshBinding)
        sections.push_back({Gr2SectionType::MeshBinding, buildMeshBindingSection(mesh)});
    if (options.writeMaterials)
        sections.push_back({Gr2SectionType::Material, buildMaterialSection(mesh)});
    if (options.writeAnimation)
        sections.push_back({Gr2SectionType::Animation, buildAnimationSection(animFrames, skeleton)});

    // Layout: header | section table | 4-byte-aligned section payloads.
    const std::size_t tableBytes = sections.size() * kSectionEntrySize;
    std::size_t cursor = kHeaderSize + tableBytes;
    cursor = (cursor + 3u) & ~3u;  // align first payload
    std::vector<std::uint32_t> dataOffsets(sections.size());
    for (std::size_t i = 0; i < sections.size(); ++i) {
        dataOffsets[i] = static_cast<std::uint32_t>(cursor);
        cursor += sections[i].data.size();
        cursor = (cursor + 3u) & ~3u;  // align next payload
    }
    const std::uint32_t totalSize = static_cast<std::uint32_t>(cursor);

    std::vector<std::uint8_t> out;
    out.reserve(cursor);
    auto putU32 = [&out](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    };

    // Header (32 bytes). CRC placeholder is patched after the full buffer is
    // assembled (crc32 over the whole container with the field zeroed).
    // The magic is the raw byte sequence 29 DE 6C C0 (big-endian reading of
    // GR2_METIN2_MAGIC); the body stays little-endian (parser read paths).
    out.push_back(static_cast<std::uint8_t>((GR2_METIN2_MAGIC >> 24) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((GR2_METIN2_MAGIC >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((GR2_METIN2_MAGIC >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(GR2_METIN2_MAGIC & 0xFFu));
    putU32(kGr2Version);
    putU32(totalSize);
    putU32(0u);  // crc32 placeholder
    putU32(static_cast<std::uint32_t>(sections.size()));
    putU32(0u);  // rootSectionOffset (parser does not interpret)
    putU32(0u);  // rootSectionSize
    putU32(0u);  // reserved

    // Section table: type, offset, size, dataOffset (16 bytes per entry).
    for (std::size_t i = 0; i < sections.size(); ++i) {
        putU32(static_cast<std::uint32_t>(sections[i].type));
        putU32(dataOffsets[i]);  // offset
        putU32(static_cast<std::uint32_t>(sections[i].data.size()));  // size
        putU32(dataOffsets[i]);  // dataOffset
    }

    // Pad to first payload, then write payloads with inter-section padding.
    out.resize(kHeaderSize + tableBytes, 0);
    for (std::size_t i = 0; i < sections.size(); ++i) {
        out.resize(dataOffsets[i], 0);
        out.insert(out.end(), sections[i].data.begin(), sections[i].data.end());
    }
    out.resize(totalSize, 0);

    // Patch CRC32 (field is zero during the computation above).
    const std::uint32_t crc = crc32(out.data(), out.size());
    out[12] = static_cast<std::uint8_t>(crc & 0xFFu);
    out[13] = static_cast<std::uint8_t>((crc >> 8) & 0xFFu);
    out[14] = static_cast<std::uint8_t>((crc >> 16) & 0xFFu);
    out[15] = static_cast<std::uint8_t>((crc >> 24) & 0xFFu);

    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

ResultVoid Gr2Writer::writeToFile(const std::string& path, const Mesh& mesh,
                                  const Skeleton& skeleton,
                                  const std::vector<SmdFrame>& animFrames, const Options& options) {
    auto buffer = serialize(mesh, skeleton, animFrames, options);
    if (!buffer) return ResultVoid::fail(buffer.error());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return ResultVoid::fail("Cannot open GR2 file for writing: " + path, "IO", path,
                                "gr2.export");
    out.write(reinterpret_cast<const char*>(buffer.value().data()),
              static_cast<std::streamsize>(buffer.value().size()));
    out.close();
    if (!out)
        return ResultVoid::fail("Failed while writing GR2 file: " + path, "IO", path, "gr2.export");
    return ResultVoid::ok();
}

}  // namespace m2rig
