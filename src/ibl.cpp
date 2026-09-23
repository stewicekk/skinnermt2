// Procedural sky + SH irradiance. Contract in m2rig/ibl.hpp.
// GPU generation path: the base cubemap is uploaded from evalSky (this
// file — exact by construction), the HLSL prefilter/BRDF mirror the GGX
// math, and the composed pixel test pins the sum. SH basis/order must
// match the HLSL evaluator; change one side only with proof.
#include "m2rig/ibl.hpp"

#include <bit>
#include <cmath>

namespace m2rig {

IblSkyParams defaultSky() { return IblSkyParams{}; }

namespace {

float saturateF(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

float smoothstepF(float edge0, float edge1, float x) {
    const float t = saturateF((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

Vec3 evalSky(const IblSkyParams& sky, const Vec3& dir) {
    const float len = length(dir);
    const Vec3 d = len > 1e-12f ? dir / len : Vec3{0, 1, 0};
    Vec3 base;
    if (d.y >= 0.0f) {
        const float t = std::pow(saturateF(d.y), 0.6f);
        base = sky.horizon + (sky.zenith - sky.horizon) * t;
    } else {
        const float t = std::pow(saturateF(-d.y), 0.5f);
        base = sky.horizon + (sky.ground - sky.horizon) * t;
    }
    const float s = dot(d, sky.sunDir);
    const float disc = smoothstepF(sky.cosOuter, sky.cosInner, s);
    const float glow = std::pow(saturateF(s), sky.glowPower) * sky.glowIntensity;
    const Vec3 sun = sky.sunColor * (disc * sky.sunIntensity + glow);
    return (base + sun) * sky.exposure;
}

SphericalHarmonics projectSky(const IblSkyParams& sky, int thetaSteps, int phiSteps) {
    SphericalHarmonics sh{};
    for (int i = 0; i < 9; ++i) sh.c[i][0] = sh.c[i][1] = sh.c[i][2] = 0.0f;
    if (thetaSteps < 4) thetaSteps = 4;
    if (phiSteps < 8) phiSteps = 8;
    // Fixed midpoint grid over the sphere (deterministic, no RNG): full 4pi
    // coverage with analytic sin(theta) area weights.
    const float dTheta = kPi / static_cast<float>(thetaSteps);
    const float dPhi = 2.0f * kPi / static_cast<float>(phiSteps);
    // Band scales A_l (cosine-lobe convolution): pi, 2pi/3 (x3), pi/4 (x5).
    static constexpr float kBand[9] = {3.14159265f, 2.09439510f, 2.09439510f,
                                       2.09439510f, 0.78539816f, 0.78539816f,
                                       0.78539816f, 0.78539816f, 0.78539816f};
    for (int ti = 0; ti < thetaSteps; ++ti) {
        const float theta = (static_cast<float>(ti) + 0.5f) * dTheta;
        const float sinT = std::sin(theta);
        const float cosT = std::cos(theta);
        const float dw = sinT * dTheta * dPhi;
        for (int pi = 0; pi < phiSteps; ++pi) {
            const float phi = (static_cast<float>(pi) + 0.5f) * dPhi;
            const float sinP = std::sin(phi);
            const float cosP = std::cos(phi);
            // Polar axis = z (matches the documented basis ordering).
            const Vec3 d{sinT * cosP, sinT * sinP, cosT};
            const Vec3 rad = evalSky(sky, d);
            const float x = d.x, y = d.y, z = d.z;
            const float basis[9] = {
                0.282095f,          0.488603f * y,          0.488603f * z,
                0.488603f * x,      1.092548f * x * y,      1.092548f * y * z,
                0.315392f * (3.0f * z * z - 1.0f), 1.092548f * x * z,
                0.546274f * (x * x - y * y),
            };
            for (int b = 0; b < 9; ++b) {
                const float w = basis[b] * dw * kBand[b];
                sh.c[b][0] += rad.x * w;
                sh.c[b][1] += rad.y * w;
                sh.c[b][2] += rad.z * w;
            }
        }
    }
    return sh;
}

Vec3 shIrradiance(const SphericalHarmonics& sh, const Vec3& n) {
    const float x = n.x, y = n.y, z = n.z;
    const float basis[9] = {
        0.282095f,          0.488603f * y,          0.488603f * z,
        0.488603f * x,      1.092548f * x * y,      1.092548f * y * z,
        0.315392f * (3.0f * z * z - 1.0f), 1.092548f * x * z,
        0.546274f * (x * x - y * y),
    };
    Vec3 e{0, 0, 0};
    for (int b = 0; b < 9; ++b) {
        e.x += sh.c[b][0] * basis[b];
        e.y += sh.c[b][1] * basis[b];
        e.z += sh.c[b][2] * basis[b];
    }
    return e;
}

std::uint16_t floatToHalf(float f) {
    const std::uint32_t x = std::bit_cast<std::uint32_t>(f);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    const int exp = static_cast<int>((x >> 23) & 0xFFu) - 112;
    const std::uint32_t mant = x & 0x7FFFFFu;
    if (exp >= 31) return static_cast<std::uint16_t>(sign | 0x7BFFu);
    if (exp <= 0) return static_cast<std::uint16_t>(sign);
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exp) << 10) |
                                      (mant >> 13));
}

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000u) << 16;
    const std::uint32_t exp = (static_cast<std::uint32_t>(h) >> 10) & 0x1Fu;
    const std::uint32_t mant = static_cast<std::uint32_t>(h) & 0x3FFu;
    std::uint32_t x;
    if (exp == 0) {
        // Zero or denormal half: value = mantissa * 2^-24.
        const float m = static_cast<float>(mant) * 5.96046448e-08f;
        x = sign | std::bit_cast<std::uint32_t>(m);
    } else if (exp == 31) {
        x = sign | 0x7F800000u | (mant << 13);  // inf / NaN preserved
    } else {
        x = sign | ((exp + 112u) << 23) | (mant << 13);
    }
    return std::bit_cast<float>(x);
}

}  // namespace m2rig
