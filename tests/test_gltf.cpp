// glTF 2.0 import tests (Wave 29a) + smd2gltf emit round-trip (Wave 29b) +
// animation import/emit (this wave) + the msm2smd CLI core chain pin:
// a hand-written minimal .glb (cube + 2-bone skin, <=4 weights,
// inverse-bind matrices) is synthesized at runtime, so no checked-in
// fixture is needed for the cube paths; the msm2smd chain prefers the real
// tests/data fixtures with inline fallbacks (same pattern as test_smd.cpp).
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
#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/gltf/gltf_reader.hpp"
#include "m2rig/gltf/gltf_writer.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"
#ifdef M2RIG_WITH_MESHOPT
#ifdef _MSC_VER
// Third-party header: its warnings must not break our /W4 /WX build.
#pragma warning(push, 0)
#endif
#include <meshoptimizer.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif

using namespace m2rig;

namespace {

struct CubeGlbOpts {
    bool withIndices = true;
    std::string extensionsRequired;  // raw JSON array, empty = none
    std::string primExtension;       // raw JSON fragment (with leading comma), empty = none
    // Animation synth (second asset on the same cube core): appends a time
    // block [0, 1/30 s] + rotation block [identity, +0.5 rad about X] with a
    // single sampler on node 2 (Bone1, joint 1) targeting `animPath` with
    // `animInterp` written verbatim, so negative variants (scale / STEP) pin
    // the per-channel explicit-fail contracts.
    bool withAnim = false;
    std::string animPath = "rotation";
    std::string animInterp = "LINEAR";
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
    // Animation blocks (only for the second synth): keyframe times then key
    // rotations. Key 1 is +0.5 rad about X (half-angle 0.25 below).
    std::size_t animTimeOff = 0;
    std::size_t animQuatOff = 0;
    if (opts.withAnim) {
        animTimeOff = bin.size();
        pushF32le(bin, 0.0f);
        pushF32le(bin, 1.0f / 30.0f);
        animQuatOff = bin.size();
        pushF32le(bin, 0.0f);
        pushF32le(bin, 0.0f);
        pushF32le(bin, 0.0f);
        pushF32le(bin, 1.0f);
        pushF32le(bin, std::sinf(0.25f));
        pushF32le(bin, 0.0f);
        pushF32le(bin, 0.0f);
        pushF32le(bin, std::cosf(0.25f));
        while (bin.size() % 4 != 0) bin.push_back(0);
    }
    // End of the static geometry: the animation blocks (if any) follow.
    const std::size_t geomEnd = opts.withAnim ? animTimeOff : bin.size();

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
    js << "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}";
    if (opts.withAnim) {
        js << ",{\"bufferView\":7,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"}";
        js << ",{\"bufferView\":8,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}";
    }
    js << "],";
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
       << (geomEnd - ibmOff) << "}";
    if (opts.withAnim) {
        js << ",{\"buffer\":0,\"byteOffset\":" << animTimeOff << ",\"byteLength\":8}";
        js << ",{\"buffer\":0,\"byteOffset\":" << animQuatOff << ",\"byteLength\":32}";
    }
    js << "],";
    js << "\"buffers\":[{\"byteLength\":" << bin.size() << "}]";
    if (opts.withAnim) {
        js << ",\"animations\":[{\"name\":\"lift\",\"channels\":[{\"sampler\":0,\"target\":{"
              "\"node\":2,\"path\":\""
           << opts.animPath << "\"}}],\"samplers\":[{\"input\":7,\"interpolation\":\""
           << opts.animInterp << "\",\"output\":8}]}]";
    }
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

// SECOND minimal synth: the cube above plus 1 channel on joint 1 (Bone1)
// with 2 LINEAR keys (t=0 identity, t=1/30 s -> +0.5 rad about X). Import
// must yield 2 frames with the posed joint angles; `path`/`interp`
// variants pin the per-channel explicit-fail contracts.
std::vector<std::uint8_t> buildAnimGlb(const std::string& path, const std::string& interp) {
    CubeGlbOpts opts;
    opts.withAnim = true;
    opts.animPath = path;
    opts.animInterp = interp;
    return buildCubeGlb(opts);
}

#ifdef M2RIG_WITH_MESHOPT
// Encoder-synthesized EXT_meshopt_compression fixture (decode decision in
// gltf_reader.hpp: the vendored lib ships the encoder, so the test encodes
// and prod decodes — real coverage, no hand-crafted streams). Cube core
// (same corners/skin/IBM as buildCubeGlb) with POSITION as
// ATTRIBUTES/NONE and indices as TRIANGLES; extensionsUsed +
// extensionsRequired carry EXT_meshopt_compression, pinning the
// required-gate allowance too. The vertex stream uses encoding version 0
// (the extension-compatible stream: default v1 needs meshoptimizer 0.23+
// and is NOT extension-shaped). truncatePos chops the POSITION payload to
// half its encoded length (declared sizes follow) to pin the explicit
// decoder-error path.
struct MeshoptCube {
    std::vector<std::uint8_t> glb;
    std::vector<std::uint8_t> posPlain;  // 8 VEC3 floats, little-endian
    bool ok = false;
};

MeshoptCube buildMeshoptCubeGlb(bool truncatePos) {
    MeshoptCube out;
    const float corners[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                 {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    out.posPlain.resize(8 * 12);
    for (int i = 0; i < 8; ++i)
        std::memcpy(out.posPlain.data() + static_cast<std::size_t>(i) * 12, corners[i],
                    12);
    std::vector<unsigned char> posEnc(meshopt_encodeVertexBufferBound(8, 12));
    const std::size_t posFull = meshopt_encodeVertexBufferLevel(
        posEnc.data(), posEnc.size(), out.posPlain.data(), 8, 12, 0, 0);
    const std::uint16_t tris[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                                    1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2};
    unsigned int idx32[36];
    for (int i = 0; i < 36; ++i) idx32[i] = tris[i];
    std::vector<unsigned char> idxEnc(meshopt_encodeIndexBufferBound(36, 8));
    const std::size_t idxLen =
        meshopt_encodeIndexBuffer(idxEnc.data(), idxEnc.size(), idx32, 36);
    if (posFull == 0 || idxLen == 0 || posFull > posEnc.size() || idxLen > idxEnc.size())
        return out;
    const std::size_t posLen = truncatePos ? (posFull / 2) : posFull;

    std::vector<std::uint8_t> bin;
    bin.insert(bin.end(), posEnc.data(), posEnc.data() + posLen);
    const std::size_t posOff = 0;
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
    const std::size_t idxOff = bin.size();
    bin.insert(bin.end(), idxEnc.data(), idxEnc.data() + idxLen);
    const std::size_t ibmOff = bin.size();
    const float ibm[2][16] = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                              {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1}};
    for (const auto& m : ibm)
        for (float f : m) pushF32le(bin, f);
    while (bin.size() % 4 != 0) bin.push_back(0);

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"m2rig-test-meshopt\"},";
    js << "\"extensionsUsed\":[\"EXT_meshopt_compression\"],";
    js << "\"extensionsRequired\":[\"EXT_meshopt_compression\"],";
    js << "\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],";
    js << "\"nodes\":[{\"name\":\"CubeNode\",\"mesh\":0},";
    js << "{\"name\":\"Bone0\",\"children\":[2],\"translation\":[0.0,0.0,0.0]},";
    js << "{\"name\":\"Bone1\",\"translation\":[0.0,2.0,0.0]}],";
    js << "\"meshes\":[{\"name\":\"Cube\",\"primitives\":[{";
    js << "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,"
          "\"WEIGHTS_0\":4},";
    js << "\"indices\":5,\"mode\":4,\"material\":0}]}],";
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
    js << "{\"buffer\":0,\"byteOffset\":" << posOff << ",\"byteLength\":" << posLen
       << ",\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteOffset\":" << posOff
       << ",\"byteLength\":" << posLen
       << ",\"byteStride\":12,\"count\":8,\"mode\":\"ATTRIBUTES\",\"filter\":\"NONE\"}}},";
    js << "{\"buffer\":0,\"byteOffset\":" << nrmOff << ",\"byteLength\":" << (uvOff - nrmOff)
       << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << uvOff << ",\"byteLength\":"
       << (jointsOff - uvOff) << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << jointsOff
       << ",\"byteLength\":" << (weightsOff - jointsOff) << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << weightsOff
       << ",\"byteLength\":" << (idxOff - weightsOff) << "},";
    js << "{\"buffer\":0,\"byteOffset\":" << idxOff << ",\"byteLength\":" << idxLen
       << ",\"extensions\":{\"EXT_meshopt_compression\":{\"buffer\":0,\"byteOffset\":" << idxOff
       << ",\"byteLength\":" << idxLen
       << ",\"byteStride\":2,\"count\":36,\"mode\":\"TRIANGLES\"}}},";
    js << "{\"buffer\":0,\"byteOffset\":" << ibmOff << ",\"byteLength\":"
       << (bin.size() - ibmOff) << "}],";
    js << "\"buffers\":[{\"byteLength\":" << bin.size() << "}]}";

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
    out.glb = std::move(glb);
    out.ok = true;
    return out;
}
#endif  // M2RIG_WITH_MESHOPT

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const std::streamsize end = f.tellg();
    if (end <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(end));
    if (!f.read(reinterpret_cast<char*>(out.data()), end)) return {};
    return out;
}

std::uint32_t u32leAt(const std::vector<std::uint8_t>& b, std::size_t o) {
    std::uint32_t u = 0;
    std::memcpy(&u, b.data() + o, sizeof(u));
    return u;
}

float f32leAt(const std::vector<std::uint8_t>& b, std::size_t o) {
    float f = 0.0f;
    std::memcpy(&f, b.data() + o, sizeof(f));
    return f;
}

std::size_t countOccurrences(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    std::size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

// Splits emitted .glb bytes into the JSON text and the raw BIN chunk
// (container geometry mirrors the writer: 12-byte header, JSON chunk,
// BIN chunk).
bool splitTestGlb(const std::vector<std::uint8_t>& glb, std::string& jsonOut,
                  std::vector<std::uint8_t>& binOut) {
    if (glb.size() < 20 || u32leAt(glb, 0) != 0x46546C67u) return false;
    const std::size_t jsonLen = u32leAt(glb, 12);
    if (20 + jsonLen > glb.size()) return false;
    jsonOut.assign(reinterpret_cast<const char*>(glb.data() + 20), jsonLen);
    const std::size_t jsonPadded = (jsonLen + 3) & ~static_cast<std::size_t>(3);
    const std::size_t binHead = 20 + jsonPadded;
    if (binHead + 8 > glb.size()) return false;
    const std::size_t binLen = u32leAt(glb, binHead);
    if (binHead + 8 + binLen != glb.size()) return false;
    binOut.assign(glb.begin() +
                      static_cast<std::vector<std::uint8_t>::difference_type>(binHead + 8),
                  glb.end());
    return true;
}

// Collects every `"byteOffset":N` value inside the `"bufferViews"` section
// in order (scoped to that section: index accessors carry byteOffset too).
std::vector<std::uint64_t> viewOffsetsInOrder(const std::string& json) {
    std::vector<std::uint64_t> out;
    const std::size_t start = json.find("\"bufferViews\":[");
    if (start == std::string::npos) return out;
    const std::size_t end = json.find("],\"buffers\"", start);
    if (end == std::string::npos) return out;
    const std::string key = "\"byteOffset\":";
    std::size_t pos = start;
    while ((pos = json.find(key, pos)) != std::string::npos && pos < end) {
        pos += key.size();
        std::size_t numEnd = pos;
        while (numEnd < end && json[numEnd] >= '0' && json[numEnd] <= '9') ++numEnd;
        if (numEnd == pos) break;
        out.push_back(std::stoull(json.substr(pos, numEnd - pos)));
        pos = numEnd;
    }
    return out;
}

// Walks up from the working directory to tests/data/<name> (same pattern
// as test_smd.cpp); empty when unreachable (caller uses inline fallback).
std::filesystem::path findFixture(const std::string& name) {
    std::filesystem::path dir = std::filesystem::current_path();
    for (int level = 0; level < 5; ++level) {
        const std::filesystem::path c = dir / "tests" / "data" / name;
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) return c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    return {};
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

M2RIG_TEST(gltf, rejects_draco_explicit) {
    int failures = 0;
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
    return failures;
}

#ifdef M2RIG_WITH_MESHOPT
// EXT_meshopt_compression decode: the encoder-synthesized cube (POSITION
// ATTRIBUTES/NONE + TRIANGLES indices, extensionsRequired set) imports
// end to end with bit-exact positions.
M2RIG_TEST(gltf, decodes_meshopt_attributes_and_triangles) {
    int failures = 0;
    const MeshoptCube mc = buildMeshoptCubeGlb(false);
    CHECK_TRUE(mc.ok);
    if (!mc.ok) return failures;
    const auto tmp = writeTempGlb(mc.glb, 11);
    auto conv = readGltfFile(tmp.string(), "meshopt-cube");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) {
        printf("    meshopt decode import failed: %s\n", conv.error().message.c_str());
        return failures;
    }
    const ConvertedGltf& g = conv.value();
    CHECK_EQ(g.mesh.vertices.size(), static_cast<std::size_t>(8));
    CHECK_EQ(g.mesh.triangleCount(), static_cast<std::size_t>(12));
    CHECK_EQ(g.skeleton.bones.size(), static_cast<std::size_t>(2));
    CHECK_EQ(g.skeleton.bones[1].parentId, 0);
    for (std::size_t i = 0; i < 8; ++i) {
        float want[3] = {0, 0, 0};
        std::memcpy(want, mc.posPlain.data() + i * 12, 12);
        CHECK_NEAR(g.mesh.vertices[i].position.x, want[0], 1e-6);
        CHECK_NEAR(g.mesh.vertices[i].position.y, want[1], 1e-6);
        CHECK_NEAR(g.mesh.vertices[i].position.z, want[2], 1e-6);
    }
    CHECK_NEAR(weightOfBone(g.mesh.vertices[0].influences, 0), 1.0, 1e-6);
    CHECK_NEAR(weightOfBone(g.mesh.vertices[1].influences, 0), 0.75, 1e-5);
    CHECK_NEAR(weightOfBone(g.mesh.vertices[1].influences, 1), 0.25, 1e-5);
    CHECK_NEAR(g.bindInverse[1].m[3][1], -2.0, 1e-5);
    CHECK_EQ(g.mesh.materials[0].texturePath, std::string("cube.png"));
    CHECK_NEAR(g.pbrMaterials[0].metallic, 0.0, 1e-6);
    CHECK_TRUE(g.conversionNote.find("Y-up") != std::string::npos);
    return failures;
}

// A truncated meshopt payload (declared sizes follow the short stream, so
// caps pass and the codec itself must refuse) fails explicitly — never a
// silent import of partial bytes.
M2RIG_TEST(gltf, rejects_truncated_meshopt_explicit) {
    int failures = 0;
    const MeshoptCube mc = buildMeshoptCubeGlb(true);
    CHECK_TRUE(mc.ok);
    if (!mc.ok) return failures;
    const auto tmp = writeTempGlb(mc.glb, 12);
    auto conv = readGltfFile(tmp.string(), "meshopt-truncated");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(conv.succeeded());
    if (!conv.succeeded())
        CHECK_TRUE(conv.error().message.find("meshopt") != std::string::npos);
    return failures;
}

// extensionsRequired: ["EXT_meshopt_compression"] with NO compressed views
// is accepted when the decoder is present (per-spec allowance).
M2RIG_TEST(gltf, allows_meshopt_extensions_required) {
    int failures = 0;
    CubeGlbOpts opts;
    opts.extensionsRequired = "[\"EXT_meshopt_compression\"]";
    const auto tmp = writeTempGlb(buildCubeGlb(opts), 13);
    auto conv = readGltfFile(tmp.string(), "cube");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_TRUE(conv.succeeded());
    return failures;
}
#else
// Lean-build pin: without M2RIG_WITH_MESHOPT the extension stays an
// explicit NOT_SUPPORTED_YET failure (same gate as Draco).
M2RIG_TEST(gltf, meshopt_explicit_fail_without_decoder) {
    int failures = 0;
    CubeGlbOpts opts;
    opts.extensionsRequired = "[\"EXT_meshopt_compression\"]";
    const auto tmp = writeTempGlb(buildCubeGlb(opts), 4);
    auto conv = readGltfFile(tmp.string(), "cube");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(conv.succeeded());
    if (!conv.succeeded())
        CHECK_TRUE(conv.error().message.find("NOT_SUPPORTED_YET") != std::string::npos);
    return failures;
}
#endif  // M2RIG_WITH_MESHOPT

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

// .gltf + sidecar .bin emit (suffix-dispatched writeGltfFile path, the
// same one the `smd2gltf` CLI verb uses): sample armor -> .gltf -> sidecar
// exists with basename-only buffers[0].uri -> readGltfFile round-trip pins
// counts/weights/bind/PBR. Also pins the writeGltfSeparate `.glb` gate.
M2RIG_TEST(gltf, smd2gltf_separate_roundtrip) {
    int failures = 0;
    auto armorRes = makeSampleArmor();
    CHECK_TRUE(armorRes.succeeded());
    if (!armorRes.succeeded()) return failures;
    Mesh mesh = std::move(armorRes.value().mesh);
    Skeleton skel = std::move(armorRes.value().skeleton);
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
    const auto dir = std::filesystem::temp_directory_path();
    const auto gltfPath = dir / "m2rig_gltf_emit_sep.gltf";
    const auto binPath = dir / "m2rig_gltf_emit_sep.bin";
    // A stale sidecar from an earlier run must not mask a missing write.
    {
        std::error_code ec;
        std::filesystem::remove(gltfPath, ec);
        std::filesystem::remove(binPath, ec);
    }
    auto wres =
        writeGltfFile(gltfPath.string(), mesh, skel, bindInverse, pbrs, "emit-sep");
    CHECK_TRUE(wres.succeeded());
    if (!wres.succeeded()) {
        std::error_code ec;
        std::filesystem::remove(gltfPath, ec);
        std::filesystem::remove(binPath, ec);
        return failures;
    }
    CHECK_TRUE(wres.value().find("sidecar") != std::string::npos);
    {
        std::error_code ec;
        CHECK_TRUE(std::filesystem::exists(binPath, ec));
    }
    {
        const std::vector<std::uint8_t> bytes = readFileBytes(gltfPath);
        CHECK_TRUE(!bytes.empty());
        const std::string json(bytes.begin(), bytes.end());
        CHECK_TRUE(json.find("\"uri\":\"m2rig_gltf_emit_sep.bin\"") != std::string::npos);
    }
    auto conv = readGltfFile(gltfPath.string(), "emit-sep");
    {
        std::error_code ec;
        std::filesystem::remove(gltfPath, ec);
        std::filesystem::remove(binPath, ec);
    }
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
    CHECK_EQ(g.bindInverse.size(), bindInverse.size());
    for (std::size_t i = 0; i < bindInverse.size(); ++i)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                CHECK_NEAR(g.bindInverse[i].m[r][c], bindInverse[i].m[r][c], 1e-4);
    CHECK_EQ(g.mesh.materials.size(), mesh.materials.size());
    CHECK_NEAR(g.pbrMaterials[0].metallic, 0.2, 1e-5);
    CHECK_NEAR(g.pbrMaterials[0].roughness, 0.4, 1e-5);
    CHECK_EQ(g.mesh.materials[0].texturePath, std::string("emit_test.png"));
    // writeGltfSeparate requires a `.gltf` path (extension gate runs before
    // validation, so empty input pins it without touching the mesh).
    {
        Mesh emptyMesh;
        Skeleton emptySkel;
        auto gate = writeGltfSeparate("out.glb", emptyMesh, emptySkel, {}, {}, "gate");
        CHECK_FALSE(gate.succeeded());
        if (!gate.succeeded())
            CHECK_TRUE(gate.error().message.find(".gltf") != std::string::npos);
    }
    return failures;
}

// Animation import: the second synth (2 joints, 1 rotation channel on
// joint 1, 2 LINEAR keys) yields 2 frames; the posed joint carries the key
// angles, everything else holds bind. Frame convention: frame =
// round(time*30), so keys at t=0 and t=1/30 s land on frames 0 and 1.
M2RIG_TEST(gltf, imports_single_rotation_channel_to_frames) {
    int failures = 0;
    const auto tmp = writeTempGlb(buildAnimGlb("rotation", "LINEAR"), 5);
    auto conv = readGltfFile(tmp.string(), "anim");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures;
    const ConvertedGltf& g = conv.value();
    CHECK_EQ(g.frames.size(), static_cast<std::size_t>(2));
    if (g.frames.size() != 2) return failures;
    CHECK_EQ(g.frames[0].time, 0);
    CHECK_EQ(g.frames[1].time, 1);
    for (const auto& f : g.frames) {
        CHECK_EQ(f.poses.size(), static_cast<std::size_t>(2));
        if (f.poses.size() == 2) {
            CHECK_EQ(f.poses[0].boneId, 0u);
            CHECK_EQ(f.poses[1].boneId, 1u);
        }
    }
    // Frame 0 is the bind pose; frame 1 carries +0.5 rad about X on joint 1.
    CHECK_NEAR(g.frames[0].poses[1].rotation.x, 0.0, 1e-5);
    CHECK_NEAR(g.frames[0].poses[1].rotation.y, 0.0, 1e-5);
    CHECK_NEAR(g.frames[0].poses[1].rotation.z, 0.0, 1e-5);
    CHECK_NEAR(g.frames[1].poses[1].rotation.x, 0.5, 1e-4);
    CHECK_NEAR(g.frames[1].poses[1].rotation.y, 0.0, 1e-5);
    CHECK_NEAR(g.frames[1].poses[1].rotation.z, 0.0, 1e-5);
    // Joint 0 untouched on both frames; positions hold bind everywhere.
    CHECK_NEAR(g.frames[1].poses[0].rotation.x,
               g.skeleton.bones[0].localRotationEuler.x, 1e-6);
    CHECK_NEAR(g.frames[1].poses[0].position.x,
               g.skeleton.bones[0].localPosition.x, 1e-6);
    CHECK_NEAR(g.frames[1].poses[1].position.y,
               g.skeleton.bones[1].localPosition.y, 1e-6);
    CHECK_TRUE(g.conversionNote.find("30 fps") != std::string::npos);
    return failures;
}

// A scale channel is an explicit per-channel failure, never a silent skip.
M2RIG_TEST(gltf, rejects_scale_channel_explicit) {
    int failures = 0;
    const auto tmp = writeTempGlb(buildAnimGlb("scale", "LINEAR"), 6);
    auto conv = readGltfFile(tmp.string(), "anim-scale");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(conv.succeeded());
    if (!conv.succeeded())
        CHECK_TRUE(conv.error().message.find("scale") != std::string::npos);
    return failures;
}

// STEP interpolation is an explicit per-channel failure (LINEAR only).
M2RIG_TEST(gltf, rejects_step_interpolation_explicit) {
    int failures = 0;
    const auto tmp = writeTempGlb(buildAnimGlb("rotation", "STEP"), 7);
    auto conv = readGltfFile(tmp.string(), "anim-step");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(conv.succeeded());
    if (!conv.succeeded())
        CHECK_TRUE(conv.error().message.find("STEP") != std::string::npos);
    return failures;
}

// Animation emit (--anim-equivalent: the writeGltfFile overload directly):
// sampler counts plus the first/last raw quaternions match the source
// eulers, and a re-import recovers the clip frames functionally.
M2RIG_TEST(gltf, smd2gltf_emits_animation_samplers) {
    int failures = 0;
    auto armorRes = makeSampleArmor();
    CHECK_TRUE(armorRes.succeeded());
    if (!armorRes.succeeded()) return failures;
    Mesh mesh = std::move(armorRes.value().mesh);
    Skeleton skel = std::move(armorRes.value().skeleton);
    const std::size_t nJ = skel.bones.size();
    CHECK_TRUE(nJ > 0);
    if (nJ == 0) return failures;
    std::vector<Mat4> bindInverse;
    for (const auto& b : skel.bones) bindInverse.push_back(b.inverseBindTransform);
    // 2-frame clip: frame 0 = bind, frame 1 = every joint +0.5 rad X / +1 Y.
    std::vector<SmdFrame> clip;
    for (int f = 0; f < 2; ++f) {
        SmdFrame sf;
        sf.time = f;
        for (std::size_t j = 0; j < nJ; ++j) {
            Vec3 p = skel.bones[j].localPosition;
            Vec3 e = skel.bones[j].localRotationEuler;
            if (f == 1) {
                e.x += 0.5f;
                p.y += 1.0f;
            }
            sf.poses.push_back({static_cast<std::uint32_t>(j), p, e});
        }
        clip.push_back(std::move(sf));
    }
    const auto tmp = std::filesystem::temp_directory_path() / "m2rig_gltf_emit_anim.glb";
    auto wres =
        writeGltfFile(tmp.string(), mesh, skel, bindInverse, {}, clip, "anim-emit");
    CHECK_TRUE(wres.succeeded());
    if (!wres.succeeded()) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return failures;
    }
    CHECK_TRUE(wres.value().find("30 fps") != std::string::npos);
    const std::vector<std::uint8_t> bytes = readFileBytes(tmp);
    CHECK_TRUE(!bytes.empty());
    std::string json;
    std::vector<std::uint8_t> bin;
    CHECK_TRUE(splitTestGlb(bytes, json, bin));
    // One animation, 2 samplers (translation + rotation) per joint.
    CHECK_EQ(countOccurrences(json, "\"interpolation\":\"LINEAR\""), 2 * nJ);
    CHECK_EQ(countOccurrences(json, "\"path\":\"translation\""), nJ);
    CHECK_EQ(countOccurrences(json, "\"path\":\"rotation\""), nJ);
    // Raw quaternions: bufferViews order is [pos, nrm, uv, joints, weights,
    // idx, ibm, time, T0, R0, T1, R1, ...], so R_j sits at views[9+2j].
    const std::vector<std::uint64_t> views = viewOffsetsInOrder(json);
    CHECK_EQ(views.size(), 7 + 1 + 2 * nJ);
    if (views.size() == 7 + 1 + 2 * nJ) {
        for (std::size_t j = 0; j < nJ; ++j) {
            const std::size_t rOff = static_cast<std::size_t>(views[9 + 2 * j]);
            const Quat want0 =
                Quat::fromEulerXyz(skel.bones[j].localRotationEuler).normalized();
            Vec3 last = skel.bones[j].localRotationEuler;
            last.x += 0.5f;
            const Quat want1 = Quat::fromEulerXyz(last).normalized();
            CHECK_TRUE(rOff + 32 <= bin.size());
            if (rOff + 32 > bin.size()) continue;
            CHECK_NEAR(f32leAt(bin, rOff), want0.x, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 4), want0.y, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 8), want0.z, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 12), want0.w, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 16), want1.x, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 20), want1.y, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 24), want1.z, 1e-5);
            CHECK_NEAR(f32leAt(bin, rOff + 28), want1.w, 1e-5);
        }
    }
    // Functional round-trip: the reader recovers the clip frames.
    auto conv = readGltfFile(tmp.string(), "anim-emit");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_TRUE(conv.succeeded());
    if (conv.succeeded()) {
        CHECK_EQ(conv.value().frames.size(), static_cast<std::size_t>(2));
        if (conv.value().frames.size() == 2) {
            for (std::size_t j = 0; j < nJ; ++j) {
                CHECK_NEAR(conv.value().frames[1].poses[j].rotation.x,
                           skel.bones[j].localRotationEuler.x + 0.5f, 1e-4);
                CHECK_NEAR(conv.value().frames[1].poses[j].position.y,
                           skel.bones[j].localPosition.y + 1.0f, 1e-4);
            }
        }
    }
    return failures;
}

// The emit overload fails explicitly on incompatible bone counts (frames
// must pose every joint; never silently remapped — same gate the
// `smd2gltf --anim` CLI verb applies before calling).
M2RIG_TEST(gltf, smd2gltf_rejects_mismatched_anim_bones) {
    int failures = 0;
    auto armorRes = makeSampleArmor();
    CHECK_TRUE(armorRes.succeeded());
    if (!armorRes.succeeded()) return failures;
    Mesh mesh = std::move(armorRes.value().mesh);
    Skeleton skel = std::move(armorRes.value().skeleton);
    if (skel.bones.size() < 2) return failures;
    std::vector<Mat4> bindInverse;
    for (const auto& b : skel.bones) bindInverse.push_back(b.inverseBindTransform);
    SmdFrame short_;
    short_.time = 0;
    short_.poses.push_back({0u, skel.bones[0].localPosition,
                            skel.bones[0].localRotationEuler});
    const auto tmp =
        std::filesystem::temp_directory_path() / "m2rig_gltf_emit_anim_bad.glb";
    auto wres = writeGltfFile(tmp.string(), mesh, skel, bindInverse, {}, {short_}, "bad");
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    CHECK_FALSE(wres.succeeded());
    if (!wres.succeeded())
        CHECK_TRUE(wres.error().message.find("poses") != std::string::npos);
    return failures;
}

// msm2smd CLI core chain pin (fixture-based, honest): the `msm2smd` verb
// wraps readMsmFile path IO + dispatch + report printing around this exact
// parseMsm -> bind -> msmToSmd -> smdToAsset -> writeSmd -> re-parse chain.
// sample.msm bone lines (Bone 0 "Bip01", Bone 1 "Bip01 Spine") are read and
// matched against the two_bone.smd bind skeleton (dense ids 0/1) before
// lowering, so an incompatible fixture fails loudly instead of lowering
// garbage. Real files are preferred; inline copies (same content as
// tests/data + test_smd.cpp) are the fallback. A dedicated `cli-msm2smd`
// ctest entry needs one CMakeLists line, which is outside this task's file
// scope — this runs under the existing m2rig_tests suite instead.
M2RIG_TEST(gltf, msm2smd_shell_chain_fixture) {
    int failures = 0;
    // Inline copy of tests/data/sample.msm (same 2-space indent dialect).
    const char* kMsmFallback =
        "Group ShapeDataSample\n"
        "{\n"
        "  Group ShapeIndex\n"
        "  {\n"
        "    ShapeCount 1\n"
        "    Group Shape0\n"
        "    {\n"
        "      Model \"armor_body.dds\"\n"
        "      SourceSkin \"sample_skin\"\n"
        "    }\n"
        "  }\n"
        "  Group Model\n"
        "  {\n"
        "    Bone 0 \"Bip01\" -1\n"
        "    Bone 1 \"Bip01 Spine\" 0\n"
        "  }\n"
        "  Group SourceSkin\n"
        "  {\n"
        "    VertexCount 1\n"
        "    Vertex 0 0 0.7 1 0.3\n"
        "  }\n"
        "}\n";
    // Inline copy of tests/data/two_bone.smd (bind source).
    const char* kBindFallback =
        "version 1\n"
        "\n"
        "nodes\n"
        "  0 \"Bip01\" -1\n"
        "  1 \"Bip01 Spine\" 0\n"
        "end\n"
        "\n"
        "skeleton\n"
        "time 0\n"
        "  0 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\n"
        "  1 0.000000 1.000000 0.000000 0.000000 0.000000 0.000000\n"
        "time 1\n"
        "  0 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000\n"
        "  1 0.000000 1.000000 0.000000 0.100000 0.000000 0.000000\n"
        "end\n"
        "\n"
        "triangles\n"
        "armor_body.dds\n"
        "0 0.000000 0.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 2 0 "
        "0.700000 1 0.300000\n"
        "0 1.000000 0.000000 0.000000 0.000000 0.000000 1.000000 1.000000 0.000000 1 0 "
        "1.000000\n"
        "1 0.000000 1.000000 0.000000 0.000000 1.000000 0.000000 0.500000 0.500000 0\n"
        "armor_trim.dds\n"
        "0 0.000000 0.000000 1.000000 0.000000 0.000000 1.000000 0.000000 1.000000 1 1 "
        "1.000000\n"
        "0 1.000000 0.000000 1.000000 0.000000 0.000000 1.000000 1.000000 1.000000 2 0 "
        "0.500000 1 0.500000\n"
        "1 0.000000 1.000000 1.000000 0.000000 1.000000 0.000000 0.500000 0.000000 1 1 "
        "1.000000\n"
        "end\n";
    std::string msmText = kMsmFallback;
    {
        const auto p = findFixture("sample.msm");
        if (!p.empty()) {
            auto t = readTextFile(p.string(), "sample.msm");
            if (t.succeeded()) msmText = t.value();
        }
    }
    std::string bindText = kBindFallback;
    {
        const auto p = findFixture("two_bone.smd");
        if (!p.empty()) {
            auto t = readTextFile(p.string(), "two_bone.smd");
            if (t.succeeded()) bindText = t.value();
        }
    }
    MsmDocument doc;
    CHECK_TRUE(parseMsm(msmText, doc));
    // No Model-children-count coupling here: the MSM "Model" group shape is
    // not a bone list (real sample.msm carries a different child count
    // than 2), and ref resolution inside msmToSmd is the real gate — it is
    // pinned by the M-track shell test and by the lower below, which fails
    // explicitly on out-of-range refs.
    auto bindParsed = parseSmd(bindText, "bind");
    CHECK_TRUE(bindParsed.succeeded());
    if (!bindParsed.succeeded()) return failures + 1;
    auto bindConv = smdToAsset(bindParsed.value(), "bind");
    CHECK_TRUE(bindConv.succeeded());
    if (!bindConv.succeeded()) return failures + 1;
    const Skeleton& bindSkel = bindConv.value().skeleton;
    CHECK_EQ(bindSkel.bones.size(), static_cast<std::size_t>(2));
    auto lowered = msmToSmd(doc, bindSkel, "sample");
    CHECK_TRUE(lowered.succeeded());
    if (!lowered.succeeded()) {
        printf("    msmToSmd failed: %s\n", lowered.error().message.c_str());
        return failures + 1;
    }
    const SmdDocument& shell = lowered.value();
    // Shell contract: caller skeleton/binds + materials, zero triangles.
    CHECK_TRUE(shell.triangles.empty());
    CHECK_EQ(shell.bones.size(), bindSkel.bones.size());
    CHECK_EQ(shell.materials.size(), static_cast<std::size_t>(1));
    if (!shell.materials.empty())
        CHECK_TRUE(shell.materials[0] == "armor_body.dds");
    for (std::size_t i = 0; i < shell.bones.size() && i < bindSkel.bones.size(); ++i) {
        CHECK_TRUE(shell.bones[i].name == bindSkel.bones[i].name);
        CHECK_NEAR(shell.bones[i].bindPosition.y, bindSkel.bones[i].localPosition.y, 1e-6);
    }
    // Shell re-parses strictly through the canonical chain (CLI verb path).
    auto back = smdToAsset(shell, "sample");
    CHECK_TRUE(back.succeeded());
    if (!back.succeeded()) return failures + 1;
    auto written = writeSmd(back.value().mesh, back.value().skeleton, back.value().frames);
    CHECK_TRUE(written.succeeded());
    if (!written.succeeded()) return failures + 1;
    auto re = parseSmd(written.value().text, "sample");
    CHECK_TRUE(re.succeeded());
    if (re.succeeded()) {
        CHECK_EQ(re.value().bones.size(), static_cast<std::size_t>(2));
        CHECK_TRUE(re.value().triangles.empty());
    }
    return failures;
}

#endif  // M2RIG_WITH_CGLTF
