// Core math tests: euler composition, lookAt, orbit camera framing.
#include <cmath>
#include <cstdio>
#include <limits>

#include "../tests/expect.hpp"
#include "m2rig/camera.hpp"
#include "m2rig/coordsys.hpp"
#include "m2rig/math.hpp"
#include "m2rig/renderer.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skeleton.hpp"

using namespace m2rig;

M2RIG_TEST(math, identity_compose_roundtrip) {
    int failures = 0;
    const Vec3 t{1, 2, 3};
    const Vec3 e{0.3f, -0.2f, 0.9f};
    const Vec3 s{1, 1, 1};
    const Mat4 m = Mat4::compose(t, e, s);
    const Mat4 inv = m.inverseRigid();
    const Vec3 p{4, -1, 0.5f};
    const Vec3 q = inv.transformPoint(m.transformPoint(p));
    CHECK_NEAR(q.x, p.x, 1e-4);
    CHECK_NEAR(q.y, p.y, 1e-4);
    CHECK_NEAR(q.z, p.z, 1e-4);
    return failures;
}

M2RIG_TEST(math, cross_dot_orthonormal) {
    int failures = 0;
    const Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    const Vec3 c = cross(x, y);
    CHECK_NEAR(c.x, z.x, 1e-6);
    CHECK_NEAR(c.y, z.y, 1e-6);
    CHECK_NEAR(c.z, z.z, 1e-6);
    CHECK_NEAR(dot(x, y), 0.0, 1e-6);
    CHECK_NEAR(length({3, 4, 0}), 5.0, 1e-5);
    return failures;
}

M2RIG_TEST(math, perspective_projects_forward) {
    int failures = 0;
    const Mat4 view = Mat4::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = Mat4::perspectiveFov(1.0f, 1.0f, 0.1f, 100.0f);
    const Mat4 vp = view * proj;
    // Origin must land in front (w != 0 path) — smoke check via eye transform.
    const Vec3 atOrigin = view.transformPoint({0, 0, 0});
    CHECK_NEAR(atOrigin.x, 0.0, 1e-4);
    CHECK_NEAR(atOrigin.y, 0.0, 1e-4);
    CHECK_NEAR(atOrigin.z, -5.0, 1e-4);
    (void)vp;
    return failures;
}

M2RIG_TEST(math, camera_frames_aabb) {
    int failures = 0;
    ArcballCamera cam;
    Aabb box;
    box.grow({-1, 0, -1});
    box.grow({1, 2, 1});
    cam.frameAabb(box);
    CHECK_NEAR(cam.target.x, 0.0, 1e-5);
    CHECK_NEAR(cam.target.y, 1.0, 1e-5);
    CHECK_TRUE(cam.distance > 2.0f);
    cam.preset({0, 0, 1});
    CHECK_NEAR(cam.yaw, 0.0, 1e-5);
    return failures;
}

M2RIG_TEST(math, camera_frames_sample_in_ndc) {
    // Viewport integration guard: the framed sample armor center must project
    // inside NDC [-1,1] for a typical 16:9 viewport. Catches camera/math
    // regressions that would show an empty viewport (no mesh, no grid center).
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    ArcballCamera cam;
    cam.frameAabb(sample.value().mesh.bounds);
    CHECK_TRUE(cam.distance > 1.0f);
    CHECK_TRUE(cam.distance < 50.0f);
    const float aspect = 16.0f / 9.0f;
    const Mat4 vp = cam.viewMatrix() * cam.projMatrix(aspect);
    const Vec3 ndc = vp.transformPoint(sample.value().mesh.bounds.center());
    CHECK_TRUE(ndc.x > -1.0f && ndc.x < 1.0f);
    CHECK_TRUE(ndc.y > -1.0f && ndc.y < 1.0f);
    CHECK_TRUE(ndc.z > 0.0f && ndc.z < 1.0f);
    // Grid origin must also be in front of the camera.
    const Vec3 gridNdc = vp.transformPoint({0, 0, 0});
    CHECK_TRUE(gridNdc.z > 0.0f && gridNdc.z < 1.0f);
    return failures;
}

M2RIG_TEST(math, camera_frames_big_aabb_radius100_in_ndc) {
    // Guard for real game models (shaman_m ~100 units): framing must keep
    // the whole AABB inside NDC and inside [near, far].
    int failures = 0;
    Aabb box;
    box.grow({-60, 0, -60});
    box.grow({60, 120, 60});
    const float r = box.radius();
    CHECK_TRUE(r > 50.0f);
    ArcballCamera cam;
    cam.frameAabb(box);
    CHECK_TRUE(cam.distance + r < cam.farZ);
    CHECK_TRUE(cam.distance - r > cam.nearZ);
    const float aspect = 1372.0f / 701.0f;
    const Mat4 vp = cam.viewMatrix() * cam.projMatrix(aspect);
    const Vec3 corners[8] = {{-60, 0, -60}, {60, 0, -60}, {-60, 0, 60},  {60, 0, 60},
                             {-60, 120, -60}, {60, 120, -60}, {-60, 120, 60}, {60, 120, 60}};
    for (const Vec3& c : corners) {
        const Vec3 ndc = vp.transformPoint(c);
        CHECK_TRUE(ndc.x > -1.05f && ndc.x < 1.05f);
        CHECK_TRUE(ndc.y > -1.05f && ndc.y < 1.05f);
        CHECK_TRUE(ndc.z > -0.05f && ndc.z < 1.05f);
    }
    return failures;
}

M2RIG_TEST(math, euler_xyz_extraction_roundtrip) {
    int failures = 0;
    const Vec3 cases[] = {{0, 0, 0}, {0.5f, -0.3f, 1.2f}, {1.0f, 0.5f, -0.7f}, {0.1f, 1.5f, 0.2f}};
    for (const Vec3& e : cases) {
        const Mat4 r = Mat4::rotationEulerXyz(e);
        const Vec3 back = r.eulerXyzFromRotation();
        const Mat4 r2 = Mat4::rotationEulerXyz(back);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) CHECK_NEAR(r.m[i][j], r2.m[i][j], 1e-5);
    }
    return failures;
}

namespace {

bool matAllFinite(const Mat4& m) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(m.m[r][c])) return false;
    return true;
}

void checkBoxInNdc(int& failures, const ArcballCamera& cam, const Aabb& box, float aspect) {
    const Mat4 vp = cam.viewMatrix() * cam.projMatrix(aspect);
    for (int xi = 0; xi < 2; ++xi)
        for (int yi = 0; yi < 2; ++yi)
            for (int zi = 0; zi < 2; ++zi) {
                const Vec3 p{xi ? box.max.x : box.min.x, yi ? box.max.y : box.min.y,
                             zi ? box.max.z : box.min.z};
                const Vec3 ndc = vp.transformPoint(p);
                CHECK_TRUE(ndc.x >= -1.0f && ndc.x <= 1.0f);
                CHECK_TRUE(ndc.y >= -1.0f && ndc.y <= 1.0f);
                CHECK_TRUE(ndc.z >= 0.0f && ndc.z <= 1.0f);
            }
}

}  // namespace

M2RIG_TEST(math, camera_ortho_frames_in_ndc) {
    int failures = 0;
    Aabb box;
    box.grow({-1, 0, -1});
    box.grow({1, 2, 1});
    ArcballCamera cam;
    cam.frameAabb(box);
    cam.orthographic = true;
    CHECK_NEAR(cam.orthoHeight, box.radius() * 2.7f, 1e-4);
    checkBoxInNdc(failures, cam, box, 16.0f / 9.0f);
    // Game-size box: same guarantee at 100-unit scale.
    Aabb big;
    big.grow({-60, 0, -60});
    big.grow({60, 120, 60});
    ArcballCamera camBig;
    camBig.frameAabb(big);
    camBig.orthographic = true;
    checkBoxInNdc(failures, camBig, big, 16.0f / 9.0f);
    return failures;
}

M2RIG_TEST(math, camera_update_clip_extremes) {
    int failures = 0;
    Aabb tiny;
    tiny.grow({0, 0, 0});
    tiny.grow({0.1f, 0.1f, 0.1f});
    ArcballCamera cam;
    cam.frameAabb(tiny);
    CHECK_EQ(cam.distance, 0.5f);  // floor, never zoomed into the surface
    CHECK_TRUE(cam.nearZ >= 0.01f && cam.nearZ < cam.distance);
    CHECK_TRUE(cam.farZ >= 200.0f);
    Aabb huge;
    huge.grow({-250, 0, -250});
    huge.grow({250, 500, 250});
    ArcballCamera camHuge;
    camHuge.frameAabb(huge);
    const float r = huge.radius();
    CHECK_NEAR(camHuge.farZ, (camHuge.distance + 2.0f * r) * 1.5f, 1e-2f);
    CHECK_TRUE(camHuge.farZ > camHuge.distance + r);
    // Near pullback when the model nearly touches the eye.
    ArcballCamera camPull;
    camPull.framedRadius = 10.0f;
    camPull.distance = 10.5f;
    camPull.updateClip();
    CHECK_NEAR(camPull.nearZ, (10.5f - 10.0f) * 0.5f, 1e-5f);
    return failures;
}

M2RIG_TEST(math, camera_zoom_limits) {
    int failures = 0;
    ArcballCamera fresh;
    CHECK_EQ(fresh.maxDistance(), 150.0f);
    CHECK_EQ(fresh.maxOrthoHeight(), 100.0f);
    Aabb box;
    box.grow({-1, 0, -1});
    box.grow({1, 2, 1});
    ArcballCamera cam;
    cam.frameAabb(box);
    const float d0 = cam.distance;
    for (int i = 0; i < 50; ++i) cam.zoom(1.0f);
    CHECK_EQ(cam.distance, 0.1f);
    CHECK_TRUE(cam.distance < d0);
    for (int i = 0; i < 200; ++i) cam.zoom(-1.0f);
    CHECK_EQ(cam.distance, cam.maxDistance());
    cam.orthographic = true;
    for (int i = 0; i < 100; ++i) cam.zoom(1.0f);
    CHECK_EQ(cam.orthoHeight, 0.02f);
    for (int i = 0; i < 100; ++i) cam.zoom(-1.0f);
    CHECK_EQ(cam.orthoHeight, cam.maxOrthoHeight());
    // Framed radius raises the caps; absurd radii stay capped.
    ArcballCamera camFar;
    camFar.framedRadius = 250.0f;
    CHECK_TRUE(camFar.maxDistance() > 150.0f);
    camFar.framedRadius = 1e5f;
    CHECK_EQ(camFar.maxDistance(), 10000.0f);
    CHECK_EQ(camFar.maxOrthoHeight(), 5000.0f);
    return failures;
}

M2RIG_TEST(math, camera_presets_keep_framing) {
    int failures = 0;
    Aabb box;
    box.grow({-1, 0, -1});
    box.grow({1, 2, 1});
    ArcballCamera cam;
    cam.frameAabb(box);
    const float d0 = cam.distance;
    const Vec3 t0 = cam.target;
    cam.preset({0, 0.1f, 1});
    CHECK_NEAR(cam.yaw, 0.0f, 1e-4f);
    CHECK_NEAR(cam.pitch, 0.0997f, 1e-3f);
    cam.preset({0, 1, 0.001f});
    CHECK_NEAR(cam.yaw, 0.0f, 1e-4f);
    CHECK_TRUE(cam.pitch > 1.55f && cam.pitch < 1.58f);
    cam.preset({-1, 0.1f, 0});
    CHECK_NEAR(cam.yaw, -kPi * 0.5f, 1e-3f);
    CHECK_TRUE(matAllFinite(cam.viewMatrix()));
    CHECK_EQ(cam.distance, d0);
    CHECK_EQ(cam.target.x, t0.x);
    CHECK_EQ(cam.target.y, t0.y);
    CHECK_EQ(cam.target.z, t0.z);
    // Framed center still projects inside NDC from the side view.
    const Mat4 vp = cam.viewMatrix() * cam.projMatrix(16.0f / 9.0f);
    const Vec3 ndc = vp.transformPoint(box.center());
    CHECK_TRUE(ndc.x > -1.0f && ndc.x < 1.0f);
    CHECK_TRUE(ndc.y > -1.0f && ndc.y < 1.0f);
    CHECK_TRUE(ndc.z > 0.0f && ndc.z < 1.0f);
    return failures;
}

M2RIG_TEST(math, camera_frame_rejects_nonfinite) {
    int failures = 0;
    Aabb bad;
    bad.empty = false;
    bad.min = {0, 0, 0};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    bad.max = {nan, 1, 1};
    ArcballCamera cam;
    cam.frameAabb(bad);  // must keep defaults, never poison the camera
    CHECK_EQ(cam.target.x, 0.0f);
    CHECK_EQ(cam.target.y, 1.0f);
    CHECK_EQ(cam.target.z, 0.0f);
    CHECK_EQ(cam.distance, 5.0f);
    CHECK_EQ(cam.framedRadius, 0.0f);
    // Sanitizer recovers an already-poisoned camera.
    cam.distance = nan;
    cam.framedRadius = nan;
    cam.fovY = nan;
    cam.updateClip();
    CHECK_TRUE(std::isfinite(cam.distance) && cam.distance > 0.0f);
    CHECK_TRUE(std::isfinite(cam.nearZ) && std::isfinite(cam.farZ));
    CHECK_TRUE(matAllFinite(cam.viewMatrix()));
    return failures;
}

M2RIG_TEST(math, camera_frame_empty_keeps_state) {
    int failures = 0;
    ArcballCamera cam;
    const Aabb empty;
    cam.frameAabb(empty);
    CHECK_EQ(cam.distance, 5.0f);
    CHECK_EQ(cam.framedRadius, 0.0f);
    Aabb point;
    point.grow({3, 4, 5});
    cam.frameAabb(point);
    CHECK_EQ(cam.distance, 0.5f);
    CHECK_EQ(cam.orthoHeight, 0.1f);
    CHECK_EQ(cam.target.x, 3.0f);
    return failures;
}

M2RIG_TEST(math, camera_orbit_clamp_and_pan_scale) {
    int failures = 0;
    ArcballCamera cam;
    // New orbit with full 360° vertical rotation (pole crossing):
    // pitch -= dy * 0.005f, so positive dy = drag down = pitch decreases (look down)
    // Start at pitch=0.35, drag up (negative dy) -> pitch increases
    // Use smaller drag to cross only one pole at a time
    cam.orbit(0.0f, -400.0f); // drag up -> pitch increases -> should cross north pole (~2.0 rad = 114 deg)
    // After crossing north pole, pitch should wrap and yaw should add pi
    CHECK_TRUE(cam.pitch < 1.57f && cam.pitch > -1.57f); // Pitch stays within [-pi/2, pi/2]
    CHECK_NEAR(cam.yaw, kPi, 0.1f); // Yaw should have flipped by pi
    
    // Drag down from there (cross south pole)
    cam.orbit(0.0f, 800.0f); // drag down -> pitch decreases -> should cross south pole
    CHECK_TRUE(cam.pitch < 1.57f && cam.pitch > -1.57f);
    CHECK_NEAR(cam.yaw, 0.0f, 0.1f); // Yaw should be back to ~0
    
    ArcballCamera nearCam, farCam;
    nearCam.distance = 5.0f;
    farCam.distance = 50.0f;
    const Vec3 tNear0 = nearCam.target, tFar0 = farCam.target;
    nearCam.pan(100.0f, 0.0f);
    farCam.pan(100.0f, 0.0f);
    const float dNear = distance(nearCam.target, tNear0);
    const float dFar = distance(farCam.target, tFar0);
    CHECK_TRUE(dNear > 0.0f);
    CHECK_NEAR(dFar / dNear, 10.0, 1e-3);
    return failures;
}

M2RIG_TEST(math, gizmo_rotate_row_strip_matches_compose) {
    // compose() scales ROWS (S*R): stripping columns (old bug) corrupts the
    // euler under non-uniform parent scale, stripping rows recovers R.
    int failures = 0;
    const Vec3 e{0.3f, -0.2f, 0.1f};
    const Vec3 s{2.0f, 0.5f, 3.0f};
    const Mat4 r = Mat4::rotationEulerXyz(e);
    Mat4 m = Mat4::scaling(s);
    m = m * r;
    Mat4 rowStripped = m;
    for (int row = 0; row < 3; ++row) {
        const float len = std::sqrt(rowStripped.m[row][0] * rowStripped.m[row][0] +
                                    rowStripped.m[row][1] * rowStripped.m[row][1] +
                                    rowStripped.m[row][2] * rowStripped.m[row][2]);
        if (len > 1e-9f) {
            rowStripped.m[row][0] /= len;
            rowStripped.m[row][1] /= len;
            rowStripped.m[row][2] /= len;
        }
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) CHECK_NEAR(rowStripped.m[i][j], r.m[i][j], 1e-5);
    // World->local roundtrip through a scaled+rotated parent, as the gizmo does.
    BoneDefinition root{"Root", kNoParent, {1, 0, 0}, {0, 0.5f, 0}, {2, 2, 2}};
    BoneDefinition child{"Child", 0, {0, 1, 0}, {0.3f, -0.2f, 0.1f}, {1, 1, 1}};
    auto built = buildSkeleton("gizmo", {root, child});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    const Mat4 pG = skel.bones[0].globalTransform;
    const Mat4 cW = skel.bones[1].globalTransform;
    Mat4 nW = cW;
    nW.m[3][0] += 2.0f;  // gizmo translate drag in world space
    // Shared core decomposition (same call the viewport gizmo makes — the
    // two can never drift apart).
    const BoneLocalEdit cur{skel.bones[1].localPosition, skel.bones[1].localRotationEuler,
                            skel.bones[1].localScale};
    const BoneLocalEdit edit = decomposeWorldToLocal(pG, nW, LocalEditOp::Translate, cur);
    skel.bones[1].localPosition = edit.position;
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    const Mat4 cW2 = skel.bones[1].globalTransform;
    CHECK_NEAR(cW2.m[3][0], nW.m[3][0], 1e-4);
    CHECK_NEAR(cW2.m[3][1], nW.m[3][1], 1e-4);
    CHECK_NEAR(cW2.m[3][2], nW.m[3][2], 1e-4);
    return failures;
}

M2RIG_TEST(math, gizmo_decompose_recovers_all_channels) {
    // Round-trip through the shared decomposition for every op: pose a bone
    // to known locals, rebuild, decompose the resulting world matrix, and
    // require the edited locals back (untouched channels pass through).
    int failures = 0;
    auto built = buildSkeleton("dec", {{"P", kNoParent, {1, 0, 0}, {0, 0.5f, 0}, {1, 1, 1}},
                                       {"C", 0, {0, 1, 0}, {0.3f, -0.2f, 0.1f}, {1, 1, 1}}});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    const Mat4 pG = skel.bones[0].globalTransform;
    const BoneLocalEdit base{skel.bones[1].localPosition, skel.bones[1].localRotationEuler,
                             skel.bones[1].localScale};
    // Translate.
    skel.bones[1].localPosition = {base.position.x + 0.5f, base.position.y - 0.25f,
                                   base.position.z + 0.125f};
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    {
        const BoneLocalEdit got =
            decomposeWorldToLocal(pG, skel.bones[1].globalTransform, LocalEditOp::Translate, base);
        CHECK_NEAR(got.position.x, skel.bones[1].localPosition.x, 1e-4);
        CHECK_NEAR(got.position.y, skel.bones[1].localPosition.y, 1e-4);
        CHECK_NEAR(got.position.z, skel.bones[1].localPosition.z, 1e-4);
        CHECK_NEAR(got.rotationEuler.x, base.rotationEuler.x, 1e-6);
        CHECK_NEAR(got.scale.x, base.scale.x, 1e-6);
    }
    // Rotate.
    skel.bones[1].localRotationEuler = {0.4f, -0.2f, 0.1f};
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    {
        const BoneLocalEdit got =
            decomposeWorldToLocal(pG, skel.bones[1].globalTransform, LocalEditOp::Rotate, base);
        CHECK_NEAR(got.rotationEuler.x, 0.4f, 1e-4);
        CHECK_NEAR(got.rotationEuler.y, -0.2f, 1e-4);
        CHECK_NEAR(got.rotationEuler.z, 0.1f, 1e-4);
        CHECK_NEAR(got.scale.x, base.scale.x, 1e-6);
    }
    // Scale (unit-scale parent: world row lengths equal local scale).
    skel.bones[1].localScale = {2.0f, 0.5f, 1.5f};
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    {
        const BoneLocalEdit got =
            decomposeWorldToLocal(pG, skel.bones[1].globalTransform, LocalEditOp::Scale, base);
        CHECK_NEAR(got.scale.x, 2.0f, 1e-4);
        CHECK_NEAR(got.scale.y, 0.5f, 1e-4);
        CHECK_NEAR(got.scale.z, 1.5f, 1e-4);
    }
    return failures;
}

M2RIG_TEST(math, gizmo_parent_draw_matrix_aligned) {
    // Draw matrix carries the (normalized) parent orientation at the joint;
    // identity parents degrade exactly to world-aligned handles.
    int failures = 0;
    Mat4 p = Mat4::compose({5, 0, 0}, {0, 0, kPi * 0.5f}, {2, 2, 2});
    const Mat4 d = parentAlignedDrawMatrix(p, {1, 2, 3});
    for (int r = 0; r < 3; ++r) {
        const float len = std::sqrt(d.m[r][0] * d.m[r][0] + d.m[r][1] * d.m[r][1] +
                                    d.m[r][2] * d.m[r][2]);
        CHECK_NEAR(len, 1.0f, 1e-6);
    }
    // Row 0 of Rz(90) is (0,1,0).
    CHECK_NEAR(d.m[0][0], 0.0f, 1e-6);
    CHECK_NEAR(d.m[0][1], 1.0f, 1e-6);
    CHECK_NEAR(d.m[0][2], 0.0f, 1e-6);
    CHECK_NEAR(d.m[3][0], 1.0f, 1e-6);
    CHECK_NEAR(d.m[3][1], 2.0f, 1e-6);
    CHECK_NEAR(d.m[3][2], 3.0f, 1e-6);
    const Mat4 di = parentAlignedDrawMatrix(Mat4::identity(), {4, 5, 6});
    CHECK_NEAR(di.m[0][0], 1.0f, 1e-6);
    CHECK_NEAR(di.m[1][1], 1.0f, 1e-6);
    CHECK_NEAR(di.m[2][2], 1.0f, 1e-6);
    CHECK_NEAR(di.m[3][0], 4.0f, 1e-6);
    return failures;
}

M2RIG_TEST(math, gizmo_parent_translate_is_exact) {
    // Parent-space translate drag: simulated draw-matrix move maps back so
    // the rebuilt world carries the drag delta exactly.
    int failures = 0;
    auto built = buildSkeleton("pt", {{"P", kNoParent, {0, 0, 0}, {0, 0, kPi * 0.5f}, {1, 1, 1}},
                                      {"C", 0, {1, 0, 0}, {0, 0, 0}, {1, 1, 1}}});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    const Mat4 pG = skel.bones[0].globalTransform;
    const Vec3 t0{skel.bones[1].globalTransform.m[3][0],
                  skel.bones[1].globalTransform.m[3][1],
                  skel.bones[1].globalTransform.m[3][2]};
    CHECK_NEAR(t0.x, 0.0f, 1e-5);
    CHECK_NEAR(t0.y, 1.0f, 1e-5);
    Mat4 movedW = skel.bones[1].globalTransform;
    movedW.m[3][1] += 2.0f;  // drag +2 in world Y (parent-frame step)
    const BoneLocalEdit cur{skel.bones[1].localPosition, skel.bones[1].localRotationEuler,
                            skel.bones[1].localScale};
    const BoneLocalEdit edit =
        decomposeWorldToLocal(pG, movedW, LocalEditOp::Translate, cur);
    skel.bones[1].localPosition = edit.position;
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    const Vec3 t1{skel.bones[1].globalTransform.m[3][0],
                  skel.bones[1].globalTransform.m[3][1],
                  skel.bones[1].globalTransform.m[3][2]};
    CHECK_NEAR(t1.x, t0.x, 1e-5);
    CHECK_NEAR(t1.y, t0.y + 2.0f, 1e-5);
    CHECK_NEAR(t1.z, t0.z, 1e-5);
    return failures;
}

M2RIG_TEST(math, gizmo_parent_rotate_about_parent_axes) {
    // Parent rotZ(90), child identity at (1,0,0) [world (0,1,0)]. The draw
    // frame carries the parent orientation, so its X ring lies along world Y.
    // A world rotation W about that axis applied to the draw output must move
    // the bone by exactly W: rebuilt world rotation == W * old world, joint
    // position unchanged. (Constructed from outputs only — agnostic to
    // ImGuizmo's internal delta conventions and angle signs.)
    int failures = 0;
    auto built = buildSkeleton("pr", {{"P", kNoParent, {0, 0, 0}, {0, 0, kPi * 0.5f}, {1, 1, 1}},
                                      {"C", 0, {1, 0, 0}, {0, 0, 0}, {1, 1, 1}}});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    const Mat4 pG = skel.bones[0].globalTransform;
    const Mat4 oldW = skel.bones[1].globalTransform;
    const Vec3 t{oldW.m[3][0], oldW.m[3][1], oldW.m[3][2]};
    const Mat4 draw = parentAlignedDrawMatrix(pG, t);
    // World rotation about the parent X axis (= world Y here).
    const Mat4 w = Mat4::rotationY(kPi * 0.5f);
    Mat4 drawNew = w * draw;
    drawNew.m[3][0] = t.x;
    drawNew.m[3][1] = t.y;
    drawNew.m[3][2] = t.z;
    drawNew.m[3][3] = 1.0f;
    const Vec3 got = applyParentRotationDelta(pG, drawNew, {0, 0, 0});
    skel.bones[1].localRotationEuler = got;
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    const Mat4& g = skel.bones[1].globalTransform;
    const Mat4 expectW = w * oldW;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) CHECK_NEAR(g.m[r][c], expectW.m[r][c], 1e-4);
    CHECK_NEAR(g.m[3][0], t.x, 1e-5);
    CHECK_NEAR(g.m[3][1], t.y, 1e-5);
    CHECK_NEAR(g.m[3][2], t.z, 1e-5);
    return failures;
}

M2RIG_TEST(math, gizmo_draw_scale_ratios_guarded) {
    int failures = 0;
    const Vec3 r = drawScaleRatios(Mat4::scaling({1, 2, 0.5f}), Mat4::scaling({2, 1, 1.5f}));
    CHECK_NEAR(r.x, 2.0f, 1e-6);
    CHECK_NEAR(r.y, 0.5f, 1e-6);
    CHECK_NEAR(r.z, 3.0f, 1e-6);
    // Degenerate source axis is a no-op (1.0), never inf/nan.
    Mat4 zero = Mat4::scaling({0, 1, 1});
    const Vec3 r2 = drawScaleRatios(zero, Mat4::scaling({5, 2, 3}));
    CHECK_NEAR(r2.x, 1.0f, 1e-6);
    CHECK_NEAR(r2.y, 2.0f, 1e-6);
    return failures;
}

M2RIG_TEST(math, camera_all_six_presets_stay_finite) {
    // The viewport overlay offers Front/Back/Top/Bottom/Left/Right. Every
    // preset must yield a finite eye + view matrix — top-down/bottom-up
    // views degenerate lookAt when the view dir parallels world-up, so the
    // 0.001 tilt in the preset vectors is load-bearing (guard it here).
    int failures = 0;
    const Vec3 dirs[6] = {{0, 0.1f, 1}, {0, 0.1f, -1}, {0, 1, 0.001f},
                          {0, -1, 0.001f}, {-1, 0.1f, 0}, {1, 0.1f, 0}};
    for (const Vec3& d : dirs) {
        ArcballCamera cam;
        cam.preset(d);
        const Vec3 e = cam.eye();
        CHECK_TRUE(isFiniteF(e.x) && isFiniteF(e.y) && isFiniteF(e.z));
        CHECK_TRUE(matAllFinite(cam.viewMatrix()));
    }
    // Spot-check yaw orientation: back faces -Z from +Z... (yaw ~ pi),
    // right looks down -X (yaw ~ pi/2).
    ArcballCamera back;
    back.preset({0, 0.1f, -1});
    CHECK_NEAR(back.yaw, kPi, 1e-3);
    ArcballCamera right;
    right.preset({1, 0.1f, 0});
    CHECK_NEAR(right.yaw, kPi * 0.5f, 1e-3);
    return failures;
}

M2RIG_TEST(math, skeleton_composes_local_then_parent) {
    // Row-vector convention: a point transforms p*L*P, so globals compose
    // G_child = L_child * G_parent. A 90°-rotated parent must swing the
    // child's offset with it. The old parent*local order left offsets
    // unrotated and detached every rotatedbind skeleton (GR2 bipeds);
    // translation-only rigs (sample armor) were blind to it.
    int failures = 0;
    auto built = buildSkeleton("rot", {{"Root", kNoParent, {0, 0, 0}, {0, 0, kPi * 0.5f}, {1, 1, 1}},
                                        {"Child", 0, {1, 0, 0}, {0, 0, 0}, {1, 1, 1}}});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    const Skeleton& skel = built.value();
    const Vec3 w{skel.bones[1].globalTransform.m[3][0],
                 skel.bones[1].globalTransform.m[3][1],
                 skel.bones[1].globalTransform.m[3][2]};
    // (1,0,0) rotated +90° about Z -> (0,1,0).
    CHECK_NEAR(w.x, 0.0f, 1e-5);
    CHECK_NEAR(w.y, 1.0f, 1e-5);
    CHECK_NEAR(w.z, 0.0f, 1e-5);
    // Three-level chain accumulates translations through rotations.
    auto chain = buildSkeleton("chain", {{"A", kNoParent, {0, 0, 0}, {0, 0, kPi * 0.5f}, {1, 1, 1}},
                                         {"B", 0, {1, 0, 0}, {0, 0, 0}, {1, 1, 1}},
                                         {"C", 1, {1, 0, 0}, {0, 0, 0}, {1, 1, 1}}});
    CHECK_TRUE(chain.succeeded());
    if (!chain.succeeded()) return failures + 1;
    const Vec3 wc{chain.value().bones[2].globalTransform.m[3][0],
                  chain.value().bones[2].globalTransform.m[3][1],
                  chain.value().bones[2].globalTransform.m[3][2]};
    CHECK_NEAR(wc.x, 0.0f, 1e-5);
    CHECK_NEAR(wc.y, 2.0f, 1e-5);
    CHECK_NEAR(wc.z, 0.0f, 1e-5);
    return failures;
}

M2RIG_TEST(math, coordsys_up_front_mapping) {
    // (up, front) axis pairs from file global settings map to conversion
    // sources; exotic/left-handed pairs decline (nullopt) so the profile
    // assumed source applies instead of a guessed conversion.
    int failures = 0;
    using A = AxisDir;
    auto got = coordSysFromUpFront(A::PosZ, A::PosY);
    CHECK_TRUE(got.has_value() && got.value() == CoordSys::ZUp_YForward);
    got = coordSysFromUpFront(A::PosZ, A::NegY);
    CHECK_TRUE(got.has_value() && got.value() == CoordSys::ZUp_YBackward);
    got = coordSysFromUpFront(A::PosY, A::PosZ);
    CHECK_TRUE(got.has_value() && got.value() == CoordSys::YUp_ZForward);
    got = coordSysFromUpFront(A::PosY, A::NegZ);
    CHECK_TRUE(got.has_value() && got.value() == CoordSys::Canonical);
    CHECK_FALSE(coordSysFromUpFront(A::PosX, A::PosY).has_value());
    CHECK_FALSE(coordSysFromUpFront(A::NegY, A::PosZ).has_value());
    CHECK_FALSE(coordSysFromUpFront(A::Unknown, A::PosY).has_value());
    CHECK_FALSE(coordSysFromUpFront(A::PosZ, A::Unknown).has_value());
    return failures;
}

M2RIG_TEST(math, coordsys_zup_profile_matches_verified_matrix) {
    // HARD-RULE guard: the FBX/GR2 profile conversion must be element-identical
    // to the verified Mat4::convertZUpToYUp(). If anyone re-derives the matrix
    // by hand (and flips a sign), this fails instead of shipping rotated models.
    int failures = 0;
    const Mat4 viaProfile = convertToCanonical(CoordSys::ZUp_YForward);
    const Mat4 verified = Mat4::convertZUpToYUp();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) CHECK_NEAR(viaProfile.m[r][c], verified.m[r][c], 1e-7);
    return failures;
}

M2RIG_TEST(math, coordsys_up_forward_right_mapping) {
    // Z-up (X=right, Y=forward, Z=up) -> canonical Y-up (X=right, Y=up,
    // forward=-Z). Up must go up, forward must go to -Z, right stays right.
    int failures = 0;
    const Mat4 m = convertToCanonical(CoordSys::ZUp_YForward);
    const Vec3 up = m.transformPoint({0, 0, 1});
    CHECK_NEAR(up.x, 0.0f, 1e-6);
    CHECK_NEAR(up.y, 1.0f, 1e-6);
    CHECK_NEAR(up.z, 0.0f, 1e-6);
    const Vec3 fwd = m.transformPoint({0, 1, 0});
    CHECK_NEAR(fwd.x, 0.0f, 1e-6);
    CHECK_NEAR(fwd.y, 0.0f, 1e-6);
    CHECK_NEAR(fwd.z, -1.0f, 1e-6);
    const Vec3 right = m.transformPoint({1, 0, 0});
    CHECK_NEAR(right.x, 1.0f, 1e-6);
    CHECK_NEAR(right.y, 0.0f, 1e-6);
    CHECK_NEAR(right.z, 0.0f, 1e-6);
    return failures;
}

M2RIG_TEST(math, coordsys_from_canonical_inverts_to_canonical) {
    // convertFromCanonical must undo convertToCanonical for every enum value
    // (round-trip identity, incl. the mirror profiles).
    int failures = 0;
    const CoordSys all[] = {CoordSys::Canonical, CoordSys::ZUp_YForward,
                            CoordSys::YUp_ZForward, CoordSys::ZUp_YBackward,
                            CoordSys::XUp};
    const Vec3 p{1.5f, -2.25f, 3.75f};
    for (CoordSys cs : all) {
        const Mat4 fwd = convertToCanonical(cs);
        const Mat4 back = convertFromCanonical(cs);
        const Vec3 q = back.transformPoint(fwd.transformPoint(p));
        CHECK_NEAR(q.x, p.x, 1e-4);
        CHECK_NEAR(q.y, p.y, 1e-4);
        CHECK_NEAR(q.z, p.z, 1e-4);
    }
    return failures;
}

namespace {

Mesh orientBoxMesh(float y0, float y1) {
    Mesh mesh;
    mesh.name = "orient-box";
    mesh.vertices.resize(8);
    const float xs[2] = {-1.0f, 1.0f};
    const float zs[2] = {-1.0f, 1.0f};
    std::size_t k = 0;
    for (float x : xs)
        for (float z : zs) {
            mesh.vertices[k++].position = {x, y0, z};
            mesh.vertices[k++].position = {x, y1, z};
        }
    computeBounds(mesh);
    return mesh;
}

Skeleton orientTwoBone(float headY, float footY) {
    // Root at origin (identity) so local == world for the head/foot joints.
    auto built = buildSkeleton("orient", {{"Root", kNoParent, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}},
                                            {"Head", 0, {0, headY, 0}, {0, 0, 0}, {1, 1, 1}},
                                            {"Foot", 0, {0, footY, 0}, {0, 0, 0}, {1, 1, 1}}});
    return std::move(built.value());
}

bool orientHasFinding(const OrientationReport& rep, const std::string& id) {
    for (const auto& f : rep.findings)
        if (f.id == id) return true;
    return false;
}

}  // namespace

M2RIG_TEST(math, orient_sample_armor_is_sane) {
    // The procedural reference rig must pass orientation diagnostics: guards
    // the sample, the diagnostic itself, and the head/feet naming contract.
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const OrientationReport rep =
        diagnoseOrientation(sample.value().mesh, sample.value().skeleton);
    if (!rep.sane()) printf("    report:\n%s\n", rep.toDisplayString().c_str());
    CHECK_TRUE(rep.sane());
    CHECK_TRUE(rep.hasHeadFeet);
    CHECK_TRUE(rep.headY > rep.footY);
    return failures;
}

M2RIG_TEST(math, orient_inverted_rig_is_blocked) {
    // Head at/below feet: 180° flip / lying-flat class. Must block.
    int failures = 0;
    const Mesh mesh = orientBoxMesh(0.0f, 2.0f);
    const Skeleton skel = orientTwoBone(0.2f, 1.8f);
    const OrientationReport rep = diagnoseOrientation(mesh, skel);
    CHECK_FALSE(rep.sane());
    CHECK_TRUE(orientHasFinding(rep, "ORIENT_HEAD_BELOW_FEET"));
    return failures;
}

M2RIG_TEST(math, orient_detached_rig_is_blocked) {
    // Healthy head/feet order but the rig floats far above the mesh: detached.
    int failures = 0;
    const Mesh mesh = orientBoxMesh(10.0f, 12.0f);
    const Skeleton skel = orientTwoBone(1.8f, 0.2f);
    const OrientationReport rep = diagnoseOrientation(mesh, skel);
    CHECK_FALSE(rep.sane());
    CHECK_TRUE(orientHasFinding(rep, "ORIENT_SKELETON_OFF_MESH"));
    return failures;
}

M2RIG_TEST(math, orient_rigid_detached_is_blocked) {
    // Skeleton overlaps the mesh and head is above feet, but a joint floats
    // far from the verts bound to it: different spaces (detached import).
    int failures = 0;
    Mesh mesh;
    mesh.name = "orient-rigid";
    mesh.vertices.resize(16);
    for (std::size_t i = 0; i < 8; ++i) {
        mesh.vertices[i].position = {(i % 2 == 0) ? -1.0f : 1.0f, 0.1f,
                                    (i < 4) ? -1.0f : 1.0f};
        mesh.vertices[i].influences.push_back({1, 1.0f});  // Head-owned low verts
        mesh.vertices[8 + i].position = {(i % 2 == 0) ? -1.0f : 1.0f, 0.15f,
                                        (i < 4) ? -1.0f : 1.0f};
        mesh.vertices[8 + i].influences.push_back({2, 1.0f});  // Foot-owned low verts
    }
    computeBounds(mesh);
    auto built = buildSkeleton("orient", {{"Root", kNoParent, {0, 1, 0}, {0, 0, 0}, {1, 1, 1}},
                                          {"Head", 0, {0, 0.9f, 0}, {0, 0, 0}, {1, 1, 1}},
                                          {"Foot", 0, {0, -0.9f, 0}, {0, 0, 0}, {1, 1, 1}}});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    // Head joint y=1.9 far above its y=0.1 verts; foot joint y=0.1 sits in
    // its own verts. Head still above feet, skeleton still overlaps the mesh.
    const OrientationReport rep = diagnoseOrientation(mesh, std::move(built.value()));
    CHECK_FALSE(rep.sane());
    CHECK_TRUE(orientHasFinding(rep, "ORIENT_RIGID_DETACHED"));
    CHECK_FALSE(orientHasFinding(rep, "ORIENT_HEAD_BELOW_FEET"));
    CHECK_FALSE(orientHasFinding(rep, "ORIENT_SKELETON_OFF_MESH"));
    return failures;
}

M2RIG_TEST(math, orient_empty_inputs_are_explicit) {
    // Empty skeleton blocks; empty mesh is an uncheckable note (not a block).
    int failures = 0;
    const Mesh mesh = orientBoxMesh(0.0f, 2.0f);
    const Skeleton skel = orientTwoBone(1.8f, 0.2f);
    {
        const Skeleton empty;
        const OrientationReport rep = diagnoseOrientation(mesh, empty);
        CHECK_FALSE(rep.sane());
        CHECK_TRUE(orientHasFinding(rep, "ORIENT_NO_SKELETON"));
    }
    {
        const Mesh noBounds;
        const OrientationReport rep = diagnoseOrientation(noBounds, skel);
        CHECK_TRUE(rep.sane());
        CHECK_TRUE(orientHasFinding(rep, "ORIENT_NO_MESH"));
    }
    return failures;
}

M2RIG_TEST(math, coordsys_unsupported_space_fails_explicitly) {
    // Unimplemented spaces (XUp) must FAIL loudly through the checked API —
    // never a silent identity pass-through that ships a mis-oriented asset.
    int failures = 0;
    CHECK_FALSE(isConversionImplemented(CoordSys::XUp));
    CHECK_TRUE(isConversionImplemented(CoordSys::ZUp_YForward));
    CHECK_FALSE(tryConvertToCanonical(CoordSys::XUp).succeeded());
    CHECK_TRUE(tryConvertToCanonical(CoordSys::ZUp_YForward).succeeded());
    Mesh mesh;
    mesh.name = "xup";
    mesh.vertices.resize(1);
    const auto& profile = fbxConversionProfile();
    CHECK_FALSE(
        applyConversionProfile(profile, CoordSys::XUp, mesh).succeeded());
    // Untouched by the refused conversion.
    CHECK_NEAR(mesh.vertices[0].position.x, 0.0f, 1e-9);
    CHECK_NEAR(mesh.vertices[0].position.y, 0.0f, 1e-9);
    CHECK_NEAR(mesh.vertices[0].position.z, 0.0f, 1e-9);
    return failures;
}

M2RIG_TEST(math, coordsys_skeleton_conversion_preserves_bind) {
    // Converting a Z-up skeleton must map every bind GLOBAL by the
    // basis-change conjugation newGlobal == M^-1 * oldGlobal * M.
    // Catches hierarchy-order bugs (converting locals without rebuild),
    // euler-extraction drift, and conjugation-direction flips.
    int failures = 0;
    BoneDefinition root{"Root", kNoParent, {0, 0, 5}, {0, 0, 0}, {1, 1, 1}};
    BoneDefinition child{"Child", 0, {0, 2, 0}, {0.2f, 0, 0}, {1, 1, 1}};
    BoneDefinition grand{"Grand", 1, {0, 0, 3}, {0, 0, 0.4f}, {1, 1, 1}};
    auto built = buildSkeleton("zup", {root, child, grand});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    std::vector<Mat4> oldGlobals;
    for (const auto& b : skel.bones) oldGlobals.push_back(b.globalTransform);
    const auto& profile = fbxConversionProfile();
    CHECK_TRUE(applyConversionProfile(profile, CoordSys::ZUp_YForward, skel).succeeded());
    CHECK_EQ(skel.bones.size(), 3u);
    const Mat4 m = convertToCanonical(CoordSys::ZUp_YForward);
    const Mat4 mi = m.inverseRigid();
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        const Mat4 expect = mi * oldGlobals[i] * m;
        const Mat4& got = skel.bones[i].globalTransform;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) CHECK_NEAR(got.m[r][c], expect.m[r][c], 1e-3);
    }
    // End-to-end direction pin through the real implementation: a bone with
    // a source-space yaw about Z-up up (rotZ +90: right -> forward) must come
    // out sending canonical right (1,0,0) to canonical forward (0,0,-1).
    // (Catches M*R*M^-1 vs M^-1*R*M flips in the shipped code path.)
    {
        auto one = buildSkeleton("yaw", {{"Yaw", kNoParent, {0, 0, 0},
                                          {0, 0, kPi * 0.5f}, {1, 1, 1}}});
        CHECK_TRUE(one.succeeded());
        if (one.succeeded()) {
            Skeleton ys = std::move(one.value());
            CHECK_TRUE(
                applyConversionProfile(profile, CoordSys::ZUp_YForward, ys).succeeded());
            const Mat4 g = ys.bones[0].globalTransform;
            const Vec3 got{g.m[0][0], g.m[0][1], g.m[0][2]};  // image of (1,0,0)
            CHECK_NEAR(got.x, 0.0f, 1e-3);
            CHECK_NEAR(got.y, 0.0f, 1e-3);
            CHECK_NEAR(got.z, -1.0f, 1e-3);
        }
    }
    return failures;
}

M2RIG_TEST(math, coordsys_frame_zero_reproduces_converted_bind) {
    // After converting skeleton + frames, posing frame 0 must restore the
    // converted bind locals. Guards the GR2 path (frames share source space).
    int failures = 0;
    BoneDefinition root{"Root", kNoParent, {0, 0, 5}, {0, 0, 0}, {1, 1, 1}};
    BoneDefinition child{"Child", 0, {0, 2, 0}, {0.2f, 0, 0}, {1, 1, 1}};
    auto built = buildSkeleton("zup", {root, child});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    // Synthetic frame 0 = bind, frame 1 = small delta (Z-up source space).
    std::vector<SmdFrame> frames(2);
    frames[0].time = 0;
    frames[1].time = 1;
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        SmdBonePose p0, p1;
        p0.boneId = p1.boneId = static_cast<std::uint32_t>(i);
        p0.position = skel.bones[i].localPosition;
        p0.rotation = skel.bones[i].localRotationEuler;
        p1.position = {p0.position.x + 0.5f, p0.position.y, p0.position.z};
        p1.rotation = p0.rotation;
        frames[0].poses.push_back(p0);
        frames[1].poses.push_back(p1);
    }
    const auto& profile = gr2ConversionProfile();
    CHECK_TRUE(applyConversionProfile(profile, CoordSys::ZUp_YForward, skel).succeeded());
    CHECK_TRUE(applyConversionProfile(profile, CoordSys::ZUp_YForward, frames).succeeded());
    std::vector<Vec3> bindPos, bindRot;
    for (const auto& b : skel.bones) {
        bindPos.push_back(b.localPosition);
        bindRot.push_back(b.localRotationEuler);
    }
    CHECK_TRUE(poseSkeletonFromFrame(skel, frames, 1).succeeded());
    CHECK_TRUE(poseSkeletonFromFrame(skel, frames, 0).succeeded());
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        CHECK_NEAR(skel.bones[i].localPosition.x, bindPos[i].x, 1e-3);
        CHECK_NEAR(skel.bones[i].localPosition.y, bindPos[i].y, 1e-3);
        CHECK_NEAR(skel.bones[i].localPosition.z, bindPos[i].z, 1e-3);
        CHECK_NEAR(skel.bones[i].localRotationEuler.x, bindRot[i].x, 1e-3);
        CHECK_NEAR(skel.bones[i].localRotationEuler.y, bindRot[i].y, 1e-3);
        CHECK_NEAR(skel.bones[i].localRotationEuler.z, bindRot[i].z, 1e-3);
    }
    return failures;
}

M2RIG_TEST(math, coordsys_mesh_conversion_moves_verts_and_bounds) {
    // Mesh conversion must move positions/normals by M and refresh bounds.
    // Normals are rotated, never recomputed (imported shading is preserved).
    int failures = 0;
    Mesh mesh;
    mesh.name = "zup-quad";
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 1};
    mesh.vertices[1].position = {1, 0, 1};
    mesh.vertices[2].position = {0, 1, 1};
    for (auto& v : mesh.vertices) v.normal = {0, 0, 1};
    mesh.indices = {0, 1, 2};
    mesh.materials.push_back({"t.dds", "t.dds"});
    computeBounds(mesh);
    const auto& profile = fbxConversionProfile();
    CHECK_TRUE(applyConversionProfile(profile, CoordSys::ZUp_YForward, mesh).succeeded());
    CHECK_NEAR(mesh.vertices[0].position.x, 0.0f, 1e-6);
    CHECK_NEAR(mesh.vertices[0].position.y, 1.0f, 1e-6);
    CHECK_NEAR(mesh.vertices[0].position.z, 0.0f, 1e-6);
    // (0,0,1) normal (Z-up up) -> (0,1,0) canonical up.
    CHECK_NEAR(mesh.vertices[0].normal.x, 0.0f, 1e-6);
    CHECK_NEAR(mesh.vertices[0].normal.y, 1.0f, 1e-6);
    CHECK_NEAR(mesh.vertices[0].normal.z, 0.0f, 1e-6);
    CHECK_FALSE(mesh.bounds.empty);
    CHECK_NEAR(mesh.bounds.min.y, 1.0f, 1e-5);
    CHECK_NEAR(mesh.bounds.max.y, 1.0f, 1e-5);
    return failures;
}

M2RIG_TEST(math, camera_apply_preset_view_mapping) {
    // applyPreset directions are camera offsets (eye = target + dir*distance).
    // Front must face a +Z-facing character (sample toes/stip at +Z, see
    // samples.cpp): Front -> yaw 0 (eye at +Z), Back -> yaw +-pi (eye at -Z).
    // Top/Bottom carry a 0.001 tilt so pitch never lands on exactly +-pi/2,
    // where the view direction parallels world-up and lookAt degenerates.
    int failures = 0;
    auto applyAndFinish = [&](CameraPreset p) {
        ArcballCamera cam;
        cam.applyPreset(p, 100.0, 0.3);
        cam.updateSmooth(100.31);
        return cam;
    };
    {
        const ArcballCamera front = applyAndFinish(CameraPreset::Front);
        CHECK_NEAR(front.yaw, 0.0f, 1e-4f);
    }
    {
        const ArcballCamera back = applyAndFinish(CameraPreset::Back);
        CHECK_NEAR(std::fabs(back.yaw), kPi, 1e-4f);
    }
    {
        const ArcballCamera left = applyAndFinish(CameraPreset::Left);
        CHECK_NEAR(left.yaw, -kPi * 0.5f, 1e-4f);
    }
    {
        const ArcballCamera right = applyAndFinish(CameraPreset::Right);
        CHECK_NEAR(right.yaw, kPi * 0.5f, 1e-4f);
    }
    {
        ArcballCamera top = applyAndFinish(CameraPreset::Top);
        CHECK_TRUE(top.pitch > 1.55f && top.pitch < kPi * 0.5f);
        const Vec3 e = top.eye();
        CHECK_TRUE(isFiniteF(e.x) && isFiniteF(e.y) && isFiniteF(e.z));
        CHECK_TRUE(matAllFinite(top.viewMatrix()));
    }
    {
        ArcballCamera bottom = applyAndFinish(CameraPreset::Bottom);
        CHECK_TRUE(bottom.pitch < -1.55f && bottom.pitch > -kPi * 0.5f);
        const Vec3 e = bottom.eye();
        CHECK_TRUE(isFiniteF(e.x) && isFiniteF(e.y) && isFiniteF(e.z));
        CHECK_TRUE(matAllFinite(bottom.viewMatrix()));
    }
    return failures;
}

M2RIG_TEST(math, grid_blue_axis_points_positive_z) {
    // The RGB triad must show +X/+Y/+Z: the blue shaft runs origin -> +axisLen
    // with its arrowhead at +axisLen. A shaft/head split across the origin
    // renders a detached, misleading axis.
    int failures = 0;
    const float half = 5.0f;
    const std::vector<GpuVertex> lines = buildGridLines(half, 0.5f);
    const float axisLen = half * 0.3f;
    const float ay = axisLen * 0.007f;
    auto isBlue = [](const GpuVertex& v) {
        return std::fabs(v.color[0] - 0.4f) < 1e-6f && std::fabs(v.color[1] - 0.6f) < 1e-6f &&
               std::fabs(v.color[2] - 1.0f) < 1e-6f;
    };
    bool shaftFound = false, negativeFound = false;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
        const GpuVertex& a = lines[i];
        const GpuVertex& b = lines[i + 1];
        if (!isBlue(a) || !isBlue(b)) continue;
        const bool aAtOrigin = std::fabs(a.position.x) < 1e-6f &&
                               std::fabs(a.position.y - ay) < 1e-5f &&
                               std::fabs(a.position.z) < 1e-6f;
        if (!aAtOrigin) continue;
        if (std::fabs(b.position.x) < 1e-6f && std::fabs(b.position.y - ay) < 1e-5f) {
            if (std::fabs(b.position.z - axisLen) < 1e-5f) shaftFound = true;
            if (std::fabs(b.position.z + axisLen) < 1e-5f) negativeFound = true;
        }
    }
    CHECK_TRUE(shaftFound);
    CHECK_TRUE(!negativeFound);
    return failures;
}

M2RIG_TEST(math, camera_orbit_applies_dpi_scale) {
    // High-DPI displays report logical-pixel deltas; orbit/pan scale by the
    // framebuffer factor so sensitivity stays constant across DPI settings.
    int failures = 0;
    ArcballCamera a, b;
    b.setDpiScale(2.0f, 2.0f);
    a.orbit(100.0f, 0.0f);
    b.orbit(100.0f, 0.0f);
    CHECK_NEAR(b.yaw / a.yaw, 2.0f, 1e-4f);
    ArcballCamera c, d;
    d.setDpiScale(2.0f, 2.0f);
    const Vec3 t0 = c.target;
    c.pan(100.0f, 0.0f);
    d.pan(100.0f, 0.0f);
    CHECK_NEAR(distance(d.target, t0) / distance(c.target, t0), 2.0f, 1e-3f);
    return failures;
}
