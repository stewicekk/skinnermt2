// SMD reader/writer/round-trip tests (spec section 21: real files only,
// differences reported, never faked).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "../tests/expect.hpp"
#include "m2rig/anim.hpp"
#include "m2rig/ast/msm_ast.hpp"
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

// Inline copy of tests/data/sample.msm (same 2-space indent dialect the AST
// parser measures). Used only when the working directory cannot reach the
// fixture file; the shell test prefers the real file.
const char* kSampleMsmInline =
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

M2RIG_TEST(smd, key_lerp_midpoint) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
    AnimKey k1 = k0;
    k1.frame = 10;
    k1.pos[0] = k1.pos[0] + Vec3{10, 0, 0};
    addKey(clip, k0);
    addKey(clip, k1);
    Skeleton posed = sample.value().skeleton;
    CHECK_TRUE(sampleClip(clip, posed, 5.0));
    const Vec3 expect = k0.pos[0] + (k1.pos[0] - k0.pos[0]) * 0.5f;
    CHECK_NEAR(posed.bones[0].localPosition.x, expect.x, 1e-4);
    CHECK_NEAR(posed.bones[0].localPosition.y, expect.y, 1e-4);
    CHECK_NEAR(posed.bones[0].localPosition.z, expect.z, 1e-4);
    return failures;
}

M2RIG_TEST(smd, key_euler_shortest_path) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
    AnimKey k1 = k0;
    k1.frame = 10;
    k0.rot[0].z = 3.0f;
    k1.rot[0].z = -3.0f;
    addKey(clip, k0);
    addKey(clip, k1);
    Skeleton posed = sample.value().skeleton;
    CHECK_TRUE(sampleClip(clip, posed, 5.0));
    // Wrapped delta (-6 + 2pi) halves to pi, not the naive 0.0.
    CHECK_NEAR(posed.bones[0].localRotationEuler.z, 3.14159265f, 1e-3);
    return failures;
}

M2RIG_TEST(smd, key_add_replace_delete_and_clamp) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    CHECK_EQ(clipLastFrame(clip), -1);
    Skeleton skel = sample.value().skeleton;
    addKey(clip, capturePoseKey(skel, 10));
    addKey(clip, capturePoseKey(skel, 0));
    CHECK_EQ(clip.keys.size(), 2u);
    CHECK_EQ(clip.keys[0].frame, 0);  // sorted on insert
    CHECK_EQ(clipLastFrame(clip), 10);
    addKey(clip, capturePoseKey(skel, 0));  // re-key replaces
    CHECK_EQ(clip.keys.size(), 2u);
    CHECK_TRUE(removeKeyAt(clip, 0));
    CHECK_FALSE(removeKeyAt(clip, 0));
    CHECK_EQ(clipLastFrame(clip), 10);
    // Clamp: before-first and after-last sample the end keys exactly.
    skel.bones[0].localPosition = {999, 999, 999};
    CHECK_TRUE(sampleClip(clip, skel, -5.0));
    CHECK_NEAR(skel.bones[0].localPosition.x, clip.keys[0].pos[0].x, 1e-5);
    CHECK_TRUE(sampleClip(clip, skel, 100.0));
    CHECK_NEAR(skel.bones[0].localPosition.x, clip.keys[0].pos[0].x, 1e-5);
    // Empty clip samples nothing and bakes nothing.
    AnimClip empty;
    CHECK_FALSE(sampleClip(empty, skel, 0.0));
    CHECK_TRUE(bakeClipFrames(empty).empty());
    return failures;
}

M2RIG_TEST(smd, key_bake_roundtrips_through_smd) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    addKey(clip, capturePoseKey(sample.value().skeleton, 0));
    AnimKey k2 = capturePoseKey(sample.value().skeleton, 2);
    k2.pos[1] = k2.pos[1] + Vec3{0, 1, 0};
    addKey(clip, k2);
    const std::vector<SmdFrame> baked = bakeClipFrames(clip);
    CHECK_EQ(baked.size(), 3u);
    for (std::size_t i = 0; i < baked.size(); ++i) {
        CHECK_EQ(baked[i].time, static_cast<int>(i));
        CHECK_EQ(baked[i].poses.size(), sample.value().skeleton.bones.size());
    }
    // In-between frame 1 carries the interpolated lift.
    CHECK_NEAR(baked[1].poses[1].position.y, sample.value().skeleton.bones[1].localPosition.y + 0.5f,
               1e-4);
    auto written = writeSmd(sample.value().mesh, sample.value().skeleton, baked);
    CHECK_TRUE(written.succeeded());
    if (!written.succeeded()) return failures + 1;
    auto back = parseSmd(written.value().text, "baked");
    CHECK_TRUE(back.succeeded());
    if (!back.succeeded()) return failures + 1;
    CHECK_EQ(back.value().frames.size(), 3u);
    return failures;
}

// Wave 28: quaternion sampling. All new tests compare ROTATION MATRICES
// (via Mat4::rotationEulerXyz) rather than raw euler triples, because the
// slerp->matrix->euler boundary may fold equivalent angles (2pi wraps,
// gimbal-pole x/z folding) while the posed matrices must stay exact.

M2RIG_TEST(smd, slerp_midpoint_matches_short_arc) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    // Yaw 0 -> 90 deg: midpoint must be 45 deg about Y (short arc).
    {
        AnimClip clip;
        AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
        CHECK_EQ(k0.quat.size(), k0.rot.size());
        AnimKey k1 = k0;
        k1.frame = 10;
        k0.rot[0] = Vec3{0, 0, 0};
        k1.rot[0] = Vec3{0, kPi * 0.5f, 0};
        addKey(clip, k0);
        addKey(clip, k1);
        // addKey re-syncs the parallel cache from rot.
        CHECK_EQ(clip.keys[0].quat.size(), clip.keys[0].rot.size());
        Skeleton posed = sample.value().skeleton;
        CHECK_TRUE(sampleClip(clip, posed, 5.0));
        CHECK_NEAR(posed.bones[0].localRotationEuler.y, kPi * 0.25f, 1e-3);
        const Mat4 m = Mat4::rotationEulerXyz(posed.bones[0].localRotationEuler);
        const Mat4 expect = Mat4::rotationY(kPi * 0.25f);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) CHECK_NEAR(m.m[r][c], expect.m[r][c], 1e-3);
    }
    // Roll wrap 3.0 -> -3.0: shortest arc halves to pi, not naive 0.
    {
        AnimClip clip;
        AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
        AnimKey k1 = k0;
        k1.frame = 10;
        k0.rot[0] = Vec3{0, 0, 3.0f};
        k1.rot[0] = Vec3{0, 0, -3.0f};
        addKey(clip, k0);
        addKey(clip, k1);
        Skeleton posed = sample.value().skeleton;
        CHECK_TRUE(sampleClip(clip, posed, 5.0));
        CHECK_NEAR(posed.bones[0].localRotationEuler.z, kPi, 1e-3);
        const Mat4 m = Mat4::rotationEulerXyz(posed.bones[0].localRotationEuler);
        const Mat4 expect = Mat4::rotationZ(kPi);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) CHECK_NEAR(m.m[r][c], expect.m[r][c], 1e-3);
    }
    return failures;
}

M2RIG_TEST(smd, wrap_pi_parity_with_quat) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    // Local copy of the retired euler wrapDelta: reference for single-axis parity.
    const auto wrapDelta = [](float d) {
        d -= std::round(d / (2.0f * kPi)) * 2.0f * kPi;
        return d;
    };
    const auto oldLerp = [&](float a, float b, double t) {
        return a + wrapDelta(b - a) * static_cast<float>(t);
    };
    struct AxisCase {
        int axis;  // 0=x, 1=y, 2=z
        float a;
        float b;
    };
    // X/Z wraps exercise the pi branch; Y uses a small non-wrapping span to
    // stay clear of the y=pi euler folding (same matrix, different triple).
    const AxisCase cases[] = {{0, 3.0f, -3.0f}, {2, 3.0f, -3.0f}, {1, 0.2f, 0.8f},
                              {0, 0.2f, 0.5f},  {2, -0.4f, 0.7f}};
    const double ts[] = {0.25, 0.5, 0.75};
    for (const auto& cs : cases) {
        for (double t : ts) {
            AnimClip clip;
            AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
            AnimKey k1 = k0;
            k1.frame = 10;
            k0.rot[0] = Vec3{0, 0, 0};
            k1.rot[0] = Vec3{0, 0, 0};
            k0.rot[0][static_cast<std::size_t>(cs.axis)] = cs.a;
            k1.rot[0][static_cast<std::size_t>(cs.axis)] = cs.b;
            addKey(clip, k0);
            addKey(clip, k1);
            Skeleton posed = sample.value().skeleton;
            CHECK_TRUE(sampleClip(clip, posed, t * 10.0));
            const float expect = oldLerp(cs.a, cs.b, t);
            const float got = posed.bones[0].localRotationEuler[static_cast<std::size_t>(cs.axis)];
            // Same rotation, possibly different ±pi branch: the quat return
            // path normalizes via eulerXyzFromRotation while raw lerp does
            // not, so compare modulo 2*pi (got-expect = -3.07 vs 3.21 at
            // t=0.75 differs by exactly 2*pi).
            CHECK_NEAR(wrapDelta(got - expect), 0.0f, 1e-3);
        }
    }
    return failures;
}

M2RIG_TEST(smd, double_cover_canonical) {
    int failures = 0;
    // q and -q name the same rotation: full-turn euler gives opposite cover.
    const Quat qa = Quat::fromEulerXyz(Vec3{0, 0, 0});
    const Quat qb = Quat::fromEulerXyz(Vec3{0, 0, 2.0f * kPi});
    const float dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    CHECK_NEAR(std::fabs(dot), 1.0, 1e-5);
    // Slerp across opposite covers must not spin: midpoint stays identity.
    const Quat mid = Quat::slerp(qa, qb, 0.5f).normalized();
    const Mat4 m = mid.toMatrix();
    const Mat4 ident = Mat4::identity();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK_NEAR(m.m[r][c], ident.m[r][c], 1e-5);
    // Explicit sign flip canonicalizes to the same matrix.
    const Quat qbNeg{-qb.x, -qb.y, -qb.z, -qb.w};
    const Mat4 m1 = Quat::slerp(qa, qb, 0.3f).normalized().toMatrix();
    const Mat4 m2 = Quat::slerp(qa, qbNeg, 0.3f).normalized().toMatrix();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK_NEAR(m1.m[r][c], m2.m[r][c], 1e-5);
    // Clip-level: 0 -> 2pi on Z samples to identity at the midpoint.
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
    AnimKey k1 = k0;
    k1.frame = 10;
    k0.rot[0] = Vec3{0, 0, 0};
    k1.rot[0] = Vec3{0, 0, 2.0f * kPi};
    addKey(clip, k0);
    addKey(clip, k1);
    Skeleton posed = sample.value().skeleton;
    CHECK_TRUE(sampleClip(clip, posed, 5.0));
    const Mat4 pm = Mat4::rotationEulerXyz(posed.bones[0].localRotationEuler);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK_NEAR(pm.m[r][c], ident.m[r][c], 1e-3);
    return failures;
}

M2RIG_TEST(smd, gimbal_pole_stable) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
    AnimKey k1 = k0;
    k1.frame = 10;
    // Straddle the Y pole (pi/2): euler may fold z into x, matrices must not.
    k0.rot[0] = Vec3{0.3f, kPi * 0.5f - 0.05f, 0.1f};
    k1.rot[0] = Vec3{-0.2f, kPi * 0.5f + 0.05f, -0.1f};
    addKey(clip, k0);
    addKey(clip, k1);
    const double ts[] = {2.5, 5.0, 7.5};
    for (double f : ts) {
        Skeleton posed = sample.value().skeleton;
        CHECK_TRUE(sampleClip(clip, posed, f));
        const Vec3& e = posed.bones[0].localRotationEuler;
        CHECK_TRUE(std::isfinite(e.x));
        CHECK_TRUE(std::isfinite(e.y));
        CHECK_TRUE(std::isfinite(e.z));
        const Mat4 m = Mat4::rotationEulerXyz(e);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) CHECK_TRUE(std::isfinite(m.m[r][c]));
        // Rows orthonormal: unit length, mutually orthogonal.
        for (int r = 0; r < 3; ++r) {
            const double len = std::sqrt(static_cast<double>(m.m[r][0] * m.m[r][0] +
                                                              m.m[r][1] * m.m[r][1] +
                                                              m.m[r][2] * m.m[r][2]));
            CHECK_NEAR(len, 1.0, 1e-4);
        }
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b) {
                const double d = static_cast<double>(m.m[a][0] * m.m[b][0] +
                                                     m.m[a][1] * m.m[b][1] + m.m[a][2] * m.m[b][2]);
                CHECK_NEAR(d, 0.0, 1e-4);
            }
    }
    return failures;
}

M2RIG_TEST(smd, bake_matches_preview_exactly) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    AnimKey k0 = capturePoseKey(sample.value().skeleton, 0);
    AnimKey k1 = k0;
    AnimKey k2 = k0;
    k1.frame = 5;
    k2.frame = 10;
    k0.rot[0] = Vec3{0.1f, 0.2f, 0.0f};
    k1.rot[0] = Vec3{0.4f, -0.3f, 0.9f};
    k2.rot[0] = Vec3{-0.5f, 0.7f, 1.4f};
    k1.pos[0] = k1.pos[0] + Vec3{1, 0, 0};
    k2.pos[0] = k2.pos[0] + Vec3{2, 1, 0};
    k1.rot[1] = k1.rot[1] + Vec3{0, 0, 0.6f};
    k2.rot[1] = k2.rot[1] + Vec3{0.3f, 0, 0};
    addKey(clip, k0);
    addKey(clip, k1);
    addKey(clip, k2);
    const std::vector<SmdFrame> baked = bakeClipFrames(clip);
    CHECK_EQ(baked.size(), 11u);
    if (baked.size() != 11u) return failures + 1;
    // Every integer frame: baked pose must equal preview matrices.
    for (int f = 0; f <= 10; ++f) {
        Skeleton posed = sample.value().skeleton;
        CHECK_TRUE(sampleClip(clip, posed, static_cast<double>(f)));
        const std::size_t nb = sample.value().skeleton.bones.size();
        CHECK_EQ(baked[static_cast<std::size_t>(f)].poses.size(), nb);
        for (std::size_t b = 0; b < nb; ++b) {
            const Vec3& bp = baked[static_cast<std::size_t>(f)].poses[b].position;
            CHECK_NEAR(bp.x, posed.bones[b].localPosition.x, 1e-5);
            CHECK_NEAR(bp.y, posed.bones[b].localPosition.y, 1e-5);
            CHECK_NEAR(bp.z, posed.bones[b].localPosition.z, 1e-5);
            const Mat4 bm = Mat4::rotationEulerXyz(baked[static_cast<std::size_t>(f)].poses[b].rotation);
            const Mat4 pm = Mat4::rotationEulerXyz(posed.bones[b].localRotationEuler);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) CHECK_NEAR(bm.m[r][c], pm.m[r][c], 1e-4);
        }
    }
    // Every half frame: preview matrices must equal the direct slerp
    // reference (same per-bone helper, matrix compare so folds agree).
    for (int f = 0; f < 10; ++f) {
        const double h = static_cast<double>(f) + 0.5;
        Skeleton posed = sample.value().skeleton;
        CHECK_TRUE(sampleClip(clip, posed, h));
        // Manual reference: locate the segment linearly, then slerp.
        const AnimKey* pa = nullptr;
        const AnimKey* pb = nullptr;
        double t = 0.0;
        for (std::size_t k = 1; k < clip.keys.size(); ++k) {
            if (h <= static_cast<double>(clip.keys[k].frame)) {
                pa = &clip.keys[k - 1];
                pb = &clip.keys[k];
                const double span =
                    static_cast<double>(pb->frame - pa->frame);
                t = span > 0.0 ? (h - pa->frame) / span : 0.0;
                break;
            }
        }
        CHECK_TRUE(pa != nullptr);
        if (!pa) continue;
        for (std::size_t b = 0; b < sample.value().skeleton.bones.size(); ++b) {
            const Quat qa = Quat::fromEulerXyz(pa->rot[b]);
            const Quat qb = Quat::fromEulerXyz(pb->rot[b]);
            const Mat4 ref = Quat::slerp(qa, qb, static_cast<float>(t)).normalized().toMatrix();
            const Mat4 got = Mat4::rotationEulerXyz(posed.bones[b].localRotationEuler);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) CHECK_NEAR(got.m[r][c], ref.m[r][c], 1e-4);
        }
    }
    return failures;
}

M2RIG_TEST(smd, binary_search_parity) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    // Dense 64-key clip: frames 0..63 with distinct pos/rot per key.
    AnimClip clip;
    const std::size_t nb = sample.value().skeleton.bones.size();
    for (int f = 0; f < 64; ++f) {
        AnimKey k = capturePoseKey(sample.value().skeleton, f);
        for (std::size_t b = 0; b < nb; ++b) {
            k.pos[b] = k.pos[b] + Vec3{static_cast<float>(f) * 0.1f, 0, 0};
            k.rot[b] = k.rot[b] + Vec3{0, static_cast<float>(f) * 0.02f,
                                       static_cast<float>(f) * 0.05f};
        }
        addKey(clip, k);
    }
    CHECK_EQ(clip.keys.size(), 64u);
    // Reference: old linear-scan segment lookup + identical quat math.
    const auto reference = [&](double frame, std::vector<Vec3>& outPos, std::vector<Vec3>& outEuler) {
        if (clip.keys.size() == 1 || frame <= clip.keys.front().frame) {
            outPos = clip.keys.front().pos;
            outEuler = clip.keys.front().rot;
            return;
        }
        if (frame >= clip.keys.back().frame) {
            outPos = clip.keys.back().pos;
            outEuler = clip.keys.back().rot;
            return;
        }
        std::size_t hi = 1;
        while (hi + 1 < clip.keys.size() &&
               static_cast<double>(clip.keys[hi].frame) < frame)
            ++hi;
        const AnimKey& a = clip.keys[hi - 1];
        const AnimKey& b = clip.keys[hi];
        const double span = static_cast<double>(b.frame - a.frame);
        const double t = span > 0.0 ? (frame - static_cast<double>(a.frame)) / span : 0.0;
        const float tf = static_cast<float>(t);
        outPos.resize(nb);
        outEuler.resize(nb);
        for (std::size_t i = 0; i < nb; ++i) {
            outPos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * tf;
            const Quat qa = Quat::fromEulerXyz(a.rot[i]);
            const Quat qb = Quat::fromEulerXyz(b.rot[i]);
            outEuler[i] = Quat::slerp(qa, qb, tf).normalized().toMatrix().eulerXyzFromRotation();
        }
    };
    const double probes[] = {-5.0, 0.0, 0.5, 1.0, 1.5, 10.25, 17.3,
                             31.0, 32.0, 32.5, 45.7, 62.5, 63.0, 100.0};
    for (double f : probes) {
        Skeleton posed = sample.value().skeleton;
        CHECK_TRUE(sampleClip(clip, posed, f));
        std::vector<Vec3> refPos;
        std::vector<Vec3> refEuler;
        reference(f, refPos, refEuler);
        for (std::size_t b = 0; b < nb; ++b) {
            CHECK_NEAR(posed.bones[b].localPosition.x, refPos[b].x, 1e-5);
            CHECK_NEAR(posed.bones[b].localPosition.y, refPos[b].y, 1e-5);
            CHECK_NEAR(posed.bones[b].localPosition.z, refPos[b].z, 1e-5);
            const Mat4 got = Mat4::rotationEulerXyz(posed.bones[b].localRotationEuler);
            const Mat4 want = Mat4::rotationEulerXyz(refEuler[b]);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) CHECK_NEAR(got.m[r][c], want.m[r][c], 1e-4);
        }
    }
    // Exhaustive exact-key + half sweep proves no off-by-one in the search.
    for (int f = 0; f < 64; ++f) {
        for (int half = 0; half < 2; ++half) {
            const double frame = static_cast<double>(f) + (half != 0 ? 0.5 : 0.0);
            if (frame > 63.0) continue;
            Skeleton posed = sample.value().skeleton;
            CHECK_TRUE(sampleClip(clip, posed, frame));
            std::vector<Vec3> refPos;
            std::vector<Vec3> refEuler;
            reference(frame, refPos, refEuler);
            for (std::size_t b = 0; b < nb; ++b) {
                const Mat4 got = Mat4::rotationEulerXyz(posed.bones[b].localRotationEuler);
                const Mat4 want = Mat4::rotationEulerXyz(refEuler[b]);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) CHECK_NEAR(got.m[r][c], want.m[r][c], 1e-4);
            }
        }
    }
    return failures;
}

// Wave 28 compression: stored representation only (bakeClipFrames untouched).

M2RIG_TEST(smd, key_reduction_lossless_on_linear) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const std::size_t nb = sample.value().skeleton.bones.size();
    AnimClip clip;
    for (int f = 0; f < 5; ++f) {
        AnimKey k = capturePoseKey(sample.value().skeleton, f);
        const float ff = static_cast<float>(f);
        for (std::size_t b = 0; b < nb; ++b) k.pos[b] = k.pos[b] + Vec3{ff * 1.0f, 0.0f, 0.0f};
        addKey(clip, k);
    }
    CHECK_EQ(clip.keys.size(), 5u);
    AnimCompressStats stats;
    const AnimClip out = reduceClipKeys(clip, 1e-4f, 1e-4f, &stats);
    CHECK_EQ(stats.keysIn, 5u);
    CHECK_EQ(stats.keysOut, 2u);
    CHECK_EQ(out.keys.size(), 2u);
    if (out.keys.size() != 2u) return failures + 1;
    CHECK_EQ(out.keys.front().frame, 0);
    CHECK_EQ(out.keys.back().frame, 4);
    CHECK_NEAR(stats.maxPosErrMeasured, 0.0, 1e-4);
    CHECK_NEAR(stats.maxAngErrMeasured, 0.0, 1e-4);
    return failures;
}

M2RIG_TEST(smd, key_reduction_respects_bounds) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimClip clip;
    for (int f = 0; f < 5; ++f) {
        AnimKey k = capturePoseKey(sample.value().skeleton, f);
        const float ff = static_cast<float>(f);
        const float bump = (f == 2) ? 1.0f : 0.0f;
        const float rotBump = (f == 2) ? 0.5f : 0.0f;
        k.pos[0] = k.pos[0] + Vec3{ff * 1.0f, bump, 0.0f};
        k.rot[1] = k.rot[1] + Vec3{0.0f, 0.0f, rotBump};
        addKey(clip, k);
    }
    CHECK_EQ(clip.keys.size(), 5u);
    AnimCompressStats loose;
    AnimCompressStats tight;
    const AnimClip outLoose = reduceClipKeys(clip, 1.5f, 1.0f, &loose);
    const AnimClip outTight = reduceClipKeys(clip, 0.05f, 0.05f, &tight);
    CHECK_EQ(loose.keysIn, 5u);
    CHECK_EQ(tight.keysIn, 5u);
    CHECK_EQ(outLoose.keys.size(), 2u);
    CHECK_TRUE(outTight.keys.size() >= 3u);
    CHECK_TRUE(outTight.keys.size() >= outLoose.keys.size());
    CHECK_TRUE(loose.maxPosErrMeasured <= 1.5f + 1e-5f);
    CHECK_TRUE(loose.maxAngErrMeasured <= 1.0f + 1e-5f);
    CHECK_TRUE(tight.maxPosErrMeasured <= 0.05f + 1e-4f);
    CHECK_TRUE(tight.maxAngErrMeasured <= 0.05f + 1e-4f);
    CHECK_EQ(outLoose.keys.front().frame, 0);
    CHECK_EQ(outLoose.keys.back().frame, 4);
    CHECK_EQ(outTight.keys.front().frame, 0);
    CHECK_EQ(outTight.keys.back().frame, 4);
    return failures;
}

M2RIG_TEST(smd, smallest_three_error_bound) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    AnimKey k = capturePoseKey(sample.value().skeleton, 0);
    const std::size_t nb = sample.value().skeleton.bones.size();
    CHECK_TRUE(k.rot.size() == nb);
    if (k.rot.size() != nb) return failures + 1;
    k.rot[0] = Vec3{0.3f, 0.5f, -0.2f};
    if (nb > 1u) k.rot[1] = Vec3{0.0f, 0.0f, 3.0f};
    float maxAng = 0.0f;
    for (std::size_t b = 0; b < nb; ++b) {
        const Quat q = Quat::fromEulerXyz(k.rot[b]);
        const SmallestThreeQuat qq = quantizeQuatSmallestThree(q);
        CHECK_EQ(qq.bits, 14);
        CHECK_TRUE(static_cast<int>(qq.largest) <= 3);
        const Quat d = dequantizeQuatSmallestThree(qq);
        CHECK_TRUE(std::isfinite(d.x));
        CHECK_TRUE(std::isfinite(d.y));
        CHECK_TRUE(std::isfinite(d.z));
        CHECK_TRUE(std::isfinite(d.w));
        CHECK_TRUE(d.w >= 0.0f);
        const float e = quatAngularError(q, d);
        if (e > maxAng) maxAng = e;
    }
    // 14-bit step is 2/16383; worst vector error ~1.1e-4 => angle ~2.2e-4.
    // 0.01 rad is generous headroom, tight enough to catch a broken codec.
    CHECK_TRUE(maxAng <= 0.01f);
    const Quat qneg{0.0f, 0.0f, 0.0f, -1.0f};
    const SmallestThreeQuat qn = quantizeQuatSmallestThree(qneg);
    const Quat dn = dequantizeQuatSmallestThree(qn);
    CHECK_TRUE(dn.w >= 0.0f);
    CHECK_NEAR(quatAngularError(qneg, dn), 0.0, 1e-3);
    return failures;
}

M2RIG_TEST(smd, position_range_error_bound) {
    int failures = 0;
    std::vector<Vec3> track;
    for (int i = 0; i < 9; ++i) {
        const float fi = static_cast<float>(i);
        track.push_back(Vec3{fi * 0.5f,
                             static_cast<float>(std::sin(static_cast<double>(fi) * 0.5)),
                             static_cast<float>(std::cos(static_cast<double>(fi) * 0.3))});
    }
    const QuantizedPosTrack qt = quantizePosTrack(track);
    CHECK_EQ(qt.bits, 16);
    CHECK_EQ(qt.codes.size(), track.size());
    const std::vector<Vec3> back = dequantizePosTrack(qt);
    CHECK_EQ(back.size(), track.size());
    if (back.size() != track.size()) return failures + 1;
    float maxErr = 0.0f;
    for (std::size_t i = 0; i < track.size(); ++i) {
        const float e = distance(track[i], back[i]);
        if (e > maxErr) maxErr = e;
    }
    const float rX = qt.max.x - qt.min.x;
    const float rY = qt.max.y - qt.min.y;
    const float rZ = qt.max.z - qt.min.z;
    float maxRange = rX;
    if (rY > maxRange) maxRange = rY;
    if (rZ > maxRange) maxRange = rZ;
    const float bound =
        static_cast<float>(std::sqrt(3.0) * static_cast<double>(maxRange) / 65535.0 * 0.5 + 1e-6);
    CHECK_TRUE(maxErr <= bound + 1e-6f);
    // Degenerate constant track: exact, no division.
    const std::vector<Vec3> constant(4u, Vec3{1.0f, 2.0f, 3.0f});
    const QuantizedPosTrack qc = quantizePosTrack(constant);
    const std::vector<Vec3> bc = dequantizePosTrack(qc);
    CHECK_EQ(bc.size(), 4u);
    for (std::size_t i = 0; i < bc.size(); ++i) {
        CHECK_NEAR(bc[i].x, 1.0, 1e-6);
        CHECK_NEAR(bc[i].y, 2.0, 1e-6);
        CHECK_NEAR(bc[i].z, 3.0, 1e-6);
    }
    // Param 12-20: finer grid must not be worse than coarse.
    const QuantizedPosTrack q12 = quantizePosTrack(track, 12);
    const QuantizedPosTrack q20 = quantizePosTrack(track, 20);
    CHECK_EQ(q12.bits, 12);
    CHECK_EQ(q20.bits, 20);
    const std::vector<Vec3> b12 = dequantizePosTrack(q12);
    const std::vector<Vec3> b20 = dequantizePosTrack(q20);
    float max12 = 0.0f;
    float max20 = 0.0f;
    for (std::size_t i = 0; i < track.size(); ++i) {
        const float e12 = distance(track[i], b12[i]);
        const float e20 = distance(track[i], b20[i]);
        if (e12 > max12) max12 = e12;
        if (e20 > max20) max20 = e20;
    }
    CHECK_TRUE(max20 <= max12 + 1e-6f);
    return failures;
}

M2RIG_TEST(smd, ani_export_bytes_pinned) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const Skeleton& skel = sample.value().skeleton;
    const std::size_t boneCount = skel.bones.size();
    AnimClip clip;
    addKey(clip, capturePoseKey(skel, 0));
    AnimKey k1 = capturePoseKey(skel, 1);
    k1.pos[0] = k1.pos[0] + Vec3{1.0f, 0.0f, 0.0f};
    addKey(clip, k1);
    auto res = exportAni(clip, skel, 30.0f);
    CHECK_TRUE(res.succeeded());
    if (!res.succeeded()) return failures + 1;
    const std::string& bytes = res.value();
    const std::size_t expectLen = 20u + 2u * boneCount * 36u;
    CHECK_EQ(bytes.size(), expectLen);
    if (bytes.size() < expectLen) return failures + 1;
    CHECK_TRUE(bytes[0] == 'A');
    CHECK_TRUE(bytes[1] == 'N');
    CHECK_TRUE(bytes[2] == 'I');
    CHECK_TRUE(bytes[3] == ' ');
    std::uint32_t version = 0;
    std::uint32_t fcount = 0;
    std::uint32_t bcount = 0;
    float fps = 0.0f;
    std::memcpy(&version, bytes.data() + 4, sizeof(version));
    std::memcpy(&fcount, bytes.data() + 8, sizeof(fcount));
    std::memcpy(&bcount, bytes.data() + 12, sizeof(bcount));
    std::memcpy(&fps, bytes.data() + 16, sizeof(fps));
    CHECK_EQ(version, 1u);
    CHECK_EQ(fcount, 2u);
    CHECK_EQ(bcount, static_cast<std::uint32_t>(boneCount));
    CHECK_NEAR(fps, 30.0, 1e-5);
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    float rz = 0.0f;
    float sx = 0.0f;
    float sy = 0.0f;
    float sz = 0.0f;
    std::memcpy(&px, bytes.data() + 20, sizeof(px));
    std::memcpy(&py, bytes.data() + 24, sizeof(py));
    std::memcpy(&pz, bytes.data() + 28, sizeof(pz));
    std::memcpy(&rx, bytes.data() + 32, sizeof(rx));
    std::memcpy(&ry, bytes.data() + 36, sizeof(ry));
    std::memcpy(&rz, bytes.data() + 40, sizeof(rz));
    std::memcpy(&sx, bytes.data() + 44, sizeof(sx));
    std::memcpy(&sy, bytes.data() + 48, sizeof(sy));
    std::memcpy(&sz, bytes.data() + 52, sizeof(sz));
    CHECK_NEAR(px, skel.bones[0].localPosition.x, 1e-5);
    CHECK_NEAR(py, skel.bones[0].localPosition.y, 1e-5);
    CHECK_NEAR(pz, skel.bones[0].localPosition.z, 1e-5);
    CHECK_NEAR(rx, skel.bones[0].localRotationEuler.x * kRadToDeg, 1e-4);
    CHECK_NEAR(ry, skel.bones[0].localRotationEuler.y * kRadToDeg, 1e-4);
    CHECK_NEAR(rz, skel.bones[0].localRotationEuler.z * kRadToDeg, 1e-4);
    CHECK_NEAR(sx, skel.bones[0].localScale.x, 1e-5);
    CHECK_NEAR(sy, skel.bones[0].localScale.y, 1e-5);
    CHECK_NEAR(sz, skel.bones[0].localScale.z, 1e-5);
    return failures;
}

// MSM -> SMD lowering (msmToSmd): sample.msm carries 1 shape (material
// armor_body.dds), 2 bone refs and 1 weighted skin vertex, but no positions,
// normals, UVs or faces — so the honest output is a geometry shell (caller
// skeleton + bind frame + materials, zero triangles), never fabricated mesh.
M2RIG_TEST(smd, msm_to_smd_shell_preserves_materials_and_skeleton) {
    int failures = 0;
    // Real fixture first; inline text only as fallback.
    std::string msmText;
    bool fromFile = false;
    {
        std::filesystem::path dir = std::filesystem::current_path();
        for (int level = 0; level < 5 && !fromFile; ++level) {
            const std::filesystem::path c = dir / "tests" / "data" / "sample.msm";
            if (std::filesystem::exists(c)) {
                auto t = readTextFile(c.string(), "sample.msm");
                if (t.succeeded()) {
                    msmText = t.value();
                    fromFile = true;
                }
            }
            if (!dir.has_parent_path()) break;
            dir = dir.parent_path();
        }
    }
    if (!fromFile) msmText = kSampleMsmInline;
    MsmDocument doc;
    CHECK_TRUE(parseMsm(msmText, doc));
    // Caller skeleton: two-bone bind whose dense ids 0/1 match sample.msm.
    auto parsed = parseSmd(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    auto conv = smdToAsset(parsed.value(), "two-bone");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    const Skeleton& skel = conv.value().skeleton;

    auto lowered = msmToSmd(doc, skel, "sample");
    CHECK_TRUE(lowered.succeeded());
    if (!lowered.succeeded()) {
        printf("    msmToSmd failed: %s\n", lowered.error().message.c_str());
        return failures + 1;
    }
    const SmdDocument& out = lowered.value();
    // Materials preserved from the Shape Model ref.
    CHECK_EQ(out.materials.size(), 1u);
    if (!out.materials.empty()) CHECK_TRUE(out.materials[0] == "armor_body.dds");
    // Skeleton carried verbatim from the caller (no invented bones/binds).
    CHECK_EQ(out.bones.size(), skel.bones.size());
    for (std::size_t i = 0; i < out.bones.size() && i < skel.bones.size(); ++i) {
        CHECK_TRUE(out.bones[i].name == skel.bones[i].name);
        CHECK_TRUE(out.bones[i].parentId == skel.bones[i].parentId);
        CHECK_NEAR(out.bones[i].bindPosition.x, skel.bones[i].localPosition.x, 1e-6);
        CHECK_NEAR(out.bones[i].bindPosition.y, skel.bones[i].localPosition.y, 1e-6);
        CHECK_NEAR(out.bones[i].bindPosition.z, skel.bones[i].localPosition.z, 1e-6);
        CHECK_NEAR(out.bones[i].bindRotation.x, skel.bones[i].localRotationEuler.x, 1e-6);
        CHECK_NEAR(out.bones[i].bindRotation.y, skel.bones[i].localRotationEuler.y, 1e-6);
        CHECK_NEAR(out.bones[i].bindRotation.z, skel.bones[i].localRotationEuler.z, 1e-6);
    }
    CHECK_EQ(out.frames.size(), 1u);
    if (!out.frames.empty()) CHECK_EQ(out.frames[0].poses.size(), skel.bones.size());
    // Triangles intentionally empty (source has no faces/positions): pinning
    // this documents the shell scope and forbids silent geometry fabrication.
    CHECK_TRUE(out.triangles.empty());
    // The shell is a valid SMD document: converts and re-parses strictly.
    auto back = smdToAsset(out, "sample");
    CHECK_TRUE(back.succeeded());
    if (back.succeeded()) {
        auto written = writeSmd(back.value().mesh, back.value().skeleton, back.value().frames);
        CHECK_TRUE(written.succeeded());
        if (written.succeeded()) {
            auto re = parseSmd(written.value().text, "sample");
            CHECK_TRUE(re.succeeded());
            if (re.succeeded()) {
                CHECK_EQ(re.value().bones.size(), 2u);
                CHECK_TRUE(re.value().triangles.empty());
            }
        }
    }
    return failures;
}

M2RIG_TEST(smd, msm_to_smd_rejects_invalid_msm) {
    int failures = 0;
    auto parsed = parseSmd(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    auto conv = smdToAsset(parsed.value(), "two-bone");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    const Skeleton& skel = conv.value().skeleton;
    struct Case {
        const char* text;
        const char* wantInMessage;
    };
    const Case cases[] = {
        // ShapeCount 2 vs 0 Shape groups.
        {"Group ShapeDataT\n{\n  Group ShapeIndex\n  {\n    ShapeCount 2\n  }\n"
         "  Group SourceSkin\n  {\n    VertexCount 1\n    Vertex 0 0 1.0\n  }\n}\n",
         "MSM_SHAPE_COUNT"},
        // Shape0 without a Model ref.
        {"Group ShapeDataT\n{\n  Group ShapeIndex\n  {\n    ShapeCount 1\n    Group Shape0\n"
         "    {\n      SourceSkin \"s\"\n    }\n  }\n"
         "  Group SourceSkin\n  {\n    VertexCount 1\n    Vertex 0 0 1.0\n  }\n}\n",
         "MSM_SHAPE_REF"},
        // No SourceSkin group at all.
        {"Group ShapeDataT\n{\n  Group ShapeIndex\n  {\n    ShapeCount 0\n  }\n}\n",
         "MSM_NO_SKIN"},
        // Quoted-empty Model ref passes the counter but names no material.
        {"Group ShapeDataT\n{\n  Group ShapeIndex\n  {\n    ShapeCount 1\n    Group Shape0\n"
         "    {\n      Model \"\"\n      SourceSkin \"s\"\n    }\n  }\n"
         "  Group SourceSkin\n  {\n    VertexCount 1\n    Vertex 0 0 1.0\n  }\n}\n",
         "Model"},
    };
    for (const auto& c : cases) {
        MsmDocument doc;
        CHECK_TRUE(parseMsm(c.text, doc));
        auto r = msmToSmd(doc, skel, "t");
        CHECK_FALSE(r.succeeded());
        if (r.succeeded()) continue;
        if (r.error().message.find(c.wantInMessage) == std::string::npos) {
            printf("    missing '%s' in: %s\n", c.wantInMessage, r.error().message.c_str());
            ++failures;
        }
    }
    return failures;
}

M2RIG_TEST(smd, msm_to_smd_rejects_bad_skin_and_empty_skeleton) {
    int failures = 0;
    auto parsed = parseSmd(kTwoBoneSmd, "two-bone");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    auto conv = smdToAsset(parsed.value(), "two-bone");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    const Skeleton& skel = conv.value().skeleton;
    const auto withSkin = [](const std::string& skinLines) {
        return std::string("Group ShapeDataT\n{\n  Group ShapeIndex\n  {\n    ShapeCount 1\n"
                           "    Group Shape0\n    {\n      Model \"armor_body.dds\"\n"
                           "      SourceSkin \"s\"\n    }\n  }\n  Group SourceSkin\n  {\n") +
               skinLines + "  }\n}\n";
    };
    struct Case {
        std::string text;
        const char* wantInMessage;
    };
    const Case cases[] = {
        {withSkin("    VertexCount 1\n    Vertex 0 9 1.0\n"), "out-of-range"},
        {withSkin("    VertexCount 1\n    Vertex 0 0 abc\n"), "weight"},
        {withSkin("    VertexCount 1\n    Vertex 0 0 -0.5\n"), "negative"},
        {withSkin("    VertexCount 2\n    Vertex 0 0 1.0\n    Vertex 0 1 1.0\n"), "Duplicate"},
        {withSkin("    VertexCount 1\n    Vertex 0 0\n"), "pairs"},
    };
    for (const auto& c : cases) {
        MsmDocument doc;
        CHECK_TRUE(parseMsm(c.text, doc));
        auto r = msmToSmd(doc, skel, "t");
        CHECK_FALSE(r.succeeded());
        if (r.succeeded()) continue;
        if (r.error().message.find(c.wantInMessage) == std::string::npos) {
            printf("    missing '%s' in: %s\n", c.wantInMessage, r.error().message.c_str());
            ++failures;
        }
    }
    // Boneless call fails even for a fully valid MSM.
    {
        MsmDocument doc;
        CHECK_TRUE(parseMsm(kSampleMsmInline, doc));
        const Skeleton empty;
        auto r = msmToSmd(doc, empty, "t");
        CHECK_FALSE(r.succeeded());
        if (!r.succeeded() &&
            r.error().message.find("skeleton") == std::string::npos) {
            printf("    missing 'skeleton' in: %s\n", r.error().message.c_str());
            ++failures;
        }
    }
    return failures;
}
