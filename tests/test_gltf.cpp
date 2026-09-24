// glTF 2.0 import tests (Wave 29a) + smd2gltf emit round-trip (Wave 29b):
// a hand-written minimal .glb (cube + 2-bone skin, <=4 weights,
// inverse-bind matrices) is synthesized at runtime, so no checked-in
// fixture is needed and no ctest suite is wired.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../tests/expect.hpp"

#ifdef M2RIG_WITH_CGLTF
#include "m2rig/gltf/gltf_reader.hpp"
#include "m2rig/gltf/gltf_writer.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"

using namespace m2rig;

namespace {

struct CubeGlbOpts {
    bool withIndices = true;
    std::string extensionsRequired;  // raw JSON array, empty = none
    std::string primExtension;       // raw JSON fragment (with leading comma), empty = none
};

void pushU32le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

void pushU16le(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

void pushF32le(std::vector<std::uint8_t>& out, float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    pushU32le(out, u);
}

// Cube corners, per-corner normals, a 2-bone skin (vert 0 rigid on bone 0,
// the rest split 0.75/0.25) and inverse-bind matrices (identity + the
// inverse of joint 1 at (0,2,0)). Offsets are recorded as the single
// source of truth for the JSON below, so the two cannot drift apart.
std::vector<std::uint8_t> buildCubeGlb(const CubeGlbOpts& opts) {
    std::vector<std::uint8_t> bin;
    const float corners[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                 {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    const std::size_t posOff = bin.size();
    for (const auto& c : corners) {
        pushF32le(bin, c[0]);
        pushF32le(bin, c[1]);
        pushF32le(bin, c[2]);
    }
    const std::size_t nrmOff = bin.size();
    for (const auto& c : corners) {
        const float l = std::sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        pushF32le(bin, c[0] / l);
        pushF32le(bin, c[1] / l);
        pushF32le(bin, c[2] / l);
    }
    const std::size_t uvOff = bin.size();
    const float uvs[8][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const auto& t : uvs) {
        pushF32le(bin, t[0]);
        pushF32le(bin, t[1]);
    }
    const std::size_t jointsOff = bin.size();
    for (int i = 0; i < 8; ++i) {
        bin.push_back(0);
        bin.push_back(i == 0 ? 0 : 1);
        bin.push_back(0);
        bin.push_back(0);
    }
    const std::size_t weightsOff = bin.size();
    for (int i = 0; i < 8; ++i) {
        pushF32le(bin, i == 0 ? 1.0f : 0.75f);
        pushF32le(bin, i == 0 ? 0.0f : 0.25f);
        pushF32le(bin, 0.0f);
        pushF32le(bin, 0.0f);
    }
    const std::uint16_t tris[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                                    1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2};
    const std::size_t idxOff = bin.size();
    for (std::uint16_t t : tris) pushU16le(bin, t);
    const std::size_t ibmOff = bin.size();
    const float ibm[2][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                              {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1}};
    for (const auto& m : ibm)
        for (float f : m) pushF32le(bin, f);
    while (bin.size() % 4 != 0) bin.push_back(0);

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"m2rig-test\"},";
    js << "\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],";
    js << "\"nodes\":[{\"name\":\"CubeNode\",\"mesh\":0},";
    js << "{\"name\":\"Bone0\",\"children\":[2],\"translation\":[0.0,0.0,0.0]},";
    js << "{\"name\":\"Bone1\",\"translation\":[0.0,2.0,0.0]}],";
    js << "\"meshes\":[{\"name\":\"Cube\",\"primitives\":[{";
    js << "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,"
          "\"WEIGHTS_0\":4},";
    if (opts.withIndices) js << "\"indices\":5,";
    js << "\"mode\":4,\"material\":0" << opts.primExtension << "}]}],";
    js << "\"skins\":[{\"name\":\"Armature\",\"joints\":[1,2],\"inverseBindMatrices\":6}],";
    js << "\"materials\":[{\"name\":\"cube_mat\",\"pbrMetallicRoughness\":{";
    js << "\"baseColorFactor\":[1.0,0.0,0.0,1.0],\"metallicFactor\":0.0,";
    js << "\"roughnessFactor\":0.5,\"baseColorTexture\":{\"index\":0}}}],";
    js << "\"textures\":[{\"source\":0}],";
    js << "\"images\":[{\"name\":\"cube_image\",\"uri\":\"cube.png\",\"mimeType\":\"image/png\"}],";
    js << "\"accessors\":[";
    js << "{\"bufferView\":0,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"},";
    js << "{\"bufferView\":1,\"componentType\":5126,\"count\":8,\"type\":\"VEC3\"},";
    js << "{\"bufferView\":2,\"componentType\":5126,\"count\":8,\"type\":\"VEC2\"},";
    js << "{\"bufferView\":3,\"componentType\":5121,\"count\":8,\"type\":\"VEC4\"},";
    js << "{\"bufferView\":4,\"componentType\":5126,\"count\":8,\"type\":\"VEC4\"},";
    js << "{\"bufferView\":5,\"componentType\":5123,\"count\":36,\"type\":\"SCALAR\"},";
    js << "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}],";
    js << "\"bufferViews\":[";
    js << "{\"buffer\":0,\"byteOffset\":" << posOff << ",\"byteLength\":" << (nrmOff - posOff)
       << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << nrmOff << ",\"byteLength\":" << (uvOff - nrmOff)
       << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << uvOff << ",\"byteLength\":"
       << (jointsOff - uvOff) << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << jointsOff
       << ",\"byteLength\":" << (weightsOff - jointsOff) << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << weightsOff
       << ",\"byteLength\":" << (idxOff - weightsOff) << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << idxOff << ",\"byteLength\":" << (ibmOff - idxOff)
       << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << ibmOff << ",\"byteLength\":"
       << (bin.size() - ibmOff) << "}],";
    js << "\"buffers\":[{\"byteLength\":" << bin.size() << "}]";
    if (!opts.extensionsRequired.empty()) js << ",\"extensionsRequired\":" << opts.extensionsRequired;
    js << "}";

    const std::string json = js.str();
    const std::size_t jsonPadded = (json.size() + 3) & ~static_cast<std::size_t>(3);
    std::vector<std::uint8_t> glb;
    const std::size_t total = 12 + 8 + jsonPadded + 8 + bin.size();
    pushU32le(glb, 0x46546C67u);
    pushU32le(glb, 2u);
    pushU32le(glb, static_cast<std::uint32_t>(total));
    pushU32le(glb, static_cast<std::uint32_t>(jsonPadded));
    glb.push_back('J');
    glb.push_back('S');
    glb.push_back('O');
    glb.push_back('N');
    glb.insert(glb.end(), json.begin(), json.end());
    while (glb.size() % 4 != 0) glb.push_back(0x20);
    pushU32le(glb, static_cast<std::uint32_t>(bin.size()));
    glb.push_back('B');
    glb.push_back('I');
    glb.push_back('N');
    glb.push_back(0);
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

std::filesystem::path writeTempGlb(const std::vector<std::uint8_t>& bytes, int tag) {
    auto p = std::filesystem::temp_directory_path() /
             ("m2rig_gltf_test_" + std::to_string(tag) + ".glb");
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    f.close();
    return p;
}

}  // namespace

M2RIG_TEST(gltf, roundtrip_cube_skin) {
    int failures = 0;
    CubeGlbOpts opts;
    const auto tmp = writeTempGlb(buildCubeGlb(opts), 1);
    auto conv = readGltfFile(tmp.string(), "cube");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures;
    const ConvertedGltf& g = conv.value();
    CHECK_EQ(g.mesh.vertices.size(), static_cast<std::size_t>(8));
    CHECK_EQ(g.mesh.triangleCount(), static_cast<std::size_t>(12));
    CHECK_EQ(g.skeleton.bones.size(), static_cast<std::size_t>(2));
    CHECK_EQ(g.skeleton.bones[0].name, std::string("Bone0"));
    CHECK_EQ(g.skeleton.bones[1].name, std::string("Bone1"));
    CHECK_EQ(g.skeleton.bones[1].parentId, 0);
    CHECK_NEAR(g.skeleton.bones[1].localPosition.y, 2.0, 1e-5);
    CHECK_NEAR(weightOfBone(g.mesh.vertices[0].influences, 0), 1.0, 1e-6);
    CHECK_NEAR(weightOfBone(g.mesh.vertices[1].influences, 0), 0.75, 1e-5);
    CHECK_NEAR(weightOfBone(g.mesh.vertices[1].influences, 1), 0.25, 1e-5);
    CHECK_EQ(g.bindInverse.size(), static_cast<std::size_t>(2));
    CHECK_NEAR(g.bindInverse[0].m[0][0], 1.0, 1e-6);
    CHECK_NEAR(g.bindInverse[1].m[3][1], -2.0, 1e-5);
    CHECK_EQ(g.mesh.materials.size(), static_cast<std::size_t>(1));
    CHECK_EQ(g.mesh.materials[0].texturePath, std::string("cube.png"));
    CHECK_EQ(g.pbrMaterials.size(), static_cast<std::size_t>(1));
    CHECK_NEAR(g.pbrMaterials[0].baseColor[0], 1.0, 1e-6);
    CHECK_NEAR(g.pbrMaterials[0].baseColor[1], 0.0, 1e-6);
    CHECK_NEAR(g.pbrMaterials[0].metallic, 0.0, 1e-6);
    CHECK_NEAR(g.pbrMaterials[0].roughness, 0.5, 1e-6);
    CHECK_TRUE(g.conversionNote.find("Y-up") != std::string::npos);
    CHECK_NEAR(g.removedMass, 0.0, 1e-9);
    // SMD round-trip preserves counts.
    auto written = writeSmd(g.mesh, g.skeleton, {});
    CHECK_TRUE(written.succeeded());
    if (written.succeeded()) {
        auto back = parseSmd(written.value().text, "cube");
        CHECK_TRUE(back.succeeded());
        if (back.succeeded()) {
            CHECK_EQ(back.value().triangles.size(), static_cast<std::size_t>(12));
            auto asset = smdToAsset(back.value(), "cube");
            CHECK_TRUE(asset.succeeded());
            if (asset.succeeded())
                CHECK_EQ(asset.value().skeleton.bones.size(), static_cast<std::size_t>(2));
        }
    }
    return failures;
}

M2RIG_TEST(gltf, rejects_nonindexed_explicit) {
    int failures = 0;
    CubeGlbOpts opts;
    opts.withIndices = false;
    const auto tmp = writeTempGlb(buildCubeGlb(opts), 2);
    auto conv = readGltfFile(tmp.string(), "cube");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(conv.succeeded());
    if (!conv.succeeded())
        CHECK_TRUE(conv.error().message.find("non-indexed") != std::string::npos);
    return failures;
}

M2RIG_TEST(gltf, rejects_meshopt_draco_explicit) {
    int failures = 0;
    {
        CubeGlbOpts opts;
        opts.extensionsRequired = "[\"KHR_draco_mesh_compression\"]";
        opts.primExtension = ",\"extensions\":{\"KHR_draco_mesh_compression\":{\"bufferView\":5}}";
        const auto tmp = writeTempGlb(buildCubeGlb(opts), 3);
        auto conv = readGltfFile(tmp.string(), "cube");
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        CHECK_FALSE(conv.succeeded());
        if (!conv.succeeded())
            CHECK_TRUE(conv.error().message.find("NOT_SUPPORTED_YET") != std::string::npos);
    }
    {
        CubeGlbOpts opts;
        opts.extensionsRequired = "[\"EXT_meshopt_compression\"]";
        const auto tmp = writeTempGlb(buildCubeGlb(opts), 4);
        auto conv = readGltfFile(tmp.string(), "cube");
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        CHECK_FALSE(conv.succeeded());
        if (!conv.succeeded())
            CHECK_TRUE(conv.error().message.find("NOT_SUPPORTED_YET") != std::string::npos);
    }
    return failures;
}

// Wave 29b: canonical sample armor -> .glb -> readGltfFile back. Pins the
// emit path end to end: triangle/bone counts, per-vertex weights,
// inverse-bind matrices and PBR factors all survive the round-trip.
M2RIG_TEST(gltf, smd2gltf_roundtrip_sample_armor) {
    int failures = 0;
    auto armorRes = makeSampleArmor();
    CHECK_TRUE(armorRes.succeeded());
    if (!armorRes.succeeded()) return failures;
    Mesh mesh = std::move(armorRes.value().mesh);
    Skeleton skel = std::move(armorRes.value().skeleton);
    // Caller-passed bind reference (same snapshot the App/CLI capture).
    std::vector<Mat4> bindInverse;
    bindInverse.reserve(skel.bones.size());
    for (const auto& b : skel.bones) bindInverse.push_back(b.inverseBindTransform);
    std::vector<PbrMaterial> pbrs;
    pbrs.reserve(mesh.materials.size());
    for (std::size_t i = 0; i < mesh.materials.size(); ++i) {
        PbrMaterial pm;
        pm.name = mesh.materials[i].name;
        pm.baseColor[0] = 0.8f;
        pm.baseColor[1] = 0.2f;
        pm.baseColor[2] = 0.1f;
        pm.baseColor[3] = 1.0f;
        pm.metallic = 0.2f;
        pm.roughness = 0.4f;
        if (i == 0) pm.albedoTexture = "emit_test.png";
        pbrs.push_back(std::move(pm));
    }
    const auto tmp =
        std::filesystem::temp_directory_path() / "m2rig_gltf_emit_rt.glb";
    auto wres = writeGltfFile(tmp.string(), mesh, skel, bindInverse, pbrs, "emit-test");
    CHECK_TRUE(wres.succeeded());
    if (!wres.succeeded()) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return failures;
    }
    CHECK_TRUE(wres.value().find("Y-up") != std::string::npos);
    auto conv = readGltfFile(tmp.string(), "emit-test");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures;
    const ConvertedGltf& g = conv.value();
    CHECK_EQ(g.mesh.vertices.size(), mesh.vertices.size());
    CHECK_EQ(g.mesh.triangleCount(), mesh.triangleCount());
    CHECK_EQ(g.skeleton.bones.size(), skel.bones.size());
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        CHECK_EQ(g.skeleton.bones[i].name, skel.bones[i].name);
        CHECK_EQ(g.skeleton.bones[i].parentId, skel.bones[i].parentId);
    }
    // Weights preserved on the first verts (influences are already <=4, so
    // the import repair is a no-op here).
    const std::size_t checkVerts = std::min<std::size_t>(8, mesh.vertices.size());
    for (std::size_t vi = 0; vi < checkVerts; ++vi) {
        CHECK_EQ(g.mesh.vertices[vi].influences.size(),
                 mesh.vertices[vi].influences.size());
        for (std::size_t k = 0; k < mesh.vertices[vi].influences.size(); ++k) {
            CHECK_EQ(g.mesh.vertices[vi].influences[k].bone,
                     mesh.vertices[vi].influences[k].bone);
            CHECK_NEAR(g.mesh.vertices[vi].influences[k].weight,
                       mesh.vertices[vi].influences[k].weight, 1e-4);
        }
    }
    // Bind preserved (verbatim row-major emit inverts the reader mapping).
    CHECK_EQ(g.bindInverse.size(), bindInverse.size());
    for (std::size_t i = 0; i < bindInverse.size(); ++i)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(g.bindInverse[i].m[r][c], bindInverse[i].m[r][c], 1e-4);
    CHECK_EQ(g.mesh.materials.size(), mesh.materials.size());
    CHECK_EQ(g.pbrMaterials.size(), mesh.materials.size());
    CHECK_NEAR(g.pbrMaterials[0].metallic, 0.2, 1e-5);
    CHECK_NEAR(g.pbrMaterials[0].roughness, 0.4, 1e-5);
    CHECK_NEAR(g.pbrMaterials[0].baseColor[0], 0.8, 1e-5);
    CHECK_EQ(g.mesh.materials[0].texturePath, std::string("emit_test.png"));
    CHECK_EQ(g.pbrMaterials[0].albedoTexture, std::string("emit_test.png"));
    return failures;
}

// Wave 29b: the JOINTS_0/WEIGHTS_0 <=4 gate fails explicitly (never a
// silent truncation to VEC4).
M2RIG_TEST(gltf, smd2gltf_rejects_overlimit_explicit) {
    int failures = 0;
    auto armorRes = makeSampleArmor();
    CHECK_TRUE(armorRes.succeeded());
    if (!armorRes.succeeded()) return failures;
    Mesh mesh = std::move(armorRes.value().mesh);
    Skeleton skel = std::move(armorRes.value().skeleton);
    std::vector<Mat4> bindInverse;
    for (const auto& b : skel.bones) bindInverse.push_back(b.inverseBindTransform);
    if (!mesh.vertices.empty() && skel.bones.size() >= 5) {
        mesh.vertices[0].influences.clear();
        for (std::uint32_t b = 0; b < 5; ++b)
            mesh.vertices[0].influences.push_back({b, 0.2f});
    }
    const auto tmp =
        std::filesystem::temp_directory_path() / "m2rig_gltf_emit_overlimit.glb";
    auto wres = writeGltfFile(tmp.string(), mesh, skel, bindInverse, {}, "overlimit");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(wres.succeeded());
    if (!wres.succeeded())
        CHECK_TRUE(wres.error().message.find("4") != std::string::npos);
    return failures;
}

// Wave 29b: `.gltf` + external `.bin` is deferred (single-BIN `.glb` only).
// The extension gate runs before validation, so even empty input pins it.
M2RIG_TEST(gltf, smd2gltf_gltf_layout_deferred) {
    int failures = 0;
    Mesh mesh;
    Skeleton skel;
    auto wres = writeGltfFile("out.gltf", mesh, skel, {}, {}, "deferred");
    CHECK_FALSE(wres.succeeded());
    if (!wres.succeeded())
        CHECK_TRUE(wres.error().message.find("NOT_SUPPORTED_YET") != std::string::npos);
    return failures;
}

#endif  // M2RIG_WITH_CGLTF
