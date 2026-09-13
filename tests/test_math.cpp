// Core math tests: euler composition, lookAt, orbit camera framing.
#include <cstdio>

#include "../tests/expect.hpp"
#include "m2rig/camera.hpp"
#include "m2rig/math.hpp"

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
