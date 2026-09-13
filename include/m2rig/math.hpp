#pragma once
// Minimal header-only math layer (row-major, DirectX11-ready).
// Mirrors the subset of DirectXMath the renderer needs so the core stays
// dependency-free; the D3D11 backend uploads these layouts directly.
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
    static Mat4 perspectiveFov(float fovY, float aspect, float nearZ, float farZ) {
        Mat4 r;
        const float h = 1.0f / std::tan(fovY * 0.5f);
        const float w = h / aspect;
        r.m[0][0] = w;
        r.m[1][1] = h;
        r.m[2][2] = farZ / (farZ - nearZ);
        r.m[2][3] = 1.0f;
        r.m[3][2] = -nearZ * farZ / (farZ - nearZ);
        return r;
    }
    static Mat4 orthographic(float width, float height, float nearZ, float farZ) {
        Mat4 r = identity();
        r.m[0][0] = 2.0f / width;
        r.m[1][1] = 2.0f / height;
        r.m[2][2] = 1.0f / (farZ - nearZ);
        r.m[3][2] = -nearZ / (farZ - nearZ);
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
};

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
