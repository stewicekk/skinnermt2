#pragma once
// Procedural sky + spherical-harmonics irradiance (Wave 25b, IBL).
// Zero third-party deps, portable C++20 (runs on any host; the GPU path in
// src/renderer.cpp mirrors the analytic sky + SH basis EXACTLY — same
// constants, same ordering — and the composed pixel test pins the sum).
//
// Convention (shared with HLSL, do not change one side alone):
// right-handed Y-up world; SH order L00, L1-1, L10, L11, L2-2, L2-1, L20,
// L21, L22 over basis 1, y, z, x, xy, yz, 3z^2-1, xz, x^2-y^2 (Green/Sloane
// with z as the polar axis). Stored coefficients are BAND-SCALED
// (A_l * L_lm with A_0=pi, A_1=2pi/3, A_2=pi/4), so shIrradiance is a pure
// basis evaluation — and a uniform sky of radiance L yields exactly L*pi.
#include <cstddef>
#include <cstdint>

#include "m2rig/math.hpp"

namespace m2rig {

// Analytic sky (linear HDR-ish values; exposure folds real-world scale into
// display range — there is no tonemapper yet, so keep the composed output
// in LDR by construction and document the knob here, not in magic code).
struct IblSkyParams {
    Vec3 zenith{0.30f, 0.50f, 0.85f};
    Vec3 horizon{0.72f, 0.78f, 0.84f};
    Vec3 ground{0.24f, 0.21f, 0.19f};
    Vec3 sunDir{0.3995f, 0.7990f, 0.4494f};  // normalize(0.4, 0.8, 0.45)
    Vec3 sunColor{1.0f, 0.96f, 0.90f};
    float sunIntensity = 10.0f;  // disc multiplier
    float glowIntensity = 1.2f;
    float glowPower = 600.0f;
    float cosInner = 0.9994f;  // sun disc inner edge (~2 deg)
    float cosOuter = 0.9988f;  // sun disc outer edge (~2.8 deg)
    float exposure = 0.4f;     // global display-range knob (see above)
};

IblSkyParams defaultSky();

// Sky radiance for a (not necessarily normalized) direction.
Vec3 evalSky(const IblSkyParams& sky, const Vec3& dir);

// Band-scaled SH coefficients (c[band-order][rgb], see file contract).
struct SphericalHarmonics {
    float c[9][3];
};

SphericalHarmonics projectSky(const IblSkyParams& sky, int thetaSteps = 64,
                              int phiSteps = 128);
// Diffuse irradiance for a world-space normal (pure basis evaluation).
Vec3 shIrradiance(const SphericalHarmonics& sh, const Vec3& n);

// IEEE-754 float <-> binary16 for FP16 texture uploads (base cubemap).
// floatToHalf clamps inf/NaN to max-finite (never propagates non-finite
// into the environment chain), flushes denormals/underflow to zero, and
// truncates (not round-to-nearest: worst error 1 ulp, covered by test).
std::uint16_t floatToHalf(float f);
float halfToFloat(std::uint16_t h);

}  // namespace m2rig
