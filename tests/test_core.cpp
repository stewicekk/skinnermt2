// Canonical mesh/skeleton/weights/profile/sample tests.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

#include "../tests/expect.hpp"
#include "m2rig/ibl.hpp"
#include "m2rig/lod.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/renderer.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/spatial.hpp"
#include "m2rig/utf8.hpp"
#include "m2rig/validation.hpp"

using namespace m2rig;

M2RIG_TEST(core, sample_skeleton_builds) {
    int failures = 0;
    auto res = makeSampleSkeleton();
    CHECK_TRUE(res.succeeded());
    CHECK_EQ(res.value().bones.size(), 23u);
    CHECK_TRUE(res.value().rootBone != kNoParent);
    const Bone* pelvis = res.value().findByName("Bip01 Pelvis");
    CHECK_TRUE(pelvis != nullptr);
    return failures;
}

M2RIG_TEST(core, skeleton_rejects_duplicates_and_cycles) {
    int failures = 0;
    auto dup = buildSkeleton("dup", {{"A", kNoParent, {}, {}, {}}, {"A", 0, {}, {}, {}}});
    CHECK_FALSE(dup.succeeded());
    auto badParent = buildSkeleton("bad", {{"A", 5, {}, {}, {}}});
    CHECK_FALSE(badParent.succeeded());
    auto cycle =
        buildSkeleton("cycle", {{"A", 1, {}, {}, {}}, {"B", 0, {}, {}, {}}});
    CHECK_FALSE(cycle.succeeded());
    return failures;
}

M2RIG_TEST(core, sample_armor_weights_valid) {
    int failures = 0;
    auto res = makeSampleArmor();
    CHECK_TRUE(res.succeeded());
    if (!res.succeeded()) return failures + 1;
    ValidationReport report;
    validateMeshStructure(res.value().mesh, "sample", report);
    validateMeshWeights(res.value().mesh, res.value().skeleton.bones.size(), "sample", report);
    CHECK_FALSE(report.exportBlocked());
    CHECK_TRUE(res.value().mesh.vertices.size() > 500u);
    CHECK_TRUE(res.value().mesh.triangleCount() > 500u);
    return failures;
}

M2RIG_TEST(core, weight_repair_pipeline) {
    int failures = 0;
    // NaN + negative + duplicate + 6 influences -> repaired to <= 4, sum 1.
    std::vector<BoneInfluence> infs = {{0, 0.5f},
                                       {1, 0.3f},
                                       {1, 0.2f},
                                       {2, -0.1f},
                                       {3, std::numeric_limits<float>::quiet_NaN()},
                                       {4, 0.25f},
                                       {5, 0.15f}};
    RepairStats stats;
    const bool usable = repairVertexInfluences(infs, 4, &stats);
    CHECK_TRUE(usable);
    CHECK_TRUE(infs.size() <= 4u);
    CHECK_NEAR(influenceTotal(infs), 1.0f, 1e-4);
    CHECK_TRUE(stats.duplicatesMerged >= 1u);
    CHECK_TRUE(stats.invalidRemoved >= 2u);
    CHECK_TRUE(stats.removedMass >= 0.0);
    return failures;
}

M2RIG_TEST(core, weight_repair_never_silent_truncate) {
    int failures = 0;
    // 6 equal influences of 1/6: reduction to 4 must report dropped mass 2/6.
    std::vector<BoneInfluence> infs;
    for (std::uint32_t i = 0; i < 6; ++i) infs.push_back({i, 1.0f / 6.0f});
    RepairStats stats;
    repairVertexInfluences(infs, 4, &stats);
    CHECK_EQ(infs.size(), 4u);
    CHECK_NEAR(stats.removedMass, 2.0 / 6.0, 1e-5);
    CHECK_NEAR(influenceTotal(infs), 1.0f, 1e-4);
    return failures;
}

M2RIG_TEST(core, profile_mirror_and_mapping) {
    int failures = 0;
    const SkeletonProfile p = warriorProfile();
    CHECK_TRUE(mirrorBoneName(p, "Bip01 L Hand") == "Bip01 R Hand");
    CHECK_TRUE(mirrorBoneName(p, "Bip01") == "Bip01");
    CHECK_TRUE(canonicalBoneName(p, "bip01_l_hand") == "Bip01 L Hand");
    auto a = makeSampleSkeleton();
    auto b = makeSampleSkeleton();
    CHECK_TRUE(a.succeeded() && b.succeeded());
    const auto mapping = mapBonesByProfile(a.value(), b.value(), p);
    CHECK_EQ(mapping.size(), a.value().bones.size());
    for (std::uint32_t m : mapping) CHECK_TRUE(m != kUnmappedBone);
    return failures;
}

M2RIG_TEST(core, mesh_structure_detects_damage) {
    int failures = 0;
    Mesh mesh;
    mesh.name = "broken";
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 99};  // out of range
    ValidationReport report;
    validateMeshStructure(mesh, "broken", report);
    CHECK_TRUE(report.exportBlocked());
    return failures;
}

namespace {

Mesh makeTriMesh(std::vector<Vec3> positions, std::vector<std::uint32_t> indices) {
    Mesh mesh;
    mesh.name = "topo";
    mesh.vertices.resize(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) mesh.vertices[i].position = positions[i];
    mesh.indices = std::move(indices);
    return mesh;
}

bool hasItem(const ValidationReport& report, const std::string& id) {
    for (const auto& item : report.items())
        if (item.id == id) return true;
    return false;
}

}  // namespace

M2RIG_TEST(core, topology_closed_tetrahedron) {
    int failures = 0;
    Mesh mesh = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
                            {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3});
    const MeshTopology topo = buildMeshTopology(mesh);
    CHECK_EQ(topo.validTriangles, 4u);
    CHECK_EQ(topo.boundaryEdges, 0u);
    CHECK_EQ(topo.manifoldEdges, 6u);
    CHECK_EQ(topo.nonManifoldEdges, 0u);
    CHECK_EQ(topo.isolatedVertices, 0u);
    CHECK_EQ(topo.openVertices, 0u);
    return failures;
}

M2RIG_TEST(core, topology_open_fan_and_isolated_vertex) {
    int failures = 0;
    // Two triangles sharing edge 0-1 plus one unreferenced vertex 4.
    Mesh mesh = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {5, 5, 5}},
                            {0, 1, 2, 0, 3, 1});
    const MeshTopology topo = buildMeshTopology(mesh);
    CHECK_EQ(topo.validTriangles, 2u);
    CHECK_EQ(topo.boundaryEdges, 4u);
    CHECK_EQ(topo.manifoldEdges, 1u);
    CHECK_EQ(topo.nonManifoldEdges, 0u);
    CHECK_EQ(topo.isolatedVertices, 1u);
    CHECK_EQ(topo.openVertices, 4u);
    return failures;
}

M2RIG_TEST(core, topology_detects_non_manifold_edge) {
    int failures = 0;
    // Three triangles sharing edge 0-1: each contributes 2 boundary edges.
    Mesh mesh = makeTriMesh({{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}},
                            {0, 1, 2, 0, 1, 3, 0, 1, 4});
    const MeshTopology topo = buildMeshTopology(mesh);
    CHECK_EQ(topo.validTriangles, 3u);
    CHECK_EQ(topo.boundaryEdges, 6u);
    CHECK_EQ(topo.manifoldEdges, 0u);
    CHECK_EQ(topo.nonManifoldEdges, 1u);
    CHECK_EQ(topo.isolatedVertices, 0u);
    ValidationReport report;
    validateMeshStructure(mesh, "topo", report);
    CHECK_TRUE(hasItem(report, "MESH_TOPOLOGY"));
    CHECK_TRUE(hasItem(report, "MESH_NON_MANIFOLD"));
    CHECK_TRUE(!report.exportBlocked());  // warning only, game meshes stay exportable
    return failures;
}

M2RIG_TEST(core, topology_skips_bad_triangles_deterministically) {
    int failures = 0;
    // Index 9 is out of range and tri (0,0,1) is degenerate: both skipped.
    // Order A and its triangle-reversed twin must agree exactly.
    Mesh a = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
                         {0, 1, 9, 0, 0, 1, 0, 1, 2, 0, 3, 1});
    Mesh b = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}},
                         {0, 3, 1, 0, 1, 2, 0, 0, 1, 0, 1, 9});
    const MeshTopology ta = buildMeshTopology(a);
    const MeshTopology tb = buildMeshTopology(b);
    CHECK_EQ(ta.validTriangles, 2u);
    CHECK_EQ(ta.boundaryEdges, tb.boundaryEdges);
    CHECK_EQ(ta.manifoldEdges, tb.manifoldEdges);
    CHECK_EQ(ta.nonManifoldEdges, tb.nonManifoldEdges);
    CHECK_EQ(ta.isolatedVertices, tb.isolatedVertices);
    CHECK_EQ(ta.openVertices, tb.openVertices);
    CHECK_EQ(ta.boundaryEdges, 4u);
    CHECK_EQ(ta.manifoldEdges, 1u);
    CHECK_EQ(ta.isolatedVertices, 0u);
    return failures;
}

namespace {

Mesh makeCube() {
    Mesh mesh;
    mesh.name = "cube";
    const float c[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                           {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    mesh.vertices.resize(8);
    for (int i = 0; i < 8; ++i) {
        mesh.vertices[static_cast<std::size_t>(i)].position = {c[i][0], c[i][1], c[i][2]};
        mesh.vertices[static_cast<std::size_t>(i)].influences.push_back({0, 1.0f});
    }
    mesh.indices = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 3, 7, 0, 7, 4,
                    1, 5, 6, 1, 6, 2, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7};
    SubMesh a, b;
    a.name = "side_a";
    a.materialIndex = 0;
    a.startIndex = 0;
    a.indexCount = 18;
    b.name = "side_b";
    b.materialIndex = 1;
    b.startIndex = 18;
    b.indexCount = 18;
    mesh.subMeshes = {a, b};
    mesh.materials.push_back({"a.dds", "a.dds"});
    mesh.materials.push_back({"b.dds", "b.dds"});
    computeBounds(mesh);
    return mesh;
}

bool lodWeightsSane(const Mesh& mesh) {
    for (const auto& v : mesh.vertices) {
        if (v.influences.size() > 4) return false;
        double sum = 0.0;
        for (const auto& inf : v.influences) sum += inf.weight;
        if (!v.influences.empty() && (sum < 0.99 || sum > 1.01)) return false;
    }
    return true;
}

}  // namespace

M2RIG_TEST(core, lod_cube_halves_deterministically) {
    int failures = 0;
    Mesh mesh = makeCube();
    LodOptions opt;
    opt.targetRatio = 0.5f;
    auto r = decimateMesh(mesh, opt);
    CHECK_TRUE(r.succeeded());
    if (!r.succeeded()) return failures + 1;
    CHECK_EQ(r.value().inputTriangles, 12u);
    // Collapse steps remove 1-2 tris each, so the loop may undershoot the
    // keep-target by one step; the floor below is what the scheme promises.
    CHECK_TRUE(r.value().outputTriangles <= 6u);
    CHECK_TRUE(r.value().outputTriangles >= 4u);
    CHECK_TRUE(r.value().collapses > 0u);
    CHECK_TRUE(mesh.vertices.size() < 8u);
    CHECK_TRUE(lodWeightsSane(mesh));
    // Submesh ranges stay valid and cover every triangle.
    std::size_t covered = 0;
    for (const auto& sm : mesh.subMeshes) {
        CHECK_TRUE(sm.startIndex + sm.indexCount <= mesh.indices.size());
        CHECK_TRUE(sm.indexCount % 3 == 0u);
        covered += sm.indexCount / 3;
    }
    CHECK_EQ(covered, mesh.triangleCount());
    // Determinism: identical input => identical output.
    Mesh again = makeCube();
    CHECK_TRUE(decimateMesh(again, opt).succeeded());
    CHECK_EQ(again.indices.size(), mesh.indices.size());
    CHECK_EQ(again.vertices.size(), mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.indices.size(); ++i)
        CHECK_EQ(again.indices[i], mesh.indices[i]);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        CHECK_NEAR(again.vertices[i].position.x, mesh.vertices[i].position.x, 1e-6);
        CHECK_NEAR(again.vertices[i].position.y, mesh.vertices[i].position.y, 1e-6);
        CHECK_NEAR(again.vertices[i].position.z, mesh.vertices[i].position.z, 1e-6);
    }
    return failures;
}

M2RIG_TEST(core, lod_merge_unions_bone_weights) {
    int failures = 0;
    // Tetrahedron, one distinct bone per vertex. Shortest edges tie at
    // length 1; deterministic order picks (0,1): the survivor must carry
    // both bones, normalized.
    Mesh mesh;
    mesh.name = "tet";
    mesh.vertices.resize(4);
    const float p[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 4; ++i) {
        mesh.vertices[static_cast<std::size_t>(i)].position = {p[i][0], p[i][1], p[i][2]};
        mesh.vertices[static_cast<std::size_t>(i)].influences.push_back(
            {static_cast<std::uint32_t>(i), 1.0f});
    }
    mesh.indices = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    computeBounds(mesh);
    LodOptions opt;
    opt.targetRatio = 0.5f;
    opt.minTriangles = 2;  // default floor 4 would no-op a 4-tri mesh
    auto r = decimateMesh(mesh, opt);
    CHECK_TRUE(r.succeeded());
    if (!r.succeeded()) return failures + 1;
    CHECK_EQ(r.value().collapses, 1u);
    CHECK_EQ(r.value().outputTriangles, 2u);
    bool foundUnion = false;
    for (const auto& v : mesh.vertices) {
        bool has0 = false, has1 = false;
        double sum = 0.0;
        for (const auto& inf : v.influences) {
            if (inf.bone == 0) has0 = true;
            if (inf.bone == 1) has1 = true;
            sum += inf.weight;
        }
        if (has0 && has1) {
            foundUnion = true;
            CHECK_NEAR(sum, 1.0, 1e-4);
        }
    }
    CHECK_TRUE(foundUnion);
    CHECK_TRUE(lodWeightsSane(mesh));
    return failures;
}

M2RIG_TEST(core, lod_rejects_bad_options_and_empty) {
    int failures = 0;
    Mesh mesh = makeCube();
    LodOptions opt;
    opt.targetRatio = 0.0f;
    CHECK_FALSE(decimateMesh(mesh, opt).succeeded());
    opt.targetRatio = 1.0f;
    CHECK_FALSE(decimateMesh(mesh, opt).succeeded());
    opt.targetRatio = 2.0f;
    CHECK_FALSE(decimateMesh(mesh, opt).succeeded());
    Mesh empty;
    opt.targetRatio = 0.5f;
    CHECK_FALSE(decimateMesh(empty, opt).succeeded());
    Mesh noTris;
    noTris.vertices.resize(3);
    CHECK_FALSE(decimateMesh(noTris, opt).succeeded());
    // Untouched by the rejections above.
    CHECK_EQ(mesh.triangleCount(), 12u);
    return failures;
}

M2RIG_TEST(core, lod_min_triangles_noop) {
    int failures = 0;
    Mesh mesh = makeCube();
    LodOptions opt;
    opt.targetRatio = 0.5f;
    opt.minTriangles = 12;
    auto r = decimateMesh(mesh, opt);
    CHECK_TRUE(r.succeeded());
    CHECK_EQ(r.value().collapses, 0u);
    CHECK_EQ(mesh.triangleCount(), 12u);
    CHECK_EQ(mesh.vertices.size(), 8u);
    return failures;
}

M2RIG_TEST(core, lod_sample_armor_stays_valid) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    Mesh mesh = sample.value().mesh;
    const std::size_t before = mesh.triangleCount();
    CHECK_TRUE(before > 4u);
    LodOptions opt;
    opt.targetRatio = 0.5f;
    auto r = decimateMesh(mesh, opt);
    CHECK_TRUE(r.succeeded());
    if (!r.succeeded()) return failures + 1;
    CHECK_TRUE(mesh.triangleCount() < before);
    CHECK_TRUE(lodWeightsSane(mesh));
    ValidationReport report;
    validateMeshStructure(mesh, "lod", report);
    validateMeshWeights(mesh, sample.value().skeleton.bones.size(), "lod", report);
    CHECK_TRUE(!report.exportBlocked());
    return failures;
}

M2RIG_TEST(core, topology_isolated_vertex_warns_without_blocking) {
    int failures = 0;
    Mesh mesh = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {9, 9, 9}}, {0, 1, 2});
    mesh.materials.push_back({"armor.dds", "armor.dds"});
    ValidationReport report;
    validateMeshStructure(mesh, "topo", report);
    CHECK_TRUE(hasItem(report, "MESH_TOPOLOGY"));
    CHECK_TRUE(hasItem(report, "MESH_ISOLATED_VERTS"));
    CHECK_TRUE(!report.exportBlocked());
    return failures;
}

namespace {

const ValidationItem* findReportItem(const ValidationReport& report, const char* id) {
    for (const auto& item : report.items())
        if (item.id == id) return &item;
    return nullptr;
}

}  // namespace

M2RIG_TEST(core, uv_range_clean_quad_has_zero_census) {
    int failures = 0;
    Mesh mesh;
    mesh.name = "uvclean";
    mesh.vertices.resize(4);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {1, 1, 0};
    mesh.vertices[3].position = {0, 1, 0};
    mesh.vertices[0].uv0 = {0, 0};
    mesh.vertices[1].uv0 = {1, 0};
    mesh.vertices[2].uv0 = {1, 1};
    mesh.vertices[3].uv0 = {0, 1};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.materials.push_back({"armor.dds", "armor.dds"});
    ValidationReport report;
    validateMeshStructure(mesh, "uvclean", report);
    CHECK_FALSE(report.exportBlocked());
    CHECK_TRUE(hasItem(report, "MESH_UV_RANGE"));
    const ValidationItem* range = findReportItem(report, "MESH_UV_RANGE");
    CHECK_TRUE(range != nullptr);
    if (range != nullptr) {
        CHECK_TRUE(range->severity == Severity::Info);
        CHECK_TRUE(range->message.find("0/4 verts outside [0,1]") != std::string::npos);
    }
    CHECK_FALSE(hasItem(report, "MESH_UV_DEGENERATE"));
    CHECK_FALSE(hasItem(report, "MESH_UV_OVERLAP"));
    CHECK_EQ(report.count(Severity::Warning), 0u);
    CHECK_EQ(report.count(Severity::Error), 0u);
    return failures;
}

M2RIG_TEST(core, uv_range_tiled_quad_reports_exact_counts) {
    int failures = 0;
    Mesh mesh;
    mesh.name = "uvtiled";
    mesh.vertices.resize(4);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {1, 1, 0};
    mesh.vertices[3].position = {0, 1, 0};
    mesh.vertices[0].uv0 = {0, 0};
    mesh.vertices[1].uv0 = {2, 0};
    mesh.vertices[2].uv0 = {2, 1};
    mesh.vertices[3].uv0 = {0, 1};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.materials.push_back({"armor.dds", "armor.dds"});
    ValidationReport report;
    validateMeshStructure(mesh, "uvtiled", report);
    CHECK_FALSE(report.exportBlocked());
    const ValidationItem* range = findReportItem(report, "MESH_UV_RANGE");
    CHECK_TRUE(range != nullptr);
    if (range != nullptr) {
        CHECK_TRUE(range->severity == Severity::Info);
        CHECK_TRUE(range->message.find("2/4 verts outside [0,1]") != std::string::npos);
        CHECK_TRUE(range->message.find("u 0..2") != std::string::npos);
        CHECK_TRUE(range->message.find("v 0..1") != std::string::npos);
    }
    CHECK_FALSE(hasItem(report, "MESH_UV_DEGENERATE"));
    CHECK_FALSE(hasItem(report, "MESH_UV_OVERLAP"));
    return failures;
}

M2RIG_TEST(core, uv_degenerate_warns_without_blocking_export) {
    int failures = 0;
    // Positions span area 0.5 (no MESH_DEGENERATE) while all three UVs are
    // identical, so only the UV-space rule may fire.
    Mesh mesh;
    mesh.name = "uvdeg";
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {0, 1, 0};
    mesh.vertices[0].uv0 = {0.5f, 0.5f};
    mesh.vertices[1].uv0 = {0.5f, 0.5f};
    mesh.vertices[2].uv0 = {0.5f, 0.5f};
    mesh.indices = {0, 1, 2};
    mesh.materials.push_back({"armor.dds", "armor.dds"});
    ValidationReport report;
    validateMeshStructure(mesh, "uvdeg", report);
    CHECK_TRUE(hasItem(report, "MESH_UV_DEGENERATE"));
    CHECK_TRUE(!report.exportBlocked());  // warning only, export stays open
    const ValidationItem* deg = findReportItem(report, "MESH_UV_DEGENERATE");
    CHECK_TRUE(deg != nullptr);
    if (deg != nullptr) {
        CHECK_TRUE(deg->severity == Severity::Warning);
        CHECK_TRUE(deg->message.find("1 triangles with degenerate UVs") != std::string::npos);
        CHECK_TRUE(deg->message.find("first tri 0") != std::string::npos);
    }
    CHECK_FALSE(hasItem(report, "MESH_DEGENERATE"));
    CHECK_FALSE(hasItem(report, "MESH_UV_OVERLAP"));
    return failures;
}

M2RIG_TEST(core, uv_overlap_reports_duplicate_groups) {
    int failures = 0;
    // Two position-distinct triangles carrying the same UV set (second in
    // rotated assignment order, proving order-independence of the key).
    Mesh mesh;
    mesh.name = "uvoverlap";
    mesh.vertices.resize(6);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {0, 1, 0};
    mesh.vertices[3].position = {2, 0, 0};
    mesh.vertices[4].position = {3, 0, 0};
    mesh.vertices[5].position = {2, 1, 0};
    mesh.vertices[0].uv0 = {0, 0};
    mesh.vertices[1].uv0 = {1, 0};
    mesh.vertices[2].uv0 = {0, 1};
    mesh.vertices[3].uv0 = {0, 1};
    mesh.vertices[4].uv0 = {0, 0};
    mesh.vertices[5].uv0 = {1, 0};
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.materials.push_back({"armor.dds", "armor.dds"});
    ValidationReport report;
    validateMeshStructure(mesh, "uvoverlap", report);
    CHECK_TRUE(hasItem(report, "MESH_UV_OVERLAP"));
    CHECK_TRUE(!report.exportBlocked());  // info only
    const ValidationItem* over = findReportItem(report, "MESH_UV_OVERLAP");
    CHECK_TRUE(over != nullptr);
    if (over != nullptr) {
        CHECK_TRUE(over->severity == Severity::Info);
        CHECK_TRUE(over->message.find("1 duplicate-UV groups") != std::string::npos);
        CHECK_TRUE(over->message.find("2 triangles") != std::string::npos);
        CHECK_TRUE(over->message.find("1 wasted") != std::string::npos);
    }
    CHECK_FALSE(hasItem(report, "MESH_UV_DEGENERATE"));
    return failures;
}

M2RIG_TEST(core, uv_findings_deterministic_under_triangle_reorder) {
    int failures = 0;
    // Tri0 clean, tri1 degenerate-UV, tri2 duplicates tri0's UV set, tri3
    // tiled (unique). Verts are per-triangle so reordering the index buffer
    // permutes UV sets without touching per-vert RANGE extrema.
    Mesh a;
    a.name = "uvdet";
    a.vertices.resize(12);
    const float pos[12][3] = {{0, 0, 0},
                              {1, 0, 0},
                              {0, 1, 0},
                              {2, 0, 0},
                              {3, 0, 0},
                              {2, 1, 0},
                              {4, 0, 0},
                              {5, 0, 0},
                              {4, 1, 0},
                              {6, 0, 0},
                              {7, 0, 0},
                              {6, 1, 0}};
    const float uv[12][2] = {{0, 0},
                             {1, 0},
                             {0, 1},
                             {0.5f, 0.5f},
                             {0.5f, 0.5f},
                             {0.5f, 0.5f},
                             {0, 0},
                             {1, 0},
                             {0, 1},
                             {2.5f, 0.5f},
                             {3, 0.5f},
                             {2.5f, 1}};
    for (std::size_t i = 0; i < 12u; ++i) {
        a.vertices[i].position = {pos[i][0], pos[i][1], pos[i][2]};
        a.vertices[i].uv0 = {uv[i][0], uv[i][1]};
    }
    a.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    a.materials.push_back({"armor.dds", "armor.dds"});
    Mesh b = a;
    b.indices = {9, 10, 11, 6, 7, 8, 3, 4, 5, 0, 1, 2};  // reversed tri order
    ValidationReport ra, rb;
    validateMeshStructure(a, "uvdet", ra);
    validateMeshStructure(b, "uvdet", rb);
    const ValidationItem* rangeA = findReportItem(ra, "MESH_UV_RANGE");
    const ValidationItem* rangeB = findReportItem(rb, "MESH_UV_RANGE");
    const ValidationItem* degA = findReportItem(ra, "MESH_UV_DEGENERATE");
    const ValidationItem* degB = findReportItem(rb, "MESH_UV_DEGENERATE");
    const ValidationItem* overA = findReportItem(ra, "MESH_UV_OVERLAP");
    const ValidationItem* overB = findReportItem(rb, "MESH_UV_OVERLAP");
    CHECK_TRUE(rangeA != nullptr && rangeB != nullptr);
    CHECK_TRUE(degA != nullptr && degB != nullptr);
    CHECK_TRUE(overA != nullptr && overB != nullptr);
    if (rangeA != nullptr && rangeB != nullptr) {
        // Per-vert census: byte-identical regardless of triangle order.
        CHECK_TRUE(rangeA->message == rangeB->message);
        CHECK_TRUE(rangeA->message.find("3/12 verts outside [0,1]") != std::string::npos);
    }
    if (degA != nullptr && degB != nullptr) {
        // Same degenerate count; only the positional first-tri may move.
        CHECK_TRUE(degA->message.find("1 triangles with degenerate UVs") != std::string::npos);
        CHECK_TRUE(degB->message.find("1 triangles with degenerate UVs") != std::string::npos);
    }
    if (overA != nullptr && overB != nullptr) {
        // Counts only: byte-identical regardless of triangle order.
        CHECK_TRUE(overA->message == overB->message);
        CHECK_TRUE(overA->message.find("1 duplicate-UV groups") != std::string::npos);
        CHECK_TRUE(overA->message.find("1 wasted") != std::string::npos);
    }
    CHECK_TRUE(ra.exportBlocked() == rb.exportBlocked());
    CHECK_EQ(ra.count(Severity::Warning), rb.count(Severity::Warning));
    CHECK_EQ(ra.count(Severity::Info), rb.count(Severity::Info));
    return failures;
}

M2RIG_TEST(core, skinning_stream_sample_armor) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const Mesh& mesh = sample.value().mesh;
    const std::size_t bones = sample.value().skeleton.bones.size();
    auto skin = buildSkinVertices(mesh, bones);
    CHECK_TRUE(skin.succeeded());
    if (!skin.succeeded()) return failures + 1;
    CHECK_EQ(skin.value().size(), mesh.vertices.size());
    for (const auto& s : skin.value()) {
        double wsum = 0.0;
        int count = 0;
        for (int k = 0; k < 4; ++k) {
            CHECK_TRUE(s.bones[k] < bones);
            CHECK_TRUE(s.weights[k] >= 0.0f);
            if (s.weights[k] > 0.0f) {
                ++count;
                wsum += s.weights[k];
            }
        }
        CHECK_TRUE(count <= 4);
        if (count > 0) CHECK_NEAR(wsum, 1.0, 1e-4);
    }
    return failures;
}

M2RIG_TEST(core, skinning_stream_rejects_bad_data) {
    int failures = 0;
    Mesh mesh = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
    CHECK_TRUE(!mesh.vertices.empty());
    mesh.vertices[0].influences = {{300u, 1.0f}};
    CHECK_FALSE(buildSkinVertices(mesh, 2u).succeeded());  // dangling id
    mesh.vertices[0].influences = {{1u, 1.0f}};
    CHECK_FALSE(buildSkinVertices(mesh, 2u, 1u).succeeded());  // beyond palette
    mesh.vertices[0].influences = {{0, 0.2f}, {1, 0.2f}, {2, 0.2f}, {3, 0.2f}, {4, 0.2f}};
    CHECK_FALSE(buildSkinVertices(mesh, 8u).succeeded());  // >4 influences
    mesh.vertices[0].influences = {{0, -0.5f}};
    CHECK_FALSE(buildSkinVertices(mesh, 8u).succeeded());  // negative weight
    // Unweighted verts encode all-zero weights (shader bind fallback).
    mesh.vertices[0].influences.clear();
    auto ok = buildSkinVertices(mesh, 8u);
    CHECK_TRUE(ok.succeeded());
    if (ok.succeeded()) {
        for (int k = 0; k < 4; ++k) CHECK_TRUE(ok.value()[0].weights[k] == 0.0f);
    }
    return failures;
}

M2RIG_TEST(core, tangents_sample_armor_sane) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    for (const auto& v : sample.value().mesh.vertices) {
        CHECK_TRUE(v.hasTangent);
        const Vec3 t{v.tangent.x, v.tangent.y, v.tangent.z};
        CHECK_TRUE(isFiniteF(t.x) && isFiniteF(t.y) && isFiniteF(t.z));
        // Gram-Schmidt orthogonality + unit handedness.
        CHECK_NEAR(dot(v.normal, t), 0.0, 1e-3);
        CHECK_TRUE(v.tangent.w == 1.0f || v.tangent.w == -1.0f);
    }
    return failures;
}

M2RIG_TEST(core, sh_uniform_sky_is_pi) {
    // Analytic pin: a uniform sky of radiance L has exactly L*pi diffuse
    // irradiance for EVERY normal (validates projection + band scales + the
    // basis evaluator as one composed chain, no GPU involved).
    int failures = 0;
    IblSkyParams sky;
    sky.zenith = sky.horizon = sky.ground = {0.5f, 0.5f, 0.5f};
    sky.sunIntensity = 0.0f;
    sky.glowIntensity = 0.0f;
    sky.exposure = 1.0f;
    const SphericalHarmonics sh = projectSky(sky, 48, 96);
    const Vec3 dirs[5] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {0.3f, -0.5f, 0.7f}};
    for (const Vec3& d : dirs) {
        const Vec3 e = shIrradiance(sh, normalized(d));
        CHECK_NEAR(e.x, 0.5f * kPi, 0.02);
        CHECK_NEAR(e.y, 0.5f * kPi, 0.02);
        CHECK_NEAR(e.z, 0.5f * kPi, 0.02);
    }
    return failures;
}

M2RIG_TEST(core, half_codec_roundtrip) {
    int failures = 0;
    // Exact bit patterns (IEEE-754 binary16).
    CHECK_TRUE(floatToHalf(1.0f) == 0x3C00u);
    CHECK_TRUE(floatToHalf(0.5f) == 0x3800u);
    CHECK_TRUE(floatToHalf(0.0f) == 0x0000u);
    CHECK_TRUE(floatToHalf(-2.0f) == 0xC000u);
    CHECK_TRUE(floatToHalf(65504.0f) == 0x7BFFu);
    // Sky-range roundtrip within half precision (~0.05% relative).
    const float vals[6] = {0.03f, 0.25f, 0.8f, 1.5f, 4.0f, 12.0f};
    for (float v : vals) {
        const float back = halfToFloat(floatToHalf(v));
        CHECK_NEAR(back, v, std::fabs(v) * 0.002 + 1e-4);
    }
    return failures;
}

M2RIG_TEST(core, sh_deterministic_and_sky_shaped) {
    int failures = 0;
    const IblSkyParams sky = defaultSky();
    const SphericalHarmonics a = projectSky(sky);
    const SphericalHarmonics b = projectSky(sky);
    for (int i = 0; i < 9; ++i)
        for (int k = 0; k < 3; ++k) CHECK_TRUE(a.c[i][k] == b.c[i][k]);
    // Up sees the bright sky, down sees the dim ground (sanity shape).
    const Vec3 up = shIrradiance(a, {0, 1, 0});
    const Vec3 down = shIrradiance(a, {0, -1, 0});
    CHECK_TRUE(up.y > down.y);
    CHECK_TRUE(up.x > 0.0f && down.x > 0.0f);
    // Sun above the horizon contributes: sun-facing beats sun-averted.
    const Vec3 toSun = shIrradiance(a, normalized(sky.sunDir));
    const Vec3 fromSun = shIrradiance(a, normalized(sky.sunDir * -1.0f));
    CHECK_TRUE(toSun.x > fromSun.x);
    return failures;
}

M2RIG_TEST(core, tangents_degenerate_uv_fallback) {
    int failures = 0;
    // makeTriMesh UVs are degenerate (all zero) -> documented (1,0,0,1).
    Mesh mesh = makeTriMesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
    computeNormals(mesh);
    computeTangents(mesh);
    for (const auto& v : mesh.vertices) {
        CHECK_TRUE(v.hasTangent);
        CHECK_NEAR(v.tangent.x, 1.0, 1e-6);
        CHECK_NEAR(v.tangent.y, 0.0, 1e-6);
        CHECK_NEAR(v.tangent.z, 0.0, 1e-6);
        CHECK_TRUE(v.tangent.w == 1.0f);
    }
    // Tangents never rewrite normals.
    for (const auto& v : mesh.vertices) {
        CHECK_NEAR(v.normal.x, 0.0, 1e-6);
        CHECK_NEAR(v.normal.y, 0.0, 1e-6);
        CHECK_NEAR(v.normal.z, 1.0, 1e-6);
    }
    return failures;
}

M2RIG_TEST(core, gpu_tangents_nonzero) {
    // src/mesh_views.cpp builders copy Vertex.tangent into GpuVertex.tangent
    // (hasTangent gate, (1,0,0)/(0,1,0) fallback). The tangents_* pins above
    // only check Vertex storage; nothing pinned the GPU copy until now.
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const Mesh& mesh = sample.value().mesh;
    const std::vector<GpuVertex> gpu = buildGpuVertices(mesh, MeshColoring::Solid);
    CHECK_EQ(gpu.size(), mesh.vertices.size());
    CHECK_TRUE(!gpu.empty());
    std::size_t good = 0;
    for (std::size_t i = 0; i < gpu.size(); ++i) {
        const Vec3 t = gpu[i].tangent;
        const Vec3 n = gpu[i].normal;
        if (length(t) > 0.5f && std::fabs(dot(n, t)) < 0.05) ++good;
    }
    // >=90% of verts must carry a usable (non-degenerate, orthogonal)
    // tangent frame (sample armor is 100% in practice; the margin keeps the
    // pin behavioral, not byte-exact).
    CHECK_TRUE(good * 10u >= gpu.size() * 9u);
    // Tangent-less mesh (default Vertex hasTangent=false) takes the
    // documented (1,0,0) fallback.
    Mesh plain;
    plain.name = "notangent";
    plain.vertices.resize(3);
    const std::vector<GpuVertex> fb = buildGpuVertices(plain, MeshColoring::Solid);
    CHECK_EQ(fb.size(), plain.vertices.size());
    for (const auto& g : fb) {
        CHECK_NEAR(g.tangent.x, 1.0, 1e-6);
        CHECK_NEAR(g.tangent.y, 0.0, 1e-6);
        CHECK_NEAR(g.tangent.z, 0.0, 1e-6);
    }
    return failures;
}

namespace {

// Deterministic LCG (fixed seed): pseudo-random but reproducible points.
struct TestRng {
    std::uint32_t s = 0x12345678u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
    }
};

// Inline brute-force reference: same scan order and first-found-wins ties
// as the loops the KD-tree replaced (plus the explicit NaN skip both the
// tree and the production comparisons implement).
std::vector<KnnHit> bruteKnn(const std::vector<Vec3>& pts, const Vec3& q, std::size_t k) {
    std::vector<KnnHit> best;
    for (std::uint32_t i = 0; i < pts.size(); ++i) {
        const Vec3 d = q - pts[i];
        const float ds = dot(d, d);
        // Non-finite never satisfies the strict production comparison.
        if (!isFiniteF(ds)) continue;
        std::size_t pos = 0;
        while (pos < best.size() &&
               (best[pos].distSq < ds || (best[pos].distSq == ds && best[pos].index < i)))
            ++pos;
        if (pos < k) {
            best.insert(best.begin() + static_cast<std::ptrdiff_t>(pos), KnnHit{i, ds});
            if (best.size() > k) best.pop_back();
        }
    }
    return best;
}

}  // namespace

M2RIG_TEST(core, spatial_index_matches_bruteforce) {
    int failures = 0;
    TestRng rng;
    std::vector<Vec3> pts(2000);
    for (auto& p : pts) p = {rng.next() * 10.0f, rng.next() * 10.0f, rng.next() * 10.0f};
    const KdTree tree(pts);
    // Second build must answer identically (deterministic construction).
    const KdTree tree2(pts);
    const std::size_t ks[3] = {1, 3, 8};
    for (int qi = 0; qi < 50; ++qi) {
        const Vec3 q{rng.next() * 10.0f, rng.next() * 10.0f, rng.next() * 10.0f};
        for (std::size_t k : ks) {
            const std::vector<KnnHit> got = tree.query(q, k);
            const std::vector<KnnHit> want = bruteKnn(pts, q, k);
            const std::vector<KnnHit> got2 = tree2.query(q, k);
            CHECK_EQ(got.size(), want.size());
            CHECK_EQ(got2.size(), want.size());
            const std::size_t n = got.size() < want.size() ? got.size() : want.size();
            for (std::size_t i = 0; i < n; ++i) {
                CHECK_TRUE(got[i].index == want[i].index);
                CHECK_TRUE(got[i].distSq == want[i].distSq);
                CHECK_TRUE(got2[i].index == want[i].index);
            }
        }
        // Radius: exact set equality (both sides sorted by construction).
        const std::vector<KnnHit> gr = tree.queryRadius(q, 1.5f);
        std::vector<KnnHit> wr;
        for (std::uint32_t i = 0; i < pts.size(); ++i) {
            const Vec3 d = q - pts[i];
            const float ds = dot(d, d);
            if (ds < 1.5f * 1.5f) wr.push_back(KnnHit{i, ds});
        }
        std::sort(wr.begin(), wr.end(), [](const KnnHit& a, const KnnHit& b) {
            if (a.distSq != b.distSq) return a.distSq < b.distSq;
            return a.index < b.index;
        });
        CHECK_EQ(gr.size(), wr.size());
        const std::size_t m = gr.size() < wr.size() ? gr.size() : wr.size();
        for (std::size_t i = 0; i < m; ++i) {
            CHECK_TRUE(gr[i].index == wr[i].index);
            CHECK_TRUE(gr[i].distSq == wr[i].distSq);
        }
    }
    return failures;
}

M2RIG_TEST(core, spatial_index_edges) {
    int failures = 0;
    const KdTree empty(std::vector<Vec3>{});
    CHECK_TRUE(empty.empty());
    CHECK_TRUE(empty.query({0, 0, 0}, 3).empty());
    CHECK_TRUE(empty.queryRadius({0, 0, 0}, 1.0f).empty());
    const KdTree single(std::vector<Vec3>{{1, 2, 3}});
    const std::vector<KnnHit> one = single.query({1, 2, 3}, 5);  // k > size
    CHECK_EQ(one.size(), 1u);
    if (!one.empty()) {
        CHECK_TRUE(one[0].index == 0u);
        CHECK_TRUE(one[0].distSq == 0.0f);
    }
    CHECK_TRUE(single.query({0, 0, 0}, 0).empty());
    CHECK_TRUE(single.queryRadius({0, 0, 0}, -1.0f).empty());
    // Duplicate points: index order decides (first-found-wins parity).
    const KdTree dup(std::vector<Vec3>{{5, 5, 5}, {5, 5, 5}, {5, 5, 5}});
    const std::vector<KnnHit> d = dup.query({5, 5, 5}, 2);
    CHECK_EQ(d.size(), 2u);
    if (d.size() == 2u) {
        CHECK_TRUE(d[0].index == 0u);
        CHECK_TRUE(d[1].index == 1u);
    }
    return failures;
}

M2RIG_TEST(core, spatial_index_nan_graceful) {
    // NaN degrades exactly like the brute-force scan (strict comparisons
    // never match NaN): no crash, no hang, no NaN hits — verified against
    // the inline brute reference, not just assumed.
    int failures = 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<Vec3> pts{{0, 0, 0}, {1, 0, 0}, {nan, nan, nan}, {0, 1, 0}};
    const KdTree tree(pts);
    const Vec3 q{0.1f, 0.0f, 0.0f};
    const std::vector<KnnHit> got = tree.query(q, 4);
    const std::vector<KnnHit> want = bruteKnn(pts, q, 4);
    CHECK_EQ(got.size(), want.size());
    const std::size_t n = got.size() < want.size() ? got.size() : want.size();
    for (std::size_t i = 0; i < n; ++i) {
        CHECK_TRUE(got[i].index == want[i].index);
        CHECK_TRUE(got[i].index != 2u);  // the NaN point never wins
    }
    // NaN query degrades to empty (callers pad identically to brute force).
    CHECK_TRUE(tree.query({nan, 0, 0}, 3).empty());
    CHECK_TRUE(tree.queryRadius({nan, 0, 0}, 1.0f).empty());
    return failures;
}

M2RIG_TEST(core, spatial_index_query_timing) {
    // Observation only (no timing assert): same workload through the
    // inline brute reference vs the tree, printed for BENCHMARKS.md.
    int failures = 0;
    TestRng rng;
    std::vector<Vec3> pts(10000);
    for (auto& p : pts) p = {rng.next() * 20.0f, rng.next() * 20.0f, rng.next() * 20.0f};
    std::vector<Vec3> queries(200);
    for (auto& q : queries) q = {rng.next() * 20.0f, rng.next() * 20.0f, rng.next() * 20.0f};
    const KdTree tree(pts);
    long long bruteMs = 0, treeMs = 0;
    std::size_t bruteSink = 0, treeSink = 0;
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (const Vec3& q : queries)
            for (const auto& h : bruteKnn(pts, q, 3)) bruteSink += h.index;
        bruteMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
    }
    {
        const auto t0 = std::chrono::steady_clock::now();
        for (const Vec3& q : queries)
            for (const auto& h : tree.query(q, 3)) treeSink += h.index;
        treeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
    }
    CHECK_TRUE(bruteSink == treeSink);  // same answers, timed fairly
    printf("    knn 10k x 200: brute %lld ms vs tree %lld ms\n", bruteMs, treeMs);
    return failures;
}

M2RIG_TEST(core, spatial_index_transfer_scale) {
    // Observation only (no timing assert — CI machines vary): 5k-vert
    // transfer wall with the Wave-26 index (was O(n*m) brute force).
    int failures = 0;
    Mesh src, dst;
    for (int y = 0; y < 63; ++y) {
        for (int x = 0; x < 80; ++x) {
            Vertex v;
            v.position = {static_cast<float>(x) * 0.1f, static_cast<float>(y) * 0.1f, 0.0f};
            v.influences = {{0u, 1.0f}};
            src.vertices.push_back(v);
            Vertex w = v;
            w.position.x += 0.03f;
            dst.vertices.push_back(w);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    const WeightTransferStats st = transferWeightsKDTree(src, dst, {}, 3);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    printf("    transfer 5040v wall: %lld ms, mapped %zu/%zu\n", static_cast<long long>(ms),
           st.verticesMapped, dst.vertices.size());
    CHECK_EQ(st.verticesMapped, dst.vertices.size());
    CHECK_EQ(st.verticesUnmapped, static_cast<std::size_t>(0));
    return failures;
}

M2RIG_TEST(core, utf8_roundtrip_czech_path) {
    int failures = 0;
    // Explicit UTF-8 bytes (source-encoding independent): a folder path with
    // Czech diacritics. This pins the conversion the Unicode folder dialog
    // (SHBrowseForFolderW) depends on — the old ANSI path mojibake'd these.
    const std::string path = std::string("D:\\modely\\brn") + "\xC4\x9B" + "n" +
                             "\xC3\xAD" + "\\zbroj";  // D:\modely\brnění\zbroj
    const std::wstring w = utf8ToWide(path);
    CHECK_TRUE(!w.empty());
    CHECK_EQ(wideToUtf8(w), path);
    // U+011B (ě) is one wide unit on every platform (BMP, no surrogates).
    CHECK_EQ(utf8ToWide("\xC4\x9B").size(), 1u);
    // Multi-word diacritics round-trip too (ž U+017E, ť U+0165, ú U+00FA).
    const std::string words = std::string("pr\xC3\xA1") + "ce " + "\xC5\xBE" + "lu" +
                              "\xC5\xA5" + " " + "k\xC5\xAF" + "\xC5\x88";
    CHECK_EQ(wideToUtf8(utf8ToWide(words)), words);
    return failures;
}

M2RIG_TEST(core, utf8_empty_and_ascii) {
    int failures = 0;
    CHECK_TRUE(utf8ToWide(std::string{}).empty());
    CHECK_TRUE(wideToUtf8(std::wstring{}).empty());
    CHECK_EQ(utf8ToWide("armor_body.dds"), L"armor_body.dds");
    CHECK_EQ(wideToUtf8(L"smd2msm"), "smd2msm");
    CHECK_EQ(wideToUtf8(utf8ToWide("Data/Models/ninja.fbx")), "Data/Models/ninja.fbx");
    return failures;
}

M2RIG_TEST(core, utf8_euro_and_emoji_units) {
    int failures = 0;
    // U+20AC (€) is BMP: exactly one wide unit everywhere.
    CHECK_EQ(utf8ToWide("\xE2\x82\xAC").size(), 1u);
    CHECK_EQ(wideToUtf8(utf8ToWide("\xE2\x82\xAC")), "\xE2\x82\xAC");
    // U+1F600 (astral plane): surrogate pair on UTF-16 Windows, one unit
    // on UTF-32 platforms — but the round-trip is identical everywhere.
    const std::string emoji = "\xF0\x9F\x98\x80";
    const std::wstring w = utf8ToWide(emoji);
#ifdef _WIN32
    CHECK_EQ(w.size(), 2u);
#else
    CHECK_EQ(w.size(), 1u);
#endif
    CHECK_EQ(wideToUtf8(w), emoji);
    return failures;
}
