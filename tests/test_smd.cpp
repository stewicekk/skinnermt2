// SMD reader/writer/round-trip tests (spec section 21: real files only,
// differences reported, never faked).
#include <cstdio>
#include <filesystem>

#include "../tests/expect.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/skin_weights.hpp"

using namespace m2rig;

namespace {

const char* kTwoBoneSmd = R"(version 1

nodes
  0 "Bip01" -1
  1 "Bip01 Spine" 0
end

skeleton
time 0
  0 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000
  1 0.000000 1.000000 0.000000 0.000000 0.000000 0.000000
time 1
  0 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000
  1 0.000000 1.000000 0.000000 0.100000 0.000000 0.000000
end

triangles
armor_body.dds
0 0.000000 0.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 2 0 0.700000 1 0.300000
0 1.000000 0.000000 0.000000 0.000000 0.000000 1.000000 1.000000 0.000000 1 0 1.000000
1 0.000000 1.000000 0.000000 0.000000 1.000000 0.000000 0.500000 0.500000 0
armor_trim.dds
0 0.000000 0.000000 1.000000 0.000000 0.000000 1.000000 0.000000 1.000000 1 1 1.000000
0 1.000000 0.000000 1.000000 0.000000 0.000000 1.000000 1.000000 1.000000 2 0 0.500000 1 0.500000
1 0.000000 1.000000 1.000000 0.000000 1.000000 0.000000 0.500000 0.000000 1 1 1.000000
end
)";

}  // namespace

M2RIG_TEST(smd, parse_two_bone_model) {
    int failures = 0;
    auto res = parseSmd(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(res.succeeded());
    if (!res.succeeded()) return failures + 1;
    CHECK_EQ(res.value().bones.size(), 2u);
    CHECK_EQ(res.value().frames.size(), 2u);
    CHECK_EQ(res.value().triangles.size(), 2u);
    CHECK_EQ(res.value().materials.size(), 2u);
    CHECK_TRUE(res.value().bones[1].name == "Bip01 Spine");
    CHECK_TRUE(res.value().bones[1].parentId == 0);
    return failures;
}

M2RIG_TEST(smd, convert_preserves_hierarchy_and_rigid_fallback) {
    int failures = 0;
    auto parsed = parseSmd(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    auto conv = smdToAsset(parsed.value(), "two-bone");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    CHECK_EQ(conv.value().skeleton.bones.size(), 2u);
    CHECK_EQ(conv.value().mesh.triangleCount(), 2u);
    // Third vertex of triangle 0 had 0 links -> rigid fallback to parent bone.
    bool foundRigid = false;
    for (const auto& v : conv.value().mesh.vertices) {
        if (v.influences.size() == 1 && std::fabs(v.influences[0].weight - 1.0f) < 1e-5) {
            foundRigid = true;
            break;
        }
    }
    CHECK_TRUE(foundRigid);
    // Frame poses remapped to dense indices 0..1.
    for (const auto& f : conv.value().frames)
        for (const auto& p : f.poses) CHECK_TRUE(p.boneId <= 1u);
    return failures;
}

M2RIG_TEST(smd, roundtrip_identical) {
    int failures = 0;
    auto rep = smdRoundTrip(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(rep.succeeded());
    if (!rep.succeeded()) return failures + 1;
    if (!rep.value().identical()) {
        for (const auto& d : rep.value().diffs)
            printf("    diff %s: %s\n", d.what.c_str(), d.detail.c_str());
    }
    CHECK_TRUE(rep.value().identical());
    return failures;
}

M2RIG_TEST(smd, roundtrip_sample_armor) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    auto written = writeSmd(sample.value().mesh, sample.value().skeleton);
    CHECK_TRUE(written.succeeded());
    if (!written.succeeded()) return failures + 1;
    CHECK_NEAR(written.value().stats.droppedMass, 0.0, 1e-9);
    auto rep = smdRoundTrip(written.value().text, "sample");
    CHECK_TRUE(rep.succeeded());
    if (!rep.succeeded()) return failures + 1;
    if (!rep.value().identical()) {
        for (const auto& d : rep.value().diffs)
            printf("    diff %s: %s\n", d.what.c_str(), d.detail.c_str());
    }
    CHECK_TRUE(rep.value().identical());
    // Re-imported asset validates clean.
    auto re = parseSmd(written.value().text, "sample");
    CHECK_TRUE(re.succeeded());
    if (re.succeeded()) {
        auto conv = smdToAsset(re.value(), "sample");
        CHECK_TRUE(conv.succeeded());
        if (conv.succeeded()) {
            ValidationReport report;
            validateMeshWeights(conv.value().mesh, conv.value().skeleton.bones.size(), "sample",
                                report);
            CHECK_FALSE(report.exportBlocked());
        }
    }
    return failures;
}

M2RIG_TEST(smd, rejects_malformed) {
    int failures = 0;
    CHECK_FALSE(parseSmd("not an smd file", "bad").succeeded());
    CHECK_FALSE(parseSmd("version 1\nnodes\n  0 \"A\" -1\n", "truncated").succeeded());
    CHECK_FALSE(parseSmd("version 1\nnodes\n  bad line\nend\nskeleton\ntime 0\n  0 0 0 0 0 0 0\nend\ntriangles\nend\n", "badnode").succeeded());
    CHECK_FALSE(parseSmd("version 1\nnodes\n  0 \"A\" -1\n  1 \"B\" 7\nend\nskeleton\ntime 0\n  0 0 0 0 0 0 0\nend\ntriangles\nend\n", "badparent").succeeded());
    // Triangle section ending mid-triangle.
    CHECK_FALSE(parseSmd("version 1\nnodes\n  0 \"A\" -1\nend\nskeleton\ntime 0\n  0 0 0 0 0 0 0\nend\ntriangles\nmat.dds\n0 0 0 0 0 0 1 0 0 1 0 1.0\nend\n", "midtri").succeeded());
    // Unknown bone reference in triangle.
    CHECK_FALSE(parseSmd("version 1\nnodes\n  0 \"A\" -1\nend\nskeleton\ntime 0\n  0 0 0 0 0 0 0\nend\ntriangles\nmat.dds\n9 0 0 0 0 0 1 0 0 0\n9 1 0 0 0 0 1 0 0 0\n9 0 1 0 0 0 1 0 0 0\nend\n", "badbone").succeeded());
    // Non-contiguous file bone ids convert fine.
    const char* sparse = "version 1\nnodes\n  0 \"A\" -1\n  5 \"B\" 0\nend\nskeleton\ntime 0\n  0 0 0 0 0 0 0\n  5 0 1 0 0 0 0\nend\ntriangles\nm.dds\n5 0 0 0 0 0 1 0 0 0\n5 1 0 0 0 0 1 0 0 0\n5 0 1 0 0 0 1 0 0 0\nend\n";
    auto sp = parseSmd(sparse, "sparse");
    CHECK_TRUE(sp.succeeded());
    if (sp.succeeded()) {
        auto conv = smdToAsset(sp.value(), "sparse");
        CHECK_TRUE(conv.succeeded());
        if (conv.succeeded()) CHECK_EQ(conv.value().skeleton.bones.size(), 2u);
    }
    return failures;
}

M2RIG_TEST(smd, file_io_roundtrip) {
    int failures = 0;
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "m2rig_smd_test.smd";
    CHECK_TRUE(writeTextFile(tmp.string(), kTwoBoneSmd, "t").succeeded());
    auto back = readTextFile(tmp.string(), "t");
    CHECK_TRUE(back.succeeded());
    if (back.succeeded()) CHECK_TRUE(back.value() == kTwoBoneSmd);
    CHECK_FALSE(readTextFile((tmp.string() + ".missing").c_str(), "t").succeeded());
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return failures;
}

M2RIG_TEST(smd, pose_frame_changes_transforms) {
    int failures = 0;
    auto parsed = parseSmd(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    auto conv = smdToAsset(parsed.value(), "two-bone");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    Skeleton bind = conv.value().skeleton;
    CHECK_TRUE(poseSkeletonFromFrame(conv.value().skeleton, conv.value().frames, 1).succeeded());
    const Vec3& a = bind.bones[1].localRotationEuler;
    const Vec3& b = conv.value().skeleton.bones[1].localRotationEuler;
    CHECK_TRUE(std::fabs(a.x - b.x) > 1e-6);
    CHECK_TRUE(poseSkeletonFromFrame(conv.value().skeleton, conv.value().frames, 0).succeeded());
    CHECK_NEAR(conv.value().skeleton.bones[1].localRotationEuler.x, a.x, 1e-6);
    CHECK_FALSE(poseSkeletonFromFrame(conv.value().skeleton, conv.value().frames, 99).succeeded());
    return failures;
}

// Real Noesis FBX-bridge output: 90-bone ninja excerpt (nodes + time 0 +
// 2 triangles) stored offline in tests/ninja_excerpt.inc. Proves the native
// parser handles production skeletons (Spine2, fingers, toes, ponytail,
// equip_left/right) instead of only synthetic two-bone fixtures.
#include "ninja_excerpt.inc"

M2RIG_TEST(smd, parses_real_ninja_bridge_excerpt) {
    int failures = 0;
    auto parsed = parseSmd(kNinjaExcerptSmd, "ninja-excerpt");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    CHECK_EQ(parsed.value().bones.size(), 90u);
    auto conv = smdToAsset(parsed.value(), "ninja-excerpt");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    CHECK_EQ(conv.value().skeleton.bones.size(), 90u);
    CHECK_TRUE(conv.value().mesh.triangleCount() == 2u);
    // Socket bones from the real model resolve in the profile.
    const SkeletonProfile* prof = findProfile("pc_warrior");
    CHECK_TRUE(prof != nullptr);
    if (prof) {
        CHECK_TRUE(conv.value().skeleton.findByName("equip_left") != nullptr);
        CHECK_TRUE(conv.value().skeleton.findByName("equip_right") != nullptr);
        CHECK_TRUE(conv.value().skeleton.findByName("Bip01 Spine2") != nullptr);
        ValidationReport report;
        validateAgainstProfile(conv.value().skeleton, *prof, "ninja-excerpt", report);
        bool missingCore = false;
        for (const auto& it : report.items())
            if (it.id == "PROFILE_MISSING_BONE") missingCore = true;
        CHECK_FALSE(missingCore);
    }
    return failures;
}
