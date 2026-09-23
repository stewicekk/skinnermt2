#pragma once
// Minimal header-only math layer (row-major, DirectX11-ready).
// Mirrors the subset of DirectXMath the renderer needs so the core stays
// dependency-free; the D3D11 backend uploads these layouts directly.
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace m2rig {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

inline bool isFiniteF(float v) { return std::isfinite(v) != 0; }

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    float& operator[](std::size_t i) { return (&x)[i]; }
    const float& operator[](std::size_t i) const { return (&x)[i]; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float lengthSq(const Vec3& v) { return dot(v, v); }
inline float distance(const Vec3& a, const Vec3& b) { return length(a - b); }
inline Vec3 normalized(const Vec3& v) {
    const float l = length(v);
    return l > 1e-12f ? v / l : Vec3{0, 0, 0};
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
};

// Row-major 4x4. m[row][col]. Compatible with D3D11 default (row-major)
// when uploaded directly and multiplied as row vectors: v' = v * M.
struct Mat4 {
    float m[4][4] = {};

    static Mat4 identity() {
        Mat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }
    static Mat4 translation(const Vec3& t) {
        Mat4 r = identity();
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }
    static Mat4 scaling(const Vec3& s) {
        Mat4 r = identity();
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        return r;
    }
    static Mat4 rotationX(float rad) {
        Mat4 r = identity();
        const float c = std::cos(rad), s = std::sin(rad);
        r.m[1][1] = c;
        r.m[1][2] = s;
        r.m[2][1] = -s;
        r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationY(float rad) {
        Mat4 r = identity();
        const float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][2] = -s;
        r.m[2][0] = s;
        r.m[2][2] = c;
        return r;
    }
    static Mat4 rotationZ(float rad) {
        Mat4 r = identity();
        const float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][1] = s;
        r.m[1][0] = -s;
        r.m[1][1] = c;
        return r;
    }
    // Valve SMD / Metin2 euler order XYZ (radians), applied as Rx * Ry * Rz
    // in row-vector convention.
    static Mat4 rotationEulerXyz(const Vec3& eulerRad) {
        return rotationX(eulerRad.x) * rotationY(eulerRad.y) * rotationZ(eulerRad.z);
    }
    static Mat4 compose(const Vec3& t, const Vec3& eulerRad, const Vec3& s) {
        Mat4 r = scaling(s);
        r = r * rotationEulerXyz(eulerRad);
        r.m[3][0] = t.x;
        r.m[3][1] = t.y;
        r.m[3][2] = t.z;
        return r;
    }
    // Right-handed projection for D3D depth [0,1], matching lookAt() above
    // (camera looks down -z, view-space z is negative in front). Maps
    // z=-near -> 0, z=-far -> 1 with w=-z > 0 (no x/y mirror).
    static Mat4 perspectiveFov(float fovY, float aspect, float nearZ, float farZ) {
        Mat4 r;
        const float h = 1.0f / std::tan(fovY * 0.5f);
        const float w = h / aspect;
        r.m[0][0] = w;
        r.m[1][1] = h;
        r.m[2][2] = farZ / (nearZ - farZ);
        r.m[2][3] = -1.0f;
        r.m[3][2] = nearZ * farZ / (nearZ - farZ);
        return r;
    }
    // Coordinate system conversion: Z-up (Blender/FBX/Granny: X=right,
    // Y=forward, Z=up) -> canonical Y-up (X=right, Y=up, Z=-forward).
    // (x, y, z) -> (x, z, -y): +90 deg rotation of points about +X.
    // Verified: Z-up up (0,0,1) -> (0,1,0); Z-up forward (0,1,0) -> (0,0,-1).
    // Single source of truth — coordsys profiles reuse this, never re-derive.
    static Mat4 convertZUpToYUp() {
        Mat4 r = identity();
        r.m[1][1] = 0.0f;
        r.m[1][2] = -1.0f;
        r.m[2][1] = 1.0f;
        r.m[2][2] = 0.0f;
        return r;
    }
    static Mat4 orthographic(float width, float height, float nearZ, float farZ) {
        Mat4 r = identity();
        r.m[0][0] = 2.0f / width;
        r.m[1][1] = 2.0f / height;
        r.m[2][2] = 1.0f / (nearZ - farZ);
        r.m[3][2] = nearZ / (nearZ - farZ);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        const Vec3 z = normalized(eye - target);
        const Vec3 x = normalized(cross(up, z));
        const Vec3 y = cross(z, x);
        Mat4 r = identity();
        r.m[0][0] = x.x;
        r.m[0][1] = y.x;
        r.m[0][2] = z.x;
        r.m[1][0] = x.y;
        r.m[1][1] = y.y;
        r.m[1][2] = z.y;
        r.m[2][0] = x.z;
        r.m[2][1] = y.z;
        r.m[2][2] = z.z;
        r.m[3][0] = -dot(x, eye);
        r.m[3][1] = -dot(y, eye);
        r.m[3][2] = -dot(z, eye);
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }
    Vec3 transformPoint(const Vec3& v) const {
        const float w = v.x * m[0][3] + v.y * m[1][3] + v.z * m[2][3] + m[3][3];
        const float inv = (w != 0.0f) ? 1.0f / w : 1.0f;
        return {(v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + m[3][0]) * inv,
                (v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + m[3][1]) * inv,
                (v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + m[3][2]) * inv};
    }
    Vec3 transformVector(const Vec3& v) const {
        return {v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
                v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
                v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]};
    }
    // Extracts XYZ euler angles (radians) from the upper 3x3, inverse of
    // rotationEulerXyz (R = Rx * Ry * Rz, row-vector convention). Assumes a
    // pure rotation (no scale/shear); gimbal lock (|cos y| ~ 0) folds z into x.
    Vec3 eulerXyzFromRotation() const {
        const float sy = -m[0][2];
        const float cy = std::sqrt(std::max(0.0f, 1.0f - sy * sy));
        float x, y, z;
        if (cy > 1e-6f) {
            y = std::asin(sy < -1.0f ? -1.0f : (sy > 1.0f ? 1.0f : sy));
            x = std::atan2(m[1][2], m[2][2]);
            z = std::atan2(m[0][1], m[0][0]);
        } else {
            y = sy > 0.0f ? kPi * 0.5f : -kPi * 0.5f;
            z = 0.0f;
            x = sy > 0.0f ? std::atan2(m[1][0], m[1][1]) : std::atan2(-m[1][0], m[1][1]);
        }
        return {x, y, z};
    }
    // Inverse of a rigid transform (rotation+translation, uniform scale 1).
    Mat4 inverseRigid() const {
        Mat4 r = identity();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        const Vec3 t{m[3][0], m[3][1], m[3][2]};
        const Vec3 it = r.transformVector({-t.x, -t.y, -t.z});
        r.m[3][0] = it.x;
        r.m[3][1] = it.y;
        r.m[3][2] = it.z;
        return r;
    }
    // General 4x4 inverse (Gauss-Jordan). Returns identity when singular.
    Mat4 inverseGeneral() const {
        float a[4][8] = {};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) a[r][c] = m[r][c];
            a[r][4 + r] = 1.0f;
        }
        for (int col = 0; col < 4; ++col) {
            int pivot = col;
            for (int r = col + 1; r < 4; ++r)
                if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
            if (std::fabs(a[pivot][col]) < 1e-12f) return identity();
            if (pivot != col)
                for (int c = 0; c < 8; ++c) {
                    const float t = a[col][c];
                    a[col][c] = a[pivot][c];
                    a[pivot][c] = t;
                }
            const float inv = 1.0f / a[col][col];
            for (int c = 0; c < 8; ++c) a[col][c] *= inv;
            for (int r = 0; r < 4; ++r) {
                if (r == col) continue;
                const float f = a[r][col];
                for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
            }
        }
        Mat4 out;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) out.m[r][c] = a[r][4 + c];
        return out;
    }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    static Quat fromEulerXyz(const Vec3& e) {
        const float cx = std::cos(e.x * 0.5f), sx = std::sin(e.x * 0.5f);
        const float cy = std::cos(e.y * 0.5f), sy = std::sin(e.y * 0.5f);
        const float cz = std::cos(e.z * 0.5f), sz = std::sin(e.z * 0.5f);
        Quat q;
        q.w = cx * cy * cz + sx * sy * sz;
        q.x = sx * cy * cz - cx * sy * sz;
        q.y = cx * sy * cz + sx * cy * sz;
        q.z = cx * cy * sz - sx * sy * cz;
        return q;
    }
    Mat4 toMatrix() const {
        Mat4 r = Mat4::identity();
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;
        r.m[0][0] = 1 - 2 * (yy + zz);
        r.m[0][1] = 2 * (xy + wz);
        r.m[0][2] = 2 * (xz - wy);
        r.m[1][0] = 2 * (xy - wz);
        r.m[1][1] = 1 - 2 * (xx + zz);
        r.m[1][2] = 2 * (yz + wx);
        r.m[2][0] = 2 * (xz + wy);
        r.m[2][1] = 2 * (yz - wx);
        r.m[2][2] = 1 - 2 * (xx + yy);
        return r;
    }
    Quat normalized() const {
        const float len = std::sqrt(x*x + y*y + z*z + w*w);
        return len > 1e-12f ? Quat{x/len, y/len, z/len, w/len} : Quat{0,0,0,1};
    }
    static Quat slerp(const Quat& a, const Quat& b, float t) {
        float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        Quat end = b;
        if (dot < 0.0f) { dot = -dot; end = Quat{-b.x, -b.y, -b.z, -b.w}; }
        dot = std::max(-1.0f, std::min(1.0f, dot));
        const float theta = std::acos(dot) * t;
        const float sinTheta = std::sin(theta);
        const float sinTheta0 = std::sin(std::acos(dot));
        if (sinTheta0 < 1e-6f) return a;
        const float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
        const float s1 = sinTheta / sinTheta0;
        return Quat{a.x*s0 + end.x*s1, a.y*s0 + end.y*s1, a.z*s0 + end.z*s1, a.w*s0 + end.w*s1};
    }
};

// Quaternion multiplication
inline Quat mul(const Quat& a, const Quat& b) {
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

// Dual Quaternion for DQS skinning (two quaternions: real + dual)
struct DualQuat {
    Quat real;   // rotation
    Quat dual;   // translation * 0.5 * real
    
    static DualQuat fromTransform(const Vec3& translation, const Quat& rotation) {
        DualQuat dq;
        dq.real = rotation.normalized();
        // dual = 0.5 * translation * real (quaternion multiplication)
        Quat t{translation.x * 0.5f, translation.y * 0.5f, translation.z * 0.5f, 0.0f};
        dq.dual = mul(t, dq.real);
        return dq;
    }
    
    static DualQuat fromMatrix(const Mat4& m) {
        Vec3 t{m.m[3][0], m.m[3][1], m.m[3][2]};
        // Extract rotation from upper 3x3
        Quat r;
        // Use matrix to quaternion conversion
        float tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
        if (tr > 0.0f) {
            float s = std::sqrt(tr + 1.0f) * 2.0f;
            r.w = 0.25f * s;
            r.x = (m.m[2][1] - m.m[1][2]) / s;
            r.y = (m.m[0][2] - m.m[2][0]) / s;
            r.z = (m.m[1][0] - m.m[0][1]) / s;
        } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
            float s = std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
            r.w = (m.m[2][1] - m.m[1][2]) / s;
            r.x = 0.25f * s;
            r.y = (m.m[0][1] + m.m[1][0]) / s;
            r.z = (m.m[0][2] + m.m[2][0]) / s;
        } else if (m.m[1][1] > m.m[2][2]) {
            float s = std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
            r.w = (m.m[0][2] - m.m[2][0]) / s;
            r.x = (m.m[0][1] + m.m[1][0]) / s;
            r.y = 0.25f * s;
            r.z = (m.m[1][2] + m.m[2][1]) / s;
        } else {
            float s = std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
            r.w = (m.m[1][0] - m.m[0][1]) / s;
            r.x = (m.m[0][2] + m.m[2][0]) / s;
            r.y = (m.m[1][2] + m.m[2][1]) / s;
            r.z = 0.25f * s;
        }
        r = r.normalized();
        return fromTransform(t, r);
    }
    
    Mat4 toMatrix() const {
        Mat4 m = real.toMatrix();
        m.m[3][0] = 2.0f * (dual.w * real.x - dual.x * real.w + dual.y * real.z - dual.z * real.y);
        m.m[3][1] = 2.0f * (dual.w * real.y - dual.y * real.w + dual.z * real.x - dual.x * real.z);
        m.m[3][2] = 2.0f * (dual.w * real.z - dual.z * real.w + dual.x * real.y - dual.y * real.x);
        m.m[3][3] = 1.0f;
        return m;
    }
    
    Vec3 getTranslation() const {
        return {
            2.0f * (dual.w * real.x - dual.x * real.w + dual.y * real.z - dual.z * real.y),
            2.0f * (dual.w * real.y - dual.y * real.w + dual.z * real.x - dual.x * real.z),
            2.0f * (dual.w * real.z - dual.z * real.w + dual.x * real.y - dual.y * real.x)
        };
    }
    
    Quat getRotation() const { return real; }
};

// Dual quaternion multiplication
inline DualQuat mul(const DualQuat& a, const DualQuat& b) {
    DualQuat r;
    r.real = mul(a.real, b.real);
    r.dual = Quat{
        a.real.w * b.dual.x + a.real.x * b.dual.w + a.real.y * b.dual.z - a.real.z * b.dual.y,
        a.real.w * b.dual.y - a.real.x * b.dual.z + a.real.y * b.dual.w + a.real.z * b.dual.x,
        a.real.w * b.dual.z + a.real.x * b.dual.y - a.real.y * b.dual.x + a.real.z * b.dual.w,
        a.real.w * b.dual.w - a.real.x * b.dual.x - a.real.y * b.dual.y - a.real.z * b.dual.z
    };
    return r;
}

// Dual quaternion addition (for blending)
inline DualQuat add(const DualQuat& a, const DualQuat& b) {
    return DualQuat{
        Quat{a.real.x + b.real.x, a.real.y + b.real.y, a.real.z + b.real.z, a.real.w + b.real.w},
        Quat{a.dual.x + b.dual.x, a.dual.y + b.dual.y, a.dual.z + b.dual.z, a.dual.w + b.dual.w}
    };
}

inline DualQuat scale(const DualQuat& a, float s) {
    return DualQuat{
        Quat{a.real.x * s, a.real.y * s, a.real.z * s, a.real.w * s},
        Quat{a.dual.x * s, a.dual.y * s, a.dual.z * s, a.dual.w * s}
    };
}

inline Quat scale(const Quat& a, float s) {
    return Quat{a.x * s, a.y * s, a.z * s, a.w * s};
}

// Normalize dual quaternion (normalize real part, adjust dual)
inline DualQuat normalize(const DualQuat& a) {
    DualQuat r = a;
    const float len = std::sqrt(a.real.x*a.real.x + a.real.y*a.real.y + a.real.z*a.real.z + a.real.w*a.real.w);
    if (len > 1e-12f) {
        const float invLen = 1.0f / len;
        r.real = Quat{a.real.x * invLen, a.real.y * invLen, a.real.z * invLen, a.real.w * invLen};
        r.dual = Quat{a.dual.x * invLen, a.dual.y * invLen, a.dual.z * invLen, a.dual.w * invLen};
    }
    return r;
}

struct Aabb {
    Vec3 min{0, 0, 0};
    Vec3 max{0, 0, 0};
    bool empty = true;
    void grow(const Vec3& p) {
        if (empty) {
            min = max = p;
            empty = false;
            return;
        }
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.z < min.z) min.z = p.z;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
        if (p.z > max.z) max.z = p.z;
    }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    float radius() const { return length(max - center()); }
};

}  // namespace m2rig
