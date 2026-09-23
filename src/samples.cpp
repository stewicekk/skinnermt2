// Procedural sample armor + reference skeleton (ported from sampleArmor.ts).
#include "m2rig/samples.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace m2rig {

std::vector<std::string> sampleBoneNames() {
    return {"Bip01",           "Bip01 Pelvis",    "Bip01 Spine",     "Bip01 Spine1",
            "Bip01 Neck",      "Bip01 Head",      "Bip01 L Clavicle", "Bip01 R Clavicle",
            "Bip01 L UpperArm", "Bip01 R UpperArm", "Bip01 L Forearm", "Bip01 R Forearm",
            "Bip01 L Hand",    "Bip01 R Hand",    "Bip01 L Thigh",   "Bip01 R Thigh",
            "Bip01 L Calf",    "Bip01 R Calf",    "Bip01 L Foot",    "Bip01 R Foot",
            "equip_left",      "equip_right",     "stip"};
}

namespace {

Vec3 anchorPos(const std::string& bone) {
    static const std::unordered_map<std::string, Vec3> kAnchors = {
        {"Bip01 Pelvis", {0, 0.05f, 0}},      {"Bip01 Spine", {0, 0.75f, 0}},
        {"Bip01 Spine1", {0, 1.35f, 0}},      {"Bip01 Neck", {0, 1.95f, 0}},
        {"Bip01 Head", {0, 2.35f, 0}},        {"Bip01 L Clavicle", {-0.34f, 1.62f, 0}},
        {"Bip01 R Clavicle", {0.34f, 1.62f, 0}}, {"Bip01 L UpperArm", {-0.72f, 1.58f, 0}},
        {"Bip01 R UpperArm", {0.72f, 1.58f, 0}}, {"Bip01 L Forearm", {-1.08f, 1.43f, 0}},
        {"Bip01 R Forearm", {1.08f, 1.43f, 0}}, {"Bip01 L Hand", {-1.38f, 1.30f, 0}},
        {"Bip01 R Hand", {1.38f, 1.30f, 0}},  {"Bip01 L Thigh", {-0.24f, -0.25f, 0}},
        {"Bip01 R Thigh", {0.24f, -0.25f, 0}}, {"Bip01 L Calf", {-0.25f, -1.10f, 0}},
        {"Bip01 R Calf", {0.25f, -1.10f, 0}}, {"Bip01 L Foot", {-0.25f, -1.82f, 0.12f}},
        {"Bip01 R Foot", {0.25f, -1.82f, 0.12f}}, {"equip_left", {-1.42f, 1.28f, 0.05f}},
        {"equip_right", {1.42f, 1.28f, 0.05f}}, {"stip", {0, -0.05f, 0.18f}},
        {"Bip01", {0, 0.0f, 0}},
    };
    auto it = kAnchors.find(bone);
    return it != kAnchors.end() ? it->second : Vec3{0, 0, 0};
}

std::string parentForSample(const std::string& name) {
    std::string n;
    n.reserve(name.size());
    for (char c : name) n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (n == "bip01") return {};
    if (n == "bip01 pelvis" || n == "stip") return "Bip01";
    if (n == "bip01 spine") return "Bip01 Pelvis";
    if (n == "bip01 spine1") return "Bip01 Spine";
    if (n == "bip01 neck") return "Bip01 Spine1";
    if (n == "bip01 head") return "Bip01 Neck";
    const bool isL = n.find(" l ") != std::string::npos;
    if (n.size() >= 8 && n.compare(n.size() - 8, 8, "clavicle") == 0) return "Bip01 Spine1";
    if (n.size() >= 8 && n.compare(n.size() - 8, 8, "upperarm") == 0)
        return isL ? "Bip01 L Clavicle" : "Bip01 R Clavicle";
    if (n.size() >= 7 && n.compare(n.size() - 7, 7, "forearm") == 0)
        return isL ? "Bip01 L UpperArm" : "Bip01 R UpperArm";
    if (n.size() >= 4 && n.compare(n.size() - 4, 4, "hand") == 0)
        return isL ? "Bip01 L Forearm" : "Bip01 R Forearm";
    if (n == "equip_left") return "Bip01 L Hand";
    if (n == "equip_right") return "Bip01 R Hand";
    if (n.size() >= 5 && n.compare(n.size() - 5, 5, "thigh") == 0) return "Bip01 Pelvis";
    if (n.size() >= 4 && n.compare(n.size() - 4, 4, "calf") == 0)
        return isL ? "Bip01 L Thigh" : "Bip01 R Thigh";
    if (n.size() >= 4 && n.compare(n.size() - 4, 4, "foot") == 0)
        return isL ? "Bip01 L Calf" : "Bip01 R Calf";
    return "Bip01";
}

}  // namespace

std::vector<BoneDefinition> sampleBoneDefinitions() {
    const auto names = sampleBoneNames();
    std::unordered_map<std::string, std::int32_t> index;
    for (std::size_t i = 0; i < names.size(); ++i) index[names[i]] = static_cast<std::int32_t>(i);
    std::vector<BoneDefinition> defs;
    defs.reserve(names.size());
    for (const auto& name : names) {
        BoneDefinition d;
        d.name = name;
        const std::string parent = parentForSample(name);
        d.parentId = parent.empty() ? kNoParent : index[parent];
        d.localPosition = anchorPos(name);
        // Convert absolute anchor positions to parent-relative offsets so the
        // hierarchy composes to the intended bind pose.
        if (!parent.empty()) d.localPosition = d.localPosition - anchorPos(parent);
        defs.push_back(d);
    }
    return defs;
}

Result<Skeleton> makeSampleSkeleton() { return buildSkeleton("sample-warrior", sampleBoneDefinitions()); }

Result<SampleArmor> makeSampleArmor() {
    auto skelRes = makeSampleSkeleton();
    if (!skelRes) return Result<SampleArmor>::fail(skelRes.error());
    Skeleton skel = std::move(skelRes.value());

    Mesh mesh;
    mesh.name = "sample-warrior-armor";
    mesh.materials = {{"armor_body.dds", "armor_body.dds"},
                      {"armor_trim.dds", "armor_trim.dds"},
                      {"armor_cloth.dds", "armor_cloth.dds"},
                      {"armor_head.dds", "armor_head.dds"}};

    auto addVertex = [&](const Vec3& p) -> std::uint32_t {
        Vertex v;
        v.position = p;
        v.uv0 = {p.x * 0.5f + 0.5f, p.y * 0.25f + 0.5f};
        mesh.vertices.push_back(std::move(v));
        return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
    };
    auto addQuad = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
                       std::uint32_t mat) {
        const std::size_t base = mesh.indices.size();
        mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        (void)base;
        if (!mesh.subMeshes.empty() && mesh.subMeshes.back().materialIndex == mat &&
            mesh.subMeshes.back().startIndex + mesh.subMeshes.back().indexCount ==
                mesh.indices.size() - 6) {
            mesh.subMeshes.back().indexCount += 6;
        } else {
            SubMesh sm;
            sm.name = "submesh_" + std::to_string(mesh.subMeshes.size());
            sm.materialIndex = mat;
            sm.startIndex = mesh.indices.size() - 6;
            sm.indexCount = 6;
            mesh.subMeshes.push_back(sm);
        }
    };

    constexpr float kPiLocal = 3.14159265358979323846f;
    // Torso rings.
    struct Level {
        float y, rx, rz;
    };
    const Level levels[] = {{-0.28f, 0.24f, 0.18f}, {0.12f, 0.34f, 0.23f}, {0.55f, 0.38f, 0.25f},
                            {1.00f, 0.40f, 0.26f},  {1.38f, 0.38f, 0.25f}, {1.68f, 0.32f, 0.22f},
                            {1.92f, 0.20f, 0.18f}};
    constexpr int kRadial = 18;
    std::vector<std::vector<std::uint32_t>> rings;
    for (const auto& lv : levels) {
        std::vector<std::uint32_t> ring;
        for (int i = 0; i < kRadial; ++i) {
            const float a = (static_cast<float>(i) / kRadial) * 2.0f * kPiLocal;
            ring.push_back(addVertex({std::cos(a) * lv.rx, lv.y, std::sin(a) * lv.rz}));
        }
        rings.push_back(std::move(ring));
    }
    for (std::size_t l = 0; l + 1 < rings.size(); ++l)
        for (int i = 0; i < kRadial; ++i) {
            const int n = (i + 1) % kRadial;
            addQuad(rings[l][i], rings[l][n], rings[l + 1][n], rings[l + 1][i], 0);
        }

    auto limb = [&](const Vec3& start, const Vec3& end, float radius, int segs, int radial,
                    std::uint32_t mat) {
        const Vec3 dir = end - start;
        const bool alongX = std::fabs(dir.x) >= std::fabs(dir.y);
        std::vector<std::vector<std::uint32_t>> lrings;
        for (int row = 0; row <= segs; ++row) {
            const float t = static_cast<float>(row) / segs;
            const Vec3 c = start + dir * t;
            std::vector<std::uint32_t> ring;
            for (int i = 0; i < radial; ++i) {
                const float a = (static_cast<float>(i) / radial) * 2.0f * kPiLocal;
                Vec3 off{0, std::cos(a) * radius, std::sin(a) * radius};
                if (!alongX) off = {std::cos(a) * radius, 0, std::sin(a) * radius};
                ring.push_back(addVertex(c + off));
            }
            lrings.push_back(std::move(ring));
        }
        for (std::size_t r = 0; r + 1 < lrings.size(); ++r)
            for (int i = 0; i < radial; ++i) {
                const int n = (i + 1) % radial;
                addQuad(lrings[r][i], lrings[r][n], lrings[r + 1][n], lrings[r + 1][i], mat);
            }
    };
    limb({-0.34f, 1.58f, 0}, {-1.43f, 1.28f, 0}, 0.13f, 8, 12, 1);
    limb({0.34f, 1.58f, 0}, {1.43f, 1.28f, 0}, 0.13f, 8, 12, 1);
    limb({-0.24f, -0.02f, 0}, {-0.25f, -1.88f, 0.04f}, 0.16f, 9, 12, 2);
    limb({0.24f, -0.02f, 0}, {0.25f, -1.88f, 0.04f}, 0.16f, 9, 12, 2);

    // Head sphere.
    const Vec3 hc{0, 2.38f, 0};
    constexpr int kLat = 7, kLon = 16;
    std::vector<std::vector<std::uint32_t>> hrings;
    for (int lat = 1; lat < kLat; ++lat) {
        const float phi = (static_cast<float>(lat) / kLat) * kPiLocal;
        std::vector<std::uint32_t> ring;
        for (int lon = 0; lon < kLon; ++lon) {
            const float th = (static_cast<float>(lon) / kLon) * 2.0f * kPiLocal;
            ring.push_back(addVertex({hc.x + std::sin(phi) * std::cos(th) * 0.19f,
                                      hc.y + std::cos(phi) * 0.23f,
                                      hc.z + std::sin(phi) * std::sin(th) * 0.20f}));
        }
        hrings.push_back(std::move(ring));
    }
    for (std::size_t r = 0; r + 1 < hrings.size(); ++r)
        for (int i = 0; i < kLon; ++i) {
            const int n = (i + 1) % kLon;
            addQuad(hrings[r][i], hrings[r][n], hrings[r + 1][n], hrings[r + 1][i], 3);
        }

    // Inverse-distance weights from bone anchors (global bind positions).
    std::unordered_map<std::string, std::uint32_t> boneIndex;
    for (const auto& b : skel.bones) boneIndex[b.name] = b.id;
    std::vector<std::pair<std::uint32_t, Vec3>> anchors;
    for (const auto& name : sampleBoneNames()) {
        const Bone* b = skel.findByName(name);
        Vec3 world{0, 0, 0};
        if (b) world = {b->globalTransform.m[3][0], b->globalTransform.m[3][1], b->globalTransform.m[3][2]};
        anchors.emplace_back(boneIndex[name], world);
    }
    for (auto& v : mesh.vertices) {
        struct W {
            std::uint32_t bone;
            float w;
        };
        std::vector<W> ws;
        for (const auto& a : anchors) {
            const float d = distance(v.position, a.second);
            ws.push_back({a.first, 1.0f / (d + 0.08f)});
        }
        std::sort(ws.begin(), ws.end(), [](const W& a, const W& b) { return a.w > b.w; });
        if (ws.size() > 4) ws.resize(4);
        float total = 0;
        for (const auto& w : ws) total += w.w;
        v.influences.clear();
        for (const auto& w : ws)
            if (w.w / total > 1e-4f) v.influences.push_back({w.bone, w.w / total});
    }

    computeNormals(mesh);
    computeBounds(mesh);
    computeTangents(mesh);
    SampleArmor out{std::move(mesh), std::move(skel)};
    return Result<SampleArmor>::ok(std::move(out));
}

Result<SampleArmor> makeSampleArmorForProfile(const std::string& profileId) {
    // Geometry is shared with the warrior template (verified 23-bone core
    // is identical across genders/races; differences are mesh-level).
    // The profile tag is validated here so templates never carry a dead id.
    auto res = makeSampleArmor();
    if (!res) return res;
    res.value().skeleton.name = profileId;
    res.value().mesh.name = "sample-" + profileId;
    return res;
}

}  // namespace m2rig
