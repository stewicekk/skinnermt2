// Canonical mesh/skeleton/weights/profile/sample tests.
#include <cstdio>
#include <limits>

#include "../tests/expect.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/skin_weights.hpp"
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
