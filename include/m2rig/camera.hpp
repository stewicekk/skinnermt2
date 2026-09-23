#pragma once
// Arcball orbit camera with presets (spec section 38). Real math, used by the
// D3D11 viewport for orbit/pan/zoom/frame and orthographic projections.
// Enhanced with smooth interpolation, FOV/near/far controls, and camera presets.
#include "m2rig/math.hpp"

namespace m2rig {

// Camera preset definitions for standard orthographic views
enum class CameraPreset {
    Front = 0,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    Perspective,
    Count
};

// Smooth interpolation state for camera transitions
struct CameraSmoothState {
    Vec3 targetStart{0, 1.0f, 0};
    Vec3 targetEnd{0, 1.0f, 0};
    float yawStart = 0.0f;
    float yawEnd = 0.0f;
    float pitchStart = 0.35f;
    float pitchEnd = 0.35f;
    float distanceStart = 5.0f;
    float distanceEnd = 5.0f;
    float orthoHeightStart = 4.0f;
    float orthoHeightEnd = 4.0f;
    double startTime = 0.0;
    double duration = 0.0;
    bool active = false;
};

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
    // Radius of the last framed model (0 = unknown). Drives adaptive
    // clipping + zoom limits so 1-unit samples and 500-unit FBX rigs both
    // stay inside the frustum.
    float framedRadius = 0.0f;

    // Smooth transition state
    CameraSmoothState smooth;

    // Sensitivity settings
    float orbitSensitivity = 0.005f;
    float panSensitivity = 0.001f;
    float zoomSensitivity = 0.1f;

    // DPI scale factors for correct coordinate mapping
    float dpiScaleX = 1.0f;
    float dpiScaleY = 1.0f;

    // Camera stability: track previous valid state for recovery
    Vec3 prevTarget{0, 1.0f, 0};
    float prevYaw = 0.0f;
    float prevPitch = 0.35f;
    float prevDistance = 5.0f;
    bool hasPrevState = false;

    // Keeps near/far/zoom-limit consistent with the current distance and the
    // framed model size. near tracks distance (depth precision ~50-200x),
    // far always covers the model + grid behind the camera. Sanitizes
    // poisoned state first: one NaN (bad bounds/workspace) must never lock
    // the viewport into an unrecoverable camera.
    void updateClip() {
        // Store valid state for recovery
        if (hasPrevState && isFiniteF(distance) && distance > 0.0f &&
            isFiniteF(fovY) && fovY > 0.01f && fovY < kPi - 0.01f) {
            prevTarget = target;
            prevYaw = yaw;
            prevPitch = pitch;
            prevDistance = distance;
        }
        hasPrevState = true;
        
        // Sanitize camera state - recover from poisoned state
        if (!isFiniteF(distance) || distance <= 0.0f) {
            distance = hasPrevState ? prevDistance : 5.0f;
        }
        if (!isFiniteF(framedRadius) || framedRadius < 0.0f) framedRadius = 0.0f;
        if (!isFiniteF(fovY) || fovY <= 0.01f || fovY >= kPi - 0.01f)
            fovY = 50.0f * kDegToRad;
        if (!isFiniteF(nearZ) || nearZ <= 0.0f) nearZ = 0.01f;
        if (!isFiniteF(farZ) || farZ <= nearZ) farZ = nearZ + 100.0f;
        if (!isFiniteF(target.x) || !isFiniteF(target.y) || !isFiniteF(target.z))
            target = hasPrevState ? prevTarget : Vec3{0, 1.0f, 0};
        
        const float r = framedRadius > 0.0f ? framedRadius : 2.8f;
        
        // Fixed FOV: near/far planes track distance but FOV remains constant
        // Near plane: 5% of distance, minimum 0.01, but never closer than half the model radius
        nearZ = distance * 0.05f;
        if (nearZ < 0.01f) nearZ = 0.01f;
        if (nearZ > distance - r && distance - r > 0.01f) nearZ = (distance - r) * 0.5f;
        
        // Far plane: covers model + grid behind camera, minimum 200 units
        float needFar = (distance + 2.0f * r) * 1.5f;
        const float gridFar = distance + 12.0f;
        if (needFar < gridFar) needFar = gridFar;
        if (needFar < 200.0f) needFar = 200.0f;
        farZ = needFar;
        
        // Ensure valid near/far ordering
        if (farZ <= nearZ) farZ = nearZ + 1.0f;
    }
    float maxDistance() const {
        float m = 150.0f;
        if (framedRadius > 0.0f) {
            const float need = framedRadius / std::tan(fovY * 0.5f) * 1.35f * 1.5f;
            if (need > m) m = need;
        }
        if (m > 10000.0f) m = 10000.0f;
        return m;
    }
    float maxOrthoHeight() const {
        float m = 100.0f;
        if (framedRadius > 0.0f) {
            const float need = framedRadius * 2.7f * 1.5f;
            if (need > m) m = need;
        }
        if (m > 5000.0f) m = 5000.0f;
        return m;
    }

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
    
    // DPI scale handling for correct coordinate mapping
    void setDpiScale(float scaleX, float scaleY) {
        dpiScaleX = scaleX > 0.0f ? scaleX : 1.0f;
        dpiScaleY = scaleY > 0.0f ? scaleY : 1.0f;
    }
    void getDpiScale(float& scaleX, float& scaleY) const {
        scaleX = dpiScaleX;
        scaleY = dpiScaleY;
    }
    
    // Sanitize: recover from invalid state
    void sanitize() {
        updateClip();
    }
    
    // Smooth interpolation update - call each frame with current time
    void updateSmooth(double nowSeconds) {
        if (!smooth.active) return;
        const double t = (nowSeconds - smooth.startTime) / smooth.duration;
        if (t >= 1.0) {
            target = smooth.targetEnd;
            yaw = smooth.yawEnd;
            pitch = smooth.pitchEnd;
            distance = smooth.distanceEnd;
            orthoHeight = smooth.orthoHeightEnd;
            smooth.active = false;
            updateClip();
            return;
        }
        // Smoothstep interpolation
        const float st = static_cast<float>(t * t * (3.0 - 2.0 * t));
        target = smooth.targetStart + (smooth.targetEnd - smooth.targetStart) * st;
        yaw = smooth.yawStart + (smooth.yawEnd - smooth.yawStart) * st;
        pitch = smooth.pitchStart + (smooth.pitchEnd - smooth.pitchStart) * st;
        distance = smooth.distanceStart + (smooth.distanceEnd - smooth.distanceStart) * st;
        orthoHeight = smooth.orthoHeightStart + (smooth.orthoHeightEnd - smooth.orthoHeightStart) * st;
        updateClip();
    }

    void orbit(float dx, float dy) {
        // Standard orbit: right drag -> yaw increases (orbit right), up drag -> pitch decreases (look up)
        // Apply DPI scale for correct sensitivity on high-DPI displays
        yaw += dx * orbitSensitivity * dpiScaleX;
        pitch -= dy * orbitSensitivity * dpiScaleY;
        
        // Full 360° rotation support: allow pitch to go beyond poles
        // by wrapping pitch and flipping yaw at poles (quaternion-like behavior)
        // This avoids gimbal lock while enabling full vertical rotation
        constexpr float halfPi = kPi * 0.5f;
        constexpr float eps = 0.01f; // Small epsilon to avoid exact pole
        
        // Handle multiple pole crossings by normalizing pitch
        while (pitch > halfPi - eps || pitch < -halfPi + eps) {
            if (pitch > halfPi - eps) {
                // Crossed north pole: reflect pitch and add 180° to yaw
                pitch = halfPi - eps - (pitch - (halfPi - eps));
                yaw += kPi;
            } else if (pitch < -halfPi + eps) {
                // Crossed south pole: reflect pitch and add 180° to yaw
                pitch = -halfPi + eps - (pitch - (-halfPi + eps));
                yaw += kPi;
            }
        }
        
        // Yaw accumulates continuously for true 360°+ horizontal rotation.
        // Only normalize for display/debug; never clamp the working value.
        // Use fmod to keep it in a reasonable range without sudden jumps.
        if (yaw > kPi || yaw < -kPi) {
            yaw = std::fmod(yaw, 2.0f * kPi);
            if (yaw > kPi) yaw -= 2.0f * kPi;
            if (yaw < -kPi) yaw += 2.0f * kPi;
        }
        
        smooth.active = false; // Cancel smooth on user input
    }
    void pan(float dx, float dy) {
        const Vec3 e = eye();
        const Vec3 fwd = normalized(target - e);
        const Vec3 right = normalized(cross(fwd, {0, 1, 0}));
        const Vec3 up = cross(right, fwd);
        // Screen-space pan: drag right -> target moves left (camera orbits right), drag up -> target moves down (camera orbits up)
        // Apply DPI scale for correct sensitivity on high-DPI displays
        const float s = distance * panSensitivity;
        target = target - right * (dx * s * dpiScaleX) + up * (dy * s * dpiScaleY);
        smooth.active = false; // Cancel smooth on user input
    }
    void zoom(float wheel) {
        // Apply DPI scale for consistent zoom speed on high-DPI displays
        const float scaledWheel = wheel * dpiScaleY;
        if (orthographic) {
            orthoHeight *= (scaledWheel > 0 ? (1.0f - zoomSensitivity) : (1.0f + zoomSensitivity));
            if (orthoHeight < 0.02f) orthoHeight = 0.02f;
            const float mx = maxOrthoHeight();
            if (orthoHeight > mx) orthoHeight = mx;
        } else {
            distance *= (scaledWheel > 0 ? (1.0f - zoomSensitivity) : (1.0f + zoomSensitivity));
            if (distance < 0.1f) distance = 0.1f;
            const float mx = maxDistance();
            if (distance > mx) distance = mx;
        }
        updateClip();
        smooth.active = false; // Cancel smooth on user input
    }
    void frameAabb(const Aabb& box) {
        if (box.empty) return;
        const Vec3 c = box.center();
        const float r = box.radius();
        if (!isFiniteF(c.x) || !isFiniteF(c.y) || !isFiniteF(c.z) || !isFiniteF(r) || r < 0.0f)
            return;  // keep the previous framing, never poison the camera
        target = c;
        framedRadius = r;
        distance = r / std::tan(fovY * 0.5f) * 1.35f;
        if (distance < 0.5f) distance = 0.5f;
        const float mx = maxDistance();
        if (distance > mx) distance = mx;
        orthoHeight = r * 2.7f;
        if (orthoHeight < 0.1f) orthoHeight = 0.1f;
        const float mo = maxOrthoHeight();
        if (orthoHeight > mo) orthoHeight = mo;
        updateClip();
    }
    
    // Smooth frame with interpolation
    void frameAabbSmooth(const Aabb& box, double nowSeconds, double duration = 0.5) {
        if (box.empty) return;
        const Vec3 c = box.center();
        const float r = box.radius();
        if (!isFiniteF(c.x) || !isFiniteF(c.y) || !isFiniteF(c.z) || !isFiniteF(r) || r < 0.0f)
            return;
        
        smooth.active = true;
        smooth.startTime = nowSeconds;
        smooth.duration = duration;
        smooth.targetStart = target;
        smooth.targetEnd = c;
        smooth.yawStart = yaw;
        smooth.yawEnd = yaw; // Keep current yaw/pitch for framing
        smooth.pitchStart = pitch;
        smooth.pitchEnd = pitch;
        smooth.distanceStart = distance;
        smooth.distanceEnd = r / std::tan(fovY * 0.5f) * 1.35f;
        if (smooth.distanceEnd < 0.5f) smooth.distanceEnd = 0.5f;
        const float mx = maxDistance();
        if (smooth.distanceEnd > mx) smooth.distanceEnd = mx;
        smooth.orthoHeightStart = orthoHeight;
        smooth.orthoHeightEnd = r * 2.7f;
        if (smooth.orthoHeightEnd < 0.1f) smooth.orthoHeightEnd = 0.1f;
        const float mo = maxOrthoHeight();
        if (smooth.orthoHeightEnd > mo) smooth.orthoHeightEnd = mo;
    }

    void preset(const Vec3& dir) {
        const Vec3 d = normalized(dir);
        yaw = std::atan2(d.x, d.z);
        pitch = std::asin(d.y > 1 ? 1 : (d.y < -1 ? -1 : d.y));
    }
    
    // Apply a camera preset with optional smooth transition.
    // Direction = camera offset from target (eye = target + dir*distance).
    // Front puts the eye at +Z looking -Z at the face of a +Z-facing
    // character (sample armor toes/stip sit at +Z); Back is the opposite.
    // Top/Bottom carry a 0.001 tilt: exact +-90 deg pitch parallels
    // world-up and degenerates lookAt (cross(up, z) == 0).
    void applyPreset(CameraPreset preset, double nowSeconds, double duration = 0.3) {
        switch (preset) {
            case CameraPreset::Front:   presetSmooth({0, 0, 1}, nowSeconds, duration); break;
            case CameraPreset::Back:    presetSmooth({0, 0, -1}, nowSeconds, duration); break;
            case CameraPreset::Left:    presetSmooth({-1, 0, 0}, nowSeconds, duration); break;
            case CameraPreset::Right:   presetSmooth({1, 0, 0}, nowSeconds, duration); break;
            case CameraPreset::Top:     presetSmooth({0, 1, 0.001f}, nowSeconds, duration); break;
            case CameraPreset::Bottom:  presetSmooth({0, -1, 0.001f}, nowSeconds, duration); break;
            case CameraPreset::Perspective:
                orthographic = false;
                break;
        }
    }
    
    void presetSmooth(const Vec3& dir, double nowSeconds, double duration) {
        const Vec3 d = normalized(dir);
        const float newYaw = std::atan2(d.x, d.z);
        const float newPitch = std::asin(d.y > 1 ? 1 : (d.y < -1 ? -1 : d.y));
        
        smooth.active = true;
        smooth.startTime = nowSeconds;
        smooth.duration = duration;
        smooth.targetStart = target;
        smooth.targetEnd = target; // Keep target for view presets
        smooth.yawStart = yaw;
        smooth.yawEnd = newYaw;
        smooth.pitchStart = pitch;
        smooth.pitchEnd = newPitch;
        smooth.distanceStart = distance;
        smooth.distanceEnd = distance;
        smooth.orthoHeightStart = orthoHeight;
        smooth.orthoHeightEnd = orthoHeight;
    }
    
    // Set orthographic/perspective mode with optional smooth transition
    void setOrthographic(bool ortho, double nowSeconds, double duration = 0.2) {
        if (orthographic == ortho) return;
        if (duration <= 0.0) {
            orthographic = ortho;
            return;
        }
        // For mode switch, we interpolate orthoHeight/distance
        smooth.active = true;
        smooth.startTime = nowSeconds;
        smooth.duration = duration;
        smooth.targetStart = target;
        smooth.targetEnd = target;
        smooth.yawStart = yaw;
        smooth.yawEnd = yaw;
        smooth.pitchStart = pitch;
        smooth.pitchEnd = pitch;
        smooth.distanceStart = distance;
        smooth.distanceEnd = distance;
        smooth.orthoHeightStart = orthoHeight;
        smooth.orthoHeightEnd = ortho ? (distance * 0.8f) : (orthoHeight / 0.8f);
        // The actual mode switch happens at the end
        // We'll handle this in updateSmooth by checking a flag
        // For simplicity, just switch immediately but keep smooth for other params
        orthographic = ortho;
    }
};

}  // namespace m2rig
