#pragma once
// Arcball orbit camera with presets (spec section 38). Real math, used by the
// D3D11 viewport for orbit/pan/zoom/frame and orthographic projections.
#include "m2rig/math.hpp"

namespace m2rig {

struct ArcballCamera {
    Vec3 target{0, 1.0f, 0};
    float yaw = 0.0f;    // radians, around Y
    float pitch = 0.35f;  // radians, clamped
    float distance = 5.0f;
    float fovY = 50.0f * kDegToRad;
    float nearZ = 0.05f;
    float farZ = 200.0f;
    bool orthographic = false;
    float orthoHeight = 4.0f;

    Vec3 eye() const {
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        return {target.x + distance * cp * sy, target.y + distance * sp,
                target.z + distance * cp * cy};
    }
    Mat4 viewMatrix() const { return Mat4::lookAt(eye(), target, {0, 1, 0}); }
    Mat4 projMatrix(float aspect) const {
        if (orthographic) {
            const float h = orthoHeight;
            return Mat4::orthographic(h * aspect, h, nearZ, farZ);
        }
        return Mat4::perspectiveFov(fovY, aspect, nearZ, farZ);
    }
    void orbit(float dx, float dy) {
        yaw -= dx * 0.008f;
        pitch += dy * 0.008f;
        if (pitch > 1.55f) pitch = 1.55f;
        if (pitch < -1.55f) pitch = -1.55f;
    }
    void pan(float dx, float dy) {
        const Vec3 e = eye();
        const Vec3 fwd = normalized(target - e);
        const Vec3 right = normalized(cross(fwd, {0, 1, 0}));
        const Vec3 up = cross(right, fwd);
        const float s = distance * 0.0016f;
        target = target - right * (dx * s) + up * (dy * s);
    }
    void zoom(float wheel) {
        if (orthographic) {
            orthoHeight *= (wheel > 0 ? 0.9f : 1.1f);
            if (orthoHeight < 0.1f) orthoHeight = 0.1f;
            if (orthoHeight > 100.0f) orthoHeight = 100.0f;
        } else {
            distance *= (wheel > 0 ? 0.9f : 1.1f);
            if (distance < 0.1f) distance = 0.1f;
            if (distance > 150.0f) distance = 150.0f;
        }
    }
    void frameAabb(const Aabb& box) {
        if (box.empty) return;
        target = box.center();
        const float r = box.radius();
        distance = r / std::tan(fovY * 0.5f) * 1.35f;
        if (distance < 0.5f) distance = 0.5f;
        orthoHeight = r * 2.7f;
    }
    void preset(const Vec3& dir) {
        const Vec3 d = normalized(dir);
        yaw = std::atan2(d.x, d.z);
        pitch = std::asin(d.y > 1 ? 1 : (d.y < -1 ? -1 : d.y));
    }
};

}  // namespace m2rig
