// DirectX 11 renderer implementation.
#include "m2rig/renderer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <wrl/client.h>

#include <algorithm>
#include <cstring>

#include "m2rig/logging.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/ibl.hpp"
#include "m2rig/dds.hpp"

namespace m2rig {

using Microsoft::WRL::ComPtr;

namespace {

const char* kShaderSrc = R"(cbuffer Frame : register(b0) { 
    float4x4 gWvp; 
    float4 gViewRot[3]; 
    float4 gLightViewAndAmbient; // xyz=lightDir, w=ambientIntensity
};
Texture2D gTex : register(t0);
SamplerState gSamp : register(s0);
struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float3 tan : TANGENT; float3 bitan : BITANGENT; float4 col : COLOR; float2 uv : TEXCOORD; };
struct VsOut { float4 pos : SV_POSITION; float3 nrmView : NORMAL; float3 tanView : TANGENT; float3 bitanView : BITANGENT; float4 col : COLOR; float2 uv : TEXCOORD; };
VsOut VsMain(VsIn i) {
    VsOut o;
    o.pos = mul(float4(i.pos, 1.0f), gWvp);
    float3x3 viewRot = float3x3(gViewRot[0].xyz, gViewRot[1].xyz, gViewRot[2].xyz);
    o.nrmView = mul(i.nrm, viewRot);
    o.tanView = i.tan;  // wire-only passthrough for the Wave-27 normal-map shader; unused by lighting
    o.bitanView = i.bitan;
    o.col = i.col;
    o.uv = i.uv;
    return o;
}
// GPU skinning palette (Wave 24, LBS): bindInverse * currentGlobal per bone,
// row-vector (v * M), uploaded per frame by setSkinningPalette (identity
// padding past the skeleton size, so short palettes read neutral bind).
cbuffer Skin : register(b1) { float4x4 gBones[256]; };
struct VsSkinIn { float3 pos : POSITION; float3 nrm : NORMAL; float3 tan : TANGENT; float3 bitan : BITANGENT; uint4 bones : BLENDINDICES; float4 weights : BLENDWEIGHT; float4 col : COLOR; float2 uv : TEXCOORD; };
VsOut VsSkinned(VsSkinIn i) {
    VsOut o;
    // Matches deformVertex/deformNormal for all accepted inputs (finite
    // non-negative weights, in-range ids): blend contributing influences,
    // renormalize by their sum, fall back to bind at ~zero. (Degenerate
    // bands — subnormal weight sums, near-zero normals — may differ in the
    // last ulp: float vs double accumulation, 1e-9 vs 1e-12 normal gate.)
    float wsum = i.weights.x + i.weights.y + i.weights.z + i.weights.w;
    float3 p = i.pos;
    float3 n = i.nrm;
    if (wsum > 1e-9f) {
        float4 sp = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float3 sn = float3(0.0f, 0.0f, 0.0f);
        sp += mul(float4(i.pos, 1.0f), gBones[i.bones.x]) * i.weights.x;
        sp += mul(float4(i.pos, 1.0f), gBones[i.bones.y]) * i.weights.y;
        sp += mul(float4(i.pos, 1.0f), gBones[i.bones.z]) * i.weights.z;
        sp += mul(float4(i.pos, 1.0f), gBones[i.bones.w]) * i.weights.w;
        sn += mul(i.nrm, (float3x3)gBones[i.bones.x]) * i.weights.x;
        sn += mul(i.nrm, (float3x3)gBones[i.bones.y]) * i.weights.y;
        sn += mul(i.nrm, (float3x3)gBones[i.bones.z]) * i.weights.z;
        sn += mul(i.nrm, (float3x3)gBones[i.bones.w]) * i.weights.w;
        p = sp.xyz / wsum;
        float3 an = sn / wsum;
        float al = length(an);
        n = al > 1e-9f ? an / al : float3(0.0f, 0.0f, 0.0f);
    }
    o.pos = mul(float4(p, 1.0f), gWvp);
    float3x3 viewRot = float3x3(gViewRot[0].xyz, gViewRot[1].xyz, gViewRot[2].xyz);
    o.nrmView = mul(n, viewRot);
    o.tanView = i.tan;  // wire-only passthrough (no skinning of the tangent frame yet; C2)
    o.bitanView = i.bitan;
    o.col = i.col;
    o.uv = i.uv;
    return o;
}
float3 BlinnSpec(float3 N, float3 L, float3 V) {
    float3 H = normalize(L + V);
    float nh = saturate(dot(N, H));
    float spec = pow(nh, 64.0f) * step(0.001f, dot(N, L));
    return spec * 0.35f;
}
float3 FresnelSchlick(float3 F0, float3 V, float3 H) {
    return F0 + (1.0f - F0) * pow(1.0f - saturate(dot(V, H)), 5.0f);
}
// sRGB <-> linear (exact piecewise curves). All lighting runs in linear:
// albedo texels arrive linear via the UNORM_SRGB SRV, vertex colors are
// display-referred and converted on entry, output is encoded on exit.
// PsFlat stays a display passthrough (debug ramps/lines are exact).
float3 SrgbToLinear(float3 c) {
    float3 lo = c / 12.92f;
    float3 hi = pow((c + 0.055f) / 1.055f, 2.4f);
    return lerp(lo, hi, step(0.04045f, c));
}
float3 LinearToSrgb(float3 c) {
    float3 lo = c * 12.92f;
    float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return lerp(lo, hi, step(0.0031308f, c));
}
float4 PsMain(VsOut i) : SV_TARGET {
    // Vertex colors are display-referred: convert once, light in linear.
    float3 albedo = SrgbToLinear(i.col.rgb);
    float3 N = normalize(i.nrmView);
    float3 L = normalize(gLightViewAndAmbient.xyz);
    float3 V = float3(0.0f, 0.0f, 1.0f);
    float nd = saturate(dot(N, L));
    // Ambient (hardcoded color for now)
    float3 ambient = float3(0.1f, 0.1f, 0.15f) * gLightViewAndAmbient.w * albedo;
    // Diffuse
    float3 diffuse = albedo * nd * 0.7f;
    // Specular (Blinn-Phong)
    float3 specular = BlinnSpec(N, L, V) * 0.35f;
    // Fresnel
    float3 F0 = albedo * 0.04f;
    float3 fresnel = FresnelSchlick(F0, V, normalize(L + V));
    float3 lit = ambient + diffuse + specular * fresnel;
    return float4(LinearToSrgb(saturate(lit)), i.col.a);
}
float4 PsFlat(VsOut i) : SV_TARGET { return i.col; }
// NOTE (Step 2, evidence): UNORM_SRGB *views* were tried first (hardware
// decode), but CreateShaderResourceView(SRGB) fails on the WARP test
// runtime for both mipmapped and single-level resources while the UNORM
// view succeeds — so the decode lives here in-shader, where the pixel
// test proves it on every runtime. Two variants: sRGB albedo (decode)
// vs linear data maps (raw). Revisit SRGB views in the PBR wave only
// with hardware proof in hand.
float4 PsTexSrgb(VsOut i) : SV_TARGET {
    // Albedo texel is display-referred: decode, then the shared linear pipe.
    // Mip tails are generated in gamma space (accepted D3D11 tradeoff,
    // documented at setTexture).
    float4 t = gTex.Sample(gSamp, i.uv);
    float3 albedo = SrgbToLinear(t.rgb) * SrgbToLinear(i.col.rgb);
    float3 N = normalize(i.nrmView);
    float3 L = normalize(gLightViewAndAmbient.xyz);
    float3 V = float3(0.0f, 0.0f, 1.0f);
    float nd = saturate(dot(N, L));
    float3 ambient = float3(0.1f, 0.1f, 0.15f) * gLightViewAndAmbient.w * albedo;
    // Diffuse
    float3 diffuse = albedo * nd * 0.7f;
    // Specular
    float3 specular = BlinnSpec(N, L, V) * 0.35f;
    float3 F0 = albedo * 0.04f;
    float3 fresnel = FresnelSchlick(F0, V, normalize(L + V));
    float3 lit = ambient + diffuse + specular * fresnel;
    return float4(LinearToSrgb(saturate(lit)), t.a * i.col.a);
}
float4 PsTexLinear(VsOut i) : SV_TARGET {
    // Data-map texel is already linear (normal/rough/metal/AO); only the
    // vertex color takes the display->linear trip (white in practice).
    float4 t = gTex.Sample(gSamp, i.uv);
    float3 albedo = t.rgb * SrgbToLinear(i.col.rgb);
    float3 N = normalize(i.nrmView);
    float3 L = normalize(gLightViewAndAmbient.xyz);
    float3 V = float3(0.0f, 0.0f, 1.0f);
    float nd = saturate(dot(N, L));
    float3 ambient = float3(0.1f, 0.1f, 0.15f) * gLightViewAndAmbient.w * albedo;
    float3 diffuse = albedo * nd * 0.7f;
    float3 specular = BlinnSpec(N, L, V) * 0.35f;
    float3 F0 = albedo * 0.04f;
    float3 fresnel = FresnelSchlick(F0, V, normalize(L + V));
    float3 lit = ambient + diffuse + specular * fresnel;
    return float4(LinearToSrgb(saturate(lit)), t.a * i.col.a);
}
// PBR material block (Wave 25a, punctual): factors only, no maps yet.
// Base color is linear (glTF convention: factors need no decode); vertex
// colors and texels are display-referred and decoded on entry like the
// Blinn path. Ambient is a placeholder until IBL (Wave 25b).
cbuffer PbrMat : register(b2) {
    float4 gBaseColor;  // rgb albedo factor, a unused
    float4 gPbrParams;  // x metallic, y roughness, z ao, w emissive intensity
    float4 gEmissive;   // rgb emissive color
};
float D_Ggx(float noH, float a) {
    float a2 = a * a;
    float d = noH * noH * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * d * d);
}
float G_SchlickGgx(float noV, float noL, float a) {
    float k = (a + 1.0f) * (a + 1.0f) / 8.0f;
    float gV = noV / (noV * (1.0f - k) + k);
    float gL = noL / (noL * (1.0f - k) + k);
    return gV * gL;
}
// Irradiance SH (b3, band-scaled coeffs from projectSky): pure basis eval,
// same ordering/constants as ibl.cpp (z-polar Green/Sloane frame). Declared
// before PbrLighting (fxc needs declaration before use); also used by the
// init-time bakes below.
cbuffer Irradiance : register(b3) { float4 gSH[9]; };
TextureCube gEnv : register(t1);
Texture2D gBRDF : register(t2);
SamplerState gSampEnv : register(s1);
SamplerState gSampData : register(s2);
float3 SHIrradiance(float3 n) {
    float3 e = gSH[0].rgb * 0.282095f;
    e += gSH[1].rgb * (0.488603f * n.y);
    e += gSH[2].rgb * (0.488603f * n.z);
    e += gSH[3].rgb * (0.488603f * n.x);
    e += gSH[4].rgb * (1.092548f * n.x * n.y);
    e += gSH[5].rgb * (1.092548f * n.y * n.z);
    e += gSH[6].rgb * (0.315392f * (3.0f * n.z * n.z - 1.0f));
    e += gSH[7].rgb * (1.092548f * n.x * n.z);
    e += gSH[8].rgb * (0.546274f * (n.x * n.x - n.y * n.y));
    return e;
}
float4 PbrLighting(VsOut i, float3 albedo, float alpha) {
    float metallic = saturate(gPbrParams.x);
    float rough = clamp(gPbrParams.y, 0.05f, 1.0f);
    float ao = saturate(gPbrParams.z);
    float3 N = normalize(i.nrmView);
    float3 L = normalize(gLightViewAndAmbient.xyz);
    float3 V = float3(0.0f, 0.0f, 1.0f);
    float noL = saturate(dot(N, L));
    float noV = saturate(dot(N, V));
    float3 H = normalize(L + V);
    float noH = saturate(dot(N, H));
    float voH = saturate(dot(V, H));
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float a = rough * rough;
    float D = D_Ggx(noH, a);
    float G = G_SchlickGgx(noV, noL, a);
    float3 F = F0 + (1.0f - F0) * pow(1.0f - voH, 5.0f);
    float3 spec = D * G * F / max(4.0f * noV * noL, 1e-4f);
    float3 diff = albedo * (1.0f - metallic) / 3.14159265f * noL;
    // IBL (Wave 25b): diffuse irradiance + split-sum specular. World-space
    // via inverse view rotation (the viewport draws with identity world, so
    // object == world). Replaces the old ambient placeholder outright.
    float3x3 vR = float3x3(gViewRot[0].xyz, gViewRot[1].xyz, gViewRot[2].xyz);
    float3 Nw = mul(N, transpose(vR));
    float3 Vw = mul(V, transpose(vR));
    float3 irr = SHIrradiance(Nw);
    float3 diffIBL = irr * albedo * (1.0f - metallic) / 3.14159265f * ao;
    float3 R = reflect(-Vw, Nw);
    float3 pre = gEnv.SampleLevel(gSampEnv, R, rough * 7.0f).rgb;
    float2 envBRDF = gBRDF.Sample(gSampData, float2(noV, rough)).rg;
    float3 F0b = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 specIBL = pre * (F0b * envBRDF.x + envBRDF.y) * ao;
    float3 emis = gEmissive.rgb * gPbrParams.w;
    float3 lit = diffIBL + specIBL + diff + spec + emis;
    return float4(LinearToSrgb(saturate(lit)), alpha);
}
float4 PsPbr(VsOut i) : SV_TARGET {
    float3 albedo = SrgbToLinear(i.col.rgb) * gBaseColor.rgb;
    return PbrLighting(i, albedo, i.col.a);
}
float4 PsTexPbr(VsOut i) : SV_TARGET {
    float4 t = gTex.Sample(gSamp, i.uv);
    float3 albedo = SrgbToLinear(t.rgb) * SrgbToLinear(i.col.rgb) * gBaseColor.rgb;
    return PbrLighting(i, albedo, t.a * i.col.a);
}
// ---- IBL (Wave 25b) ------------------------------------------------
// (Irradiance decls + SHIrradiance live before PbrLighting above.)
// Blit vertex: fullscreen triangle; uv0 = payload ((face,rough) constant or
// (NoV,rough) corners), uv1 = NDC xy for cube-face reconstruction.
struct VsBlitIn { float3 pos : POSITION; float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; };
struct VsBlitOut { float4 pos : SV_POSITION; float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; };
VsBlitOut VsBlit(VsBlitIn i) {
    VsBlitOut o;
    o.pos = float4(i.pos, 1.0f);
    o.uv0 = i.uv0;
    o.uv1 = i.uv1;
    return o;
}
// Cube-face direction (D3D11 +X/-X/+Y/-Y/+Z/-Z layout, NDC y-up). Derived
// from view math (r x u = -d); the mirror-sun pixel test proves it against
// hardware sampling, so a sign slip here cannot hide.
float3 CubeDir(float face, float2 ndc) {
    float x = ndc.x, y = ndc.y;
    if (face < 0.5f) return normalize(float3(1.0f, y, x));    // +X
    if (face < 1.5f) return normalize(float3(-1.0f, y, -x));  // -X
    if (face < 2.5f) return normalize(float3(x, 1.0f, y));    // +Y
    if (face < 3.5f) return normalize(float3(x, -1.0f, -y));  // -Y
    if (face < 4.5f) return normalize(float3(-x, y, 1.0f));   // +Z
    return normalize(float3(x, y, -1.0f));                    // -Z
}
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}
float2 Hammersley(uint i, uint N) {
    return float2((float(i) + 0.5f) / float(N), RadicalInverse_VdC(i));
}
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 6.28318530f * Xi.x;
    float cosTheta = sqrt(max((1.0f - Xi.y) / max(1.0f + (a * a - 1.0f) * Xi.y, 1e-6f), 0.0f));
    float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 Tx = normalize(cross(up, N));
    float3 Ty = cross(N, Tx);
    return normalize(Tx * H.x + Ty * H.y + N * H.z);
}
// Init-time prefilter bake: GGX importance-sampled average of mip 0.
// Normals for the bake live in cube space; the bake texture (t0) is the
// same env cube (reads mip 0 while writing mips >= 1 — disjoint
// subresources, no OM/PS hazard).
TextureCube gEnvBake : register(t0);
float4 PsPrefilter(VsBlitOut i) : SV_TARGET {
    float3 R = CubeDir(i.uv0.x, i.uv1);
    float roughness = clamp(i.uv0.y, 0.02f, 1.0f);
    float3 acc = float3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;
    for (int k = 0; k < 64; ++k) {
        float2 Xi = Hammersley(uint(k), 64u);
        float3 H = ImportanceSampleGGX(Xi, R, roughness);
        float3 L = normalize(2.0f * dot(R, H) * H - R);
        float NoL = saturate(dot(R, L));
        if (NoL > 0.0f) {
            acc += gEnvBake.SampleLevel(gSamp, L, 0).rgb * NoL;
            wsum += NoL;
        }
    }
    return float4(wsum > 1e-4f ? acc / wsum : float3(0.0f, 0.0f, 0.0f), 1.0f);
}
float GeometrySmithJoint(float NoV, float NoL, float a) {
    // Height-correlated Smith GGX (visibility form); shared by the LUT and
    // the CPU reference. The punctual path intentionally keeps separable
    // Schlick-GGX (frozen 25a pixel values).
    float a2 = a * a;
    float ggxV = NoV * sqrt(max((-NoV * a2 + NoV) * NoV + a2, 1e-6f));
    float ggxL = NoL * sqrt(max((-NoL * a2 + NoL) * NoL + a2, 1e-6f));
    return 0.5f / max(ggxV + ggxL, 1e-4f);
}
float4 PsBrdf(VsBlitOut i) : SV_TARGET {
    float NoV = saturate(i.uv0.x);
    float roughness = saturate(i.uv0.y);
    float3 V = float3(sqrt(max(1.0f - NoV * NoV, 0.0f)), 0.0f, NoV);
    float3 N = float3(0.0f, 0.0f, 1.0f);
    float A = 0.0f, B = 0.0f;
    for (int k = 0; k < 64; ++k) {
        float2 Xi = Hammersley(uint(k), 64u);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        float NoL = saturate(L.z);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(V, H));
        if (NoL > 0.0f && NoH > 0.0f && NoV > 0.0f) {
            float Gv = GeometrySmithJoint(NoV, NoL, roughness * roughness);
            float G_Vis = Gv * VoH / max(NoH * NoV, 1e-4f);
            float Fc = pow(1.0f - VoH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float4(A / 64.0f, B / 64.0f, 0.0f, 1.0f);
}
)";

bool compileShader(const char* src, const char* entry, const char* target, ComPtr<ID3DBlob>& out,
                   std::string& err) {
    ComPtr<ID3DBlob> errors;
    const HRESULT hr =
        D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target,
                   D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
    if (FAILED(hr)) {
        err = std::string("D3DCompile(") + entry + ") failed";
        if (errors) err += std::string(": ") + static_cast<const char*>(errors->GetBufferPointer());
        return false;
    }
    return true;
}

}  // namespace

struct Renderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> psLit;
    ComPtr<ID3D11PixelShader> psFlat;
    ComPtr<ID3D11PixelShader> psTexSrgb;  // albedo: in-shader sRGB decode
    ComPtr<ID3D11PixelShader> psTexLin;   // data maps: texel already linear
    ComPtr<ID3D11PixelShader> psPbr;      // PBR punctual (factors from b2)
    ComPtr<ID3D11PixelShader> psTexPbr;   // PBR punctual, textured
    ComPtr<ID3D11SamplerState> sampler;
    struct TextureEntry {
        ComPtr<ID3D11ShaderResourceView> view;
        bool srgb = true;  // albedo (in-shader decode) vs data map (raw)
    };
    std::unordered_map<std::string, TextureEntry> textures;
    std::string activeTexture;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> frameCb;
    // GPU skinning (Wave 24): skinned vertex shader + two-slot layout +
    // 256-matrix palette CB (b1). Initialized to identity so draws before
    // the first setSkinningPalette read neutral bind, never garbage.
    ComPtr<ID3D11VertexShader> vsSkin;
    ComPtr<ID3D11InputLayout> layoutSkinned;
    ComPtr<ID3D11Buffer> skinCb;
    // PBR factor block (Wave 25a, b2, PS-only): baseColor + params +
    // emissive. Defaults = neutral dielectric (white, metal 0, rough 0.5).
    ComPtr<ID3D11Buffer> pbrCb;
    // IBL (Wave 25b): procedural-sky env cube (128, full mip chain, FP16) +
    // BRDF LUT (128, R16G16F) + 9-coeff irradiance CB (b3, PS-only) +
    // samplers. All generated once at init; no runtime assets.
    ComPtr<ID3D11Texture2D> envTex;
    ComPtr<ID3D11ShaderResourceView> envSrv;
    ComPtr<ID3D11Texture2D> brdfTex;
    ComPtr<ID3D11ShaderResourceView> brdfSrv;
    ComPtr<ID3D11SamplerState> envSampler;
    ComPtr<ID3D11SamplerState> brdfSampler;
    ComPtr<ID3D11Buffer> irrCb;
    // Blit pipeline for init-time bakes (fullscreen triangle, dynamic VB).
    ComPtr<ID3D11VertexShader> vsBlit;
    ComPtr<ID3D11InputLayout> blitLayout;
    ComPtr<ID3D11Buffer> blitVb;
    ComPtr<ID3D11PixelShader> psPrefilter;
    ComPtr<ID3D11PixelShader> psBrdf;
    // Cached second half of the Frame cbuffer (gViewRot + gLightViewAndAmbient,
    // 16 floats = dst[16..31]). D3D11 MAP_WRITE_DISCARD discards the whole
    // buffer, so every Map must rewrite all 32 floats (WVP + this cache),
    // otherwise draw* calls would lose lighting and setSceneView would lose WVP.
    // Layout (float4 registers c4-c7):
    // c4: gViewRot[0] (xyz=row0, w=0)
    // c5: gViewRot[1] (xyz=row1, w=0)
    // c6: gViewRot[2] (xyz=row2, w=0)
    // c7: gLightViewAndAmbient (xyz=lightDir, w=ambientIntensity)
    float cachedSecondHalf[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 0.0f,
                                  0.0f, 0.0f, 1.0f, 0.0f,
                                  0.4f,  0.8f, 0.45f, 0.35f};
    ComPtr<ID3D11Buffer> lineVb;
    ComPtr<ID3D11RasterizerState> rsSolid;
    ComPtr<ID3D11RasterizerState> rsWire;
    ComPtr<ID3D11RasterizerState> rsWireBias;
    ComPtr<ID3D11DepthStencilState> dsState;
    ComPtr<ID3D11DepthStencilState> dsNoDepth;
    // Offscreen viewport target (Wave 21): scene renders here, composited
    // via ImGui::Image(). Recreated only on size change.
    ComPtr<ID3D11Texture2D> vpTex;
    ComPtr<ID3D11RenderTargetView> vpRtv;
    ComPtr<ID3D11ShaderResourceView> vpSrv;
    ComPtr<ID3D11Texture2D> vpDepth;
    ComPtr<ID3D11DepthStencilView> vpDsv;
    UINT vpWidth = 0;
    UINT vpHeight = 0;
    int frameDrawCalls = 0;
    bool frameTexturedFallback = false;
    UINT targetWidth = 0;
    UINT targetHeight = 0;
    std::size_t lineVbCap = 0;

    struct MeshBuffers {
        ComPtr<ID3D11Buffer> vb;
        ComPtr<ID3D11Buffer> ib;
        UINT indexCount = 0;
        UINT vertexCount = 0;
    };
    std::unordered_map<std::string, MeshBuffers> meshes;
    struct SkinBuffers {
        ComPtr<ID3D11Buffer> vb;
        UINT vertexCount = 0;
    };
    std::unordered_map<std::string, SkinBuffers> skins;

    // Shared skinned-draw prologue as Impl methods (they must name Impl's
    // private buffers): mesh + skin lookup with vertex-count pairing (never
    // OOB reads), WVP upload, skinned layout/VS bind. Returns false when the
    // caller must skip the draw. endSkinnedDraw restores the static
    // layout/VS/PS exactly as the passes leave them.
    bool beginSkinnedDraw(const std::string& key, const Mat4& worldViewProj);
    void endSkinnedDraw();
    // IBL (Wave 25b): builds env cube + prefilter chain + BRDF LUT +
    // irradiance CB once at init. Returns false with outError on failure.
    bool buildIbl(std::string& outError);
    // Fullscreen-triangle blit into an RTV (init-time bakes only).
    void drawBlit(ID3D11PixelShader* ps, ID3D11RenderTargetView* target, UINT w, UINT h,
                  const float uv0[3][2]);
    // Binds env/BRDF views + samplers for PBR draws (t1/t2, s1/s2).
    void bindIblForPbr();

    bool createTarget(int w, int h) {
        rtv.Reset();
        dsv.Reset();
        if (FAILED(swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h),
                                            DXGI_FORMAT_UNKNOWN, 0)))
            return false;
        targetWidth = static_cast<UINT>(w);
        targetHeight = static_cast<UINT>(h);
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
        if (FAILED(device->CreateRenderTargetView(back.Get(), nullptr, &rtv))) return false;
        D3D11_TEXTURE2D_DESC dd{};
        back->GetDesc(&dd);
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        ComPtr<ID3D11Texture2D> depth;
        if (FAILED(device->CreateTexture2D(&dd, nullptr, &depth))) return false;
        if (FAILED(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv))) return false;
        return true;
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(HWND hwnd, int width, int height, std::string& outError) {
    if (initialized_) return true;
    Impl& I = *impl_;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = static_cast<UINT>(width);
    sd.BufferDesc.Height = static_cast<UINT>(height);
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    static const D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, kLevels, 2, D3D11_SDK_VERSION, &sd,
        &I.swapChain, &I.device, &obtained, &I.context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, kLevels,
                                            2, D3D11_SDK_VERSION, &sd, &I.swapChain, &I.device,
                                            &obtained, &I.context);
    }
    if (FAILED(hr)) {
        outError = "D3D11 device creation failed (HRESULT " + std::to_string(hr) + ").";
        return false;
    }
    if (!I.createTarget(width, height)) {
        outError = "Failed to create render target views.";
        return false;
    }

    ComPtr<ID3DBlob> vsBlob;
    if (!compileShader(kShaderSrc, "VsMain", "vs_5_0", vsBlob, outError)) return false;
    if (FAILED(I.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                            nullptr, &I.vs))) {
        outError = "CreateVertexShader failed.";
        return false;
    }
    ComPtr<ID3DBlob> psBlob;
    if (!compileShader(kShaderSrc, "PsMain", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                           nullptr, &I.psLit))) {
        outError = "CreatePixelShader(lit) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsFlat", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psFlat))) {
        outError = "CreatePixelShader(flat) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsTexSrgb", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psTexSrgb))) {
        outError = "CreatePixelShader(textured-srgb) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsTexLinear", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psTexLin))) {
        outError = "CreatePixelShader(textured-linear) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsPbr", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psPbr))) {
        outError = "CreatePixelShader(pbr) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsTexPbr", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psTexPbr))) {
        outError = "CreatePixelShader(pbr-textured) failed.";
        return false;
    }
    // Blit pipeline for init-time IBL bakes (Wave 25b).
    ComPtr<ID3DBlob> blitBlob;
    if (!compileShader(kShaderSrc, "VsBlit", "vs_5_0", blitBlob, outError)) return false;
    if (FAILED(I.device->CreateVertexShader(blitBlob->GetBufferPointer(),
                                             blitBlob->GetBufferSize(), nullptr, &I.vsBlit))) {
        outError = "CreateVertexShader(blit) failed.";
        return false;
    }
    const D3D11_INPUT_ELEMENT_DESC blitElems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.device->CreateInputLayout(blitElems, 3, blitBlob->GetBufferPointer(),
                                            blitBlob->GetBufferSize(), &I.blitLayout))) {
        outError = "CreateInputLayout(blit) failed.";
        return false;
    }
    D3D11_BUFFER_DESC blitBd{};
    blitBd.ByteWidth = 3u * 7u * static_cast<UINT>(sizeof(float));  // 3 verts x (pos3+uv0+uv1)
    blitBd.Usage = D3D11_USAGE_DYNAMIC;
    blitBd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    blitBd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(I.device->CreateBuffer(&blitBd, nullptr, &I.blitVb))) {
        outError = "Failed to create blit vertex buffer.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsPrefilter", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psPrefilter))) {
        outError = "CreatePixelShader(prefilter) failed.";
        return false;
    }
    if (!compileShader(kShaderSrc, "PsBrdf", "ps_5_0", psBlob, outError)) return false;
    if (FAILED(I.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                            nullptr, &I.psBrdf))) {
        outError = "CreatePixelShader(brdf) failed.";
        return false;
    }
    D3D11_SAMPLER_DESC samp{};
    // 4x anisotropic for authored-mip minification (Wave 27 Slice C). 8-16x
    // needs a sampler cache (per-material max-aniso), which is Slice C2.
    samp.Filter = D3D11_FILTER_ANISOTROPIC;
    samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samp.MaxAnisotropy = 4;
    samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp.MinLOD = 0.0f;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(I.device->CreateSamplerState(&samp, &I.sampler))) {
        outError = "CreateSamplerState failed.";
        return false;
    }

    // Static layout (Wave 27 Slice C): TANGENT@24 + BITANGENT@36 are wired to
    // the VS passthrough for the normal-map shader. Stride stays 72 B; pixels
    // stay byte-identical because no PS consumes the new interpolators yet.
    const D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.device->CreateInputLayout(elems, 6, vsBlob->GetBufferPointer(),
                                            vsBlob->GetBufferSize(), &I.layout))) {
        outError = "CreateInputLayout failed.";
        return false;
    }
    // Skinned variant (Wave 24): slot 0 = GpuVertex (unchanged), slot 1 =
    // SkinVertex bone ids/weights. Strides stay sizeof-structs (72/32).
    ComPtr<ID3DBlob> vsSkinBlob;
    if (!compileShader(kShaderSrc, "VsSkinned", "vs_5_0", vsSkinBlob, outError)) return false;
    if (FAILED(I.device->CreateVertexShader(vsSkinBlob->GetBufferPointer(),
                                             vsSkinBlob->GetBufferSize(), nullptr, &I.vsSkin))) {
        outError = "CreateVertexShader(skinned) failed.";
        return false;
    }
    // Skinned layout (Wave 27 Slice C): same TANGENT@24 + BITANGENT@36 wiring
    // on slot 0; slot 1 (skin stream) is untouched. 8 elements total.
    const D3D11_INPUT_ELEMENT_DESC skinElems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA,
         0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };
    if (FAILED(I.device->CreateInputLayout(skinElems, 8, vsSkinBlob->GetBufferPointer(),
                                            vsSkinBlob->GetBufferSize(), &I.layoutSkinned))) {
        outError = "CreateInputLayout(skinned) failed.";
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    // Frame cbuffer is 32 floats (128 B): gWvp (16) + gViewRot packed as
    // 3x float4 (12) + gLightView (3) + gPad (1). Must match kShaderSrc.
    static_assert(sizeof(float) == 4, "float must be 32-bit for Frame CB");
    cb.ByteWidth = sizeof(float) * 32;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(I.device->CreateBuffer(&cb, nullptr, &I.frameCb))) {
        outError = "Failed to create frame constant buffer.";
        return false;
    }
    // PBR factor block (b2, PS-only): 3 float4, dynamic, neutral defaults.
    D3D11_BUFFER_DESC pcb{};
    pcb.ByteWidth = 3u * 4u * static_cast<UINT>(sizeof(float));
    pcb.Usage = D3D11_USAGE_DYNAMIC;
    pcb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pcb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(I.device->CreateBuffer(&pcb, nullptr, &I.pbrCb))) {
        outError = "Failed to create PBR factor buffer.";
        return false;
    }
    {
        D3D11_MAPPED_SUBRESOURCE pmap{};
        if (SUCCEEDED(I.context->Map(I.pbrCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &pmap))) {
            float* pdst = static_cast<float*>(pmap.pData);
            pdst[0] = pdst[1] = pdst[2] = pdst[3] = 1.0f;   // baseColor white
            pdst[4] = 0.0f; pdst[5] = 0.5f; pdst[6] = 1.0f; pdst[7] = 0.0f;  // dielectric
            pdst[8] = pdst[9] = pdst[10] = pdst[11] = 0.0f;  // no emissive
            I.context->Unmap(I.pbrCb.Get(), 0);
        }
    }
    // Skinning palette CB (b1): 256 float4x4, dynamic, identity-filled so
    // draws before the first setSkinningPalette read neutral bind.
    D3D11_BUFFER_DESC scb{};
    scb.ByteWidth = static_cast<UINT>(256u * 16u * sizeof(float));
    scb.Usage = D3D11_USAGE_DYNAMIC;
    scb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    scb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(I.device->CreateBuffer(&scb, nullptr, &I.skinCb))) {
        outError = "Failed to create skinning palette buffer.";
        return false;
    }
    // Identity fill at creation: per-matrix diagonal (c*4+c), NOT a flat
    // i%5 pattern (16 is not a multiple of 5 — that would skew blocks).
    {
        D3D11_MAPPED_SUBRESOURCE smap{};
        if (SUCCEEDED(I.context->Map(I.skinCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &smap))) {
            float* sdst = static_cast<float*>(smap.pData);
            for (int b = 0; b < 256; ++b)
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r) sdst[b * 16 + c * 4 + r] = (c == r) ? 1.0f : 0.0f;
            I.context->Unmap(I.skinCb.Get(), 0);
        }
    }

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    // CULL_NONE: sample/bridge meshes have mixed winding (SMD/FBX/GR2 do not
    // guarantee a single front-face order); BACK culling made such models
    // invisible from the outside. Viewer must show the model either way.
    rs.CullMode = D3D11_CULL_NONE;
    rs.ScissorEnable = TRUE;
    rs.DepthClipEnable = TRUE;
    if (FAILED(I.device->CreateRasterizerState(&rs, &I.rsSolid))) {
        outError = "Failed to create solid rasterizer state.";
        return false;
    }
    rs.FillMode = D3D11_FILL_WIREFRAME;
    rs.CullMode = D3D11_CULL_NONE;
    rs.ScissorEnable = TRUE;
    if (FAILED(I.device->CreateRasterizerState(&rs, &I.rsWire))) {
        outError = "Failed to create wireframe rasterizer state.";
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(I.device->CreateDepthStencilState(&ds, &I.dsState))) {
        outError = "Failed to create depth stencil state.";
        return false;
    }
    D3D11_DEPTH_STENCIL_DESC dsNo{};
    dsNo.DepthEnable = FALSE;
    dsNo.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsNo.DepthFunc = D3D11_COMPARISON_ALWAYS;
    if (FAILED(I.device->CreateDepthStencilState(&dsNo, &I.dsNoDepth))) {
        outError = "Failed to create no-depth stencil state.";
        return false;
    }
    D3D11_RASTERIZER_DESC rsBias{};
    rsBias.FillMode = D3D11_FILL_WIREFRAME;
    rsBias.CullMode = D3D11_CULL_NONE;
    rsBias.ScissorEnable = TRUE;
    rsBias.DepthClipEnable = TRUE;
    rsBias.DepthBias = 20;
    rsBias.SlopeScaledDepthBias = 1.0f;
    if (FAILED(I.device->CreateRasterizerState(&rsBias, &I.rsWireBias))) {
        outError = "Failed to create wire overlay rasterizer state.";
        return false;
    }

    // IBL once at init (env cube + prefilter chain + BRDF LUT + SH CB).
    // Load-bearing for every PBR draw; a failure here fails init honestly
    // instead of shading black later.
    if (!impl_->buildIbl(outError)) return false;
    bindBackbuffer();
    initialized_ = true;
    Logger::instance().info("D3D11 renderer initialized (feature level " +
                            std::to_string((obtained >> 12) & 0xF) + "." +
                            std::to_string((obtained >> 8) & 0xF) + ").");
    return true;
}

void Renderer::shutdown() {
    if (!initialized_) return;
    impl_->meshes.clear();
    impl_->skins.clear();
    impl_->lineVb.Reset();
    impl_->vpSrv.Reset();
    impl_->vpRtv.Reset();
    impl_->vpTex.Reset();
    impl_->vpDsv.Reset();
    impl_->vpDepth.Reset();
    impl_->vpWidth = impl_->vpHeight = 0;
    impl_->frameCb.Reset();
    impl_->skinCb.Reset();
    impl_->layoutSkinned.Reset();
    impl_->vsSkin.Reset();
    impl_->layout.Reset();
    impl_->vs.Reset();
    impl_->psLit.Reset();
    impl_->psFlat.Reset();
    impl_->psTexSrgb.Reset();
    impl_->psTexLin.Reset();
    impl_->psPbr.Reset();
    impl_->psTexPbr.Reset();
    impl_->pbrCb.Reset();
    impl_->envTex.Reset();
    impl_->envSrv.Reset();
    impl_->brdfTex.Reset();
    impl_->brdfSrv.Reset();
    impl_->envSampler.Reset();
    impl_->brdfSampler.Reset();
    impl_->irrCb.Reset();
    impl_->blitVb.Reset();
    impl_->blitLayout.Reset();
    impl_->vsBlit.Reset();
    impl_->psPrefilter.Reset();
    impl_->psBrdf.Reset();
    impl_->sampler.Reset();
    impl_->textures.clear();
    impl_->activeTexture.clear();
    impl_->rsSolid.Reset();
    impl_->rsWire.Reset();
    impl_->rsWireBias.Reset();
    impl_->dsState.Reset();
    impl_->dsNoDepth.Reset();
    impl_->dsv.Reset();
    impl_->rtv.Reset();
    impl_->swapChain.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    initialized_ = false;
}

bool Renderer::resize(int width, int height) {
    if (!initialized_ || width <= 0 || height <= 0) return false;
    impl_->context->OMSetRenderTargets(0, nullptr, nullptr);
    return impl_->createTarget(width, height);
}

bool Renderer::beginScenePass(int x, int y, int w, int h, const float clearColor[4]) {
    Impl& I = *impl_;
    bindBackbuffer();
    if (clearColor) clearBackbuffer(clearColor);
    I.frameDrawCalls = 0;
    I.frameTexturedFallback = false;
    // ImGui reports logical panel coordinates while D3D consumes physical
    // backbuffer pixels. Clamp at this boundary so DPI changes, a partially
    // visible window, or a stale resize event can never produce an invalid
    // viewport or draw scene pixels over docked panels.
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(static_cast<int>(I.targetWidth), x + w);
    const int bottom = std::min(static_cast<int>(I.targetHeight), y + h);
    if (w <= 0 || h <= 0 || right <= left || bottom <= top) return false;
    I.context->OMSetDepthStencilState(I.dsState.Get(), 0);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = static_cast<float>(left);
    vp.TopLeftY = static_cast<float>(top);
    vp.Width = static_cast<float>(right - left);
    vp.Height = static_cast<float>(bottom - top);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    I.context->RSSetViewports(1, &vp);
    D3D11_RECT scissor{left, top, right, bottom};
    I.context->RSSetScissorRects(1, &scissor);
    I.context->ClearDepthStencilView(I.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    I.context->IASetInputLayout(I.layout.Get());
    I.context->VSSetShader(I.vs.Get(), nullptr, 0);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    I.context->VSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
    I.context->VSSetConstantBuffers(1, 1, I.skinCb.GetAddressOf());
    I.context->PSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
    I.context->PSSetConstantBuffers(2, 1, I.pbrCb.GetAddressOf());
    I.context->PSSetConstantBuffers(3, 1, I.irrCb.GetAddressOf());

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int i = 0; i < 32; ++i) dst[i] = 0.0f;
        // gWvp (identity initially)
        dst[0] = dst[5] = dst[10] = dst[15] = 1.0f;
        // gViewRot[3] (c4-c6) - 3 float4 rows of view rotation matrix
        dst[16] = dst[21] = dst[26] = 1.0f; // diagonal
        // gLightViewAndAmbient (c7) - xyz=lightDir, w=ambientIntensity
        dst[28] = 0.4f;
        dst[29] = 0.8f;
        dst[30] = 0.45f;
        dst[31] = 0.35f; // ambient intensity
        // Cache the second half (c4-c7 = 16 floats)
        for (int i = 0; i < 16; ++i) I.cachedSecondHalf[i] = dst[16 + i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    return true;
}

void Renderer::bindBackbuffer() {
    if (!initialized_ || !impl_->rtv || !impl_->dsv) return;
    impl_->context->OMSetRenderTargets(1, impl_->rtv.GetAddressOf(), impl_->dsv.Get());
}

void Renderer::clearBackbuffer(const float clearColor[4]) {
    if (!initialized_ || !impl_->rtv || !impl_->dsv) return;
    impl_->context->ClearRenderTargetView(impl_->rtv.Get(), clearColor);
    impl_->context->ClearDepthStencilView(impl_->dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void Renderer::setSceneView(const Mat4& view) {
    if (!initialized_) return;
    Impl& I = *impl_;
    // Recompute the lighting half into the cache first.
    // gViewRot[3] = rows 0-2 of view rotation (transposed for column-major)
    float second[16] = {0.0f};
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) second[c * 4 + r] = view.m[r][c];
        second[c * 4 + 3] = 0.0f;
    }
    const Vec3 light = normalized(view.transformVector(Vec3{0.4f, 0.8f, 0.45f}));
    second[12] = light.x;
    second[13] = light.y;
    second[14] = light.z;
    second[15] = 0.35f; // ambient intensity
    for (int i = 0; i < 16; ++i) I.cachedSecondHalf[i] = second[i];
    // DISCARD drops the whole buffer: rewrite identity WVP + new lighting half
    // so the buffer stays valid until the next draw* (which rewrites WVP).
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    float* dst = static_cast<float*>(map.pData);
    for (int i = 0; i < 32; ++i) dst[i] = 0.0f;
    dst[0] = dst[5] = dst[10] = dst[15] = 1.0f;
    for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
    I.context->Unmap(I.frameCb.Get(), 0);
    // Pixel shaders sample gLightViewAndAmbient from the same cbuffer: keep VS+PS bound.
    I.context->VSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
    I.context->PSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
}

void Renderer::endScenePass() {}

bool Renderer::ensureViewportTarget(int w, int h, std::string& outError) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        outError = "Bad viewport target size.";
        return false;
    }
    if (I.vpTex && I.vpRtv && I.vpSrv && I.vpDsv && I.vpWidth == static_cast<UINT>(w) &&
        I.vpHeight == static_cast<UINT>(h))
        return true;
    I.vpSrv.Reset();
    I.vpRtv.Reset();
    I.vpTex.Reset();
    I.vpDsv.Reset();
    I.vpDepth.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(w);
    td.Height = static_cast<UINT>(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(I.device->CreateTexture2D(&td, nullptr, &I.vpTex))) {
        outError = "Failed to create viewport texture.";
        return false;
    }
    if (FAILED(I.device->CreateRenderTargetView(I.vpTex.Get(), nullptr, &I.vpRtv))) {
        outError = "Failed to create viewport RTV.";
        I.vpTex.Reset();
        return false;
    }
    if (FAILED(I.device->CreateShaderResourceView(I.vpTex.Get(), nullptr, &I.vpSrv))) {
        outError = "Failed to create viewport SRV.";
        I.vpTex.Reset();
        I.vpRtv.Reset();
        return false;
    }
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = static_cast<UINT>(w);
    dd.Height = static_cast<UINT>(h);
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(I.device->CreateTexture2D(&dd, nullptr, &I.vpDepth))) {
        outError = "Failed to create viewport depth texture.";
        return false;
    }
    if (FAILED(I.device->CreateDepthStencilView(I.vpDepth.Get(), nullptr, &I.vpDsv))) {
        outError = "Failed to create viewport DSV.";
        return false;
    }
    I.vpWidth = static_cast<UINT>(w);
    I.vpHeight = static_cast<UINT>(h);
    return true;
}

void* Renderer::viewportSrv() const {
    if (!initialized_) return nullptr;
    return impl_->vpSrv.Get();
}

bool Renderer::beginViewportPass(const float clearColor[4]) {
    Impl& I = *impl_;
    if (!initialized_ || !I.vpRtv || !I.vpDsv) return false;
    static const float kDefault[4] = {0.09f, 0.10f, 0.13f, 1.0f};
    const float* cc = clearColor ? clearColor : kDefault;
    I.frameDrawCalls = 0;
    I.frameTexturedFallback = false;
    I.context->OMSetRenderTargets(1, I.vpRtv.GetAddressOf(), I.vpDsv.Get());
    I.context->ClearRenderTargetView(I.vpRtv.Get(), cc);
    I.context->ClearDepthStencilView(I.vpDsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    I.context->OMSetDepthStencilState(I.dsState.Get(), 0);
    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(I.vpWidth);
    vp.Height = static_cast<float>(I.vpHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    I.context->RSSetViewports(1, &vp);
    D3D11_RECT scissor{0, 0, static_cast<LONG>(I.vpWidth), static_cast<LONG>(I.vpHeight)};
    I.context->RSSetScissorRects(1, &scissor);
    I.context->IASetInputLayout(I.layout.Get());
    I.context->VSSetShader(I.vs.Get(), nullptr, 0);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    I.context->VSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
    I.context->VSSetConstantBuffers(1, 1, I.skinCb.GetAddressOf());
    I.context->PSSetConstantBuffers(0, 1, I.frameCb.GetAddressOf());
    I.context->PSSetConstantBuffers(2, 1, I.pbrCb.GetAddressOf());
    I.context->PSSetConstantBuffers(3, 1, I.irrCb.GetAddressOf());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int i = 0; i < 32; ++i) dst[i] = 0.0f;
        // gWvp (identity initially)
        dst[0] = dst[5] = dst[10] = dst[15] = 1.0f;
        // gViewRot[3] (c4-c6) - 3 float4 rows of view rotation matrix
        dst[16] = dst[21] = dst[26] = 1.0f; // diagonal
        // gLightViewAndAmbient (c7) - xyz=lightDir, w=ambientIntensity
        dst[28] = 0.4f;
        dst[29] = 0.8f;
        dst[30] = 0.45f;
        dst[31] = 0.35f; // ambient intensity
        // Cache the second half (c4-c7 = 16 floats)
        for (int i = 0; i < 16; ++i) I.cachedSecondHalf[i] = dst[16 + i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    return true;
}

void Renderer::endViewportPass() {
    if (!initialized_) return;
    // Rebind the swap-chain backbuffer so the subsequent ImGui pass composites
    // to the screen; the viewport SRV stays unbound from the pixel stage so it
    // can be sampled by ImGui::Image() without an OM/PS hazard.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    impl_->context->PSSetShaderResources(0, 1, &nullSrv);
    bindBackbuffer();
}

bool Renderer::readViewport(std::vector<std::uint8_t>& outRgba, int& outW, int& outH) {
    Impl& I = *impl_;
    outW = outH = 0;
    outRgba.clear();
    if (!initialized_ || !I.vpTex) return false;
    D3D11_TEXTURE2D_DESC dd{};
    I.vpTex->GetDesc(&dd);
    if (dd.Width == 0 || dd.Height == 0) return false;
    D3D11_TEXTURE2D_DESC sd = dd;
    sd.BindFlags = 0;
    sd.MiscFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.Usage = D3D11_USAGE_STAGING;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(I.device->CreateTexture2D(&sd, nullptr, &staging))) return false;
    I.context->CopyResource(staging.Get(), I.vpTex.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    outW = static_cast<int>(dd.Width);
    outH = static_cast<int>(dd.Height);
    outRgba.resize(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u);
    const std::uint8_t* src = static_cast<const std::uint8_t*>(map.pData);
    for (int y = 0; y < outH; ++y)
        memcpy(outRgba.data() + static_cast<std::size_t>(y) * outW * 4u,
               src + static_cast<std::size_t>(y) * map.RowPitch,
               static_cast<std::size_t>(outW) * 4u);
    I.context->Unmap(staging.Get(), 0);
    return true;
}

Renderer::FrameStats Renderer::frameStats() const {
    FrameStats s;
    if (!initialized_) return s;
    s.drawCalls = impl_->frameDrawCalls;
    s.texturedFallback = impl_->frameTexturedFallback;
    return s;
}

void Renderer::resetFrameStats() {
    if (!initialized_) return;
    impl_->frameDrawCalls = 0;
    impl_->frameTexturedFallback = false;
}

bool Renderer::lastTexturedFallback() const {
    return initialized_ && impl_->frameTexturedFallback;
}

bool Renderer::uploadMesh(const std::string& key, const std::vector<GpuVertex>& vertices,
                          const std::vector<std::uint32_t>& indices, std::string& outError) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    if (vertices.empty() || indices.empty()) {
        outError = "Cannot upload empty mesh '" + key + "'.";
        return false;
    }
    Impl::MeshBuffers mb;
    D3D11_BUFFER_DESC vd{};
    vd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
    vd.Usage = D3D11_USAGE_DEFAULT;
    vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{};
    vsd.pSysMem = vertices.data();
    if (FAILED(I.device->CreateBuffer(&vd, &vsd, &mb.vb))) {
        outError = "Failed to create vertex buffer for '" + key + "'.";
        return false;
    }
    D3D11_BUFFER_DESC id{};
    id.ByteWidth = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    id.Usage = D3D11_USAGE_DEFAULT;
    id.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA isd{};
    isd.pSysMem = indices.data();
    if (FAILED(I.device->CreateBuffer(&id, &isd, &mb.ib))) {
        outError = "Failed to create index buffer for '" + key + "'.";
        return false;
    }
    mb.indexCount = static_cast<UINT>(indices.size());
    mb.vertexCount = static_cast<UINT>(vertices.size());
    I.meshes[key] = std::move(mb);
    // A fresh vertex array invalidates any paired skin stream (count pairing
    // is enforced at draw); the caller re-uploads skin right after.
    I.skins.erase(key);
    return true;
}

void Renderer::releaseMesh(const std::string& key) {
    impl_->meshes.erase(key);
    impl_->skins.erase(key);
}

bool Renderer::hasMesh(const std::string& key) const {
    return initialized_ && impl_->meshes.find(key) != impl_->meshes.end();
}

bool Renderer::uploadSkinning(const std::string& key, const std::vector<SkinVertex>& skin,
                              std::string& outError) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    if (skin.empty()) {
        outError = "Cannot upload empty skin stream '" + key + "'.";
        return false;
    }
    Impl::SkinBuffers sb;
    D3D11_BUFFER_DESC vd{};
    vd.ByteWidth = static_cast<UINT>(skin.size() * sizeof(SkinVertex));
    vd.Usage = D3D11_USAGE_DEFAULT;
    vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd{};
    vsd.pSysMem = skin.data();
    if (FAILED(I.device->CreateBuffer(&vd, &vsd, &sb.vb))) {
        outError = "Failed to create skinning buffer for '" + key + "'.";
        return false;
    }
    sb.vertexCount = static_cast<UINT>(skin.size());
    I.skins[key] = std::move(sb);
    return true;
}

bool Renderer::hasSkinning(const std::string& key) const {
    return initialized_ && impl_->skins.find(key) != impl_->skins.end();
}

void Renderer::releaseSkinning(const std::string& key) { impl_->skins.erase(key); }

void Renderer::setSkinningPalette(const std::vector<Mat4>& palette) {
    if (!initialized_) return;
    Impl& I = *impl_;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(I.skinCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    float* dst = static_cast<float*>(map.pData);
    // HLSL default is column-major: transpose row-major matrices. Entries
    // past the palette read as identity (neutral bind), never garbage.
    const std::size_t n = palette.size() < kSkinPaletteBones ? palette.size() : kSkinPaletteBones;
    for (std::size_t b = 0; b < kSkinPaletteBones; ++b) {
        float* m = dst + b * 16u;
        if (b < n) {
            const Mat4& p = palette[b];
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) m[c * 4 + r] = p.m[r][c];
        } else {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) m[c * 4 + r] = (c == r) ? 1.0f : 0.0f;
        }
    }
    I.context->Unmap(I.skinCb.Get(), 0);
}

namespace {

// CPU mirror of the HLSL CubeDir table (face order +X/-X/+Y/-Y/+Z/-Z, NDC
// y-up, y-down texture rows). Must match bit-for-bit in spirit; the
// mirror-sun pixel test proves the pair against hardware sampling.
Vec3 cubeDirCpu(UINT face, float nx, float ny) {
    switch (face) {
        case 0: return {1.0f, ny, nx};
        case 1: return {-1.0f, ny, -nx};
        case 2: return {nx, 1.0f, ny};
        case 3: return {nx, -1.0f, -ny};
        case 4: return {-nx, ny, 1.0f};
        default: return {nx, ny, -1.0f};
    }
}

}  // namespace

void Renderer::Impl::drawBlit(ID3D11PixelShader* ps, ID3D11RenderTargetView* target, UINT w,
                               UINT h, const float uv0[3][2]) {
    Impl& I = *this;
    static constexpr float kPos[3][3] = {{-1.0f, -1.0f, 0.0f},
                                         {3.0f, -1.0f, 0.0f},
                                         {-1.0f, 3.0f, 0.0f}};
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(I.blitVb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    float* v = static_cast<float*>(map.pData);
    for (int k = 0; k < 3; ++k) {
        v[k * 7 + 0] = kPos[k][0];
        v[k * 7 + 1] = kPos[k][1];
        v[k * 7 + 2] = kPos[k][2];
        v[k * 7 + 3] = uv0[k][0];
        v[k * 7 + 4] = uv0[k][1];
        v[k * 7 + 5] = kPos[k][0];
        v[k * 7 + 6] = kPos[k][1];
    }
    I.context->Unmap(I.blitVb.Get(), 0);
    I.context->OMSetRenderTargets(1, &target, nullptr);
    I.context->OMSetDepthStencilState(I.dsState.Get(), 0);
    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(w);
    vp.Height = static_cast<float>(h);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    I.context->RSSetViewports(1, &vp);
    const D3D11_RECT sc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
    I.context->RSSetScissorRects(1, &sc);
    I.context->IASetInputLayout(I.blitLayout.Get());
    I.context->VSSetShader(I.vsBlit.Get(), nullptr, 0);
    I.context->PSSetShader(ps, nullptr, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT stride = 7u * static_cast<UINT>(sizeof(float)), offset = 0;
    I.context->IASetVertexBuffers(0, 1, I.blitVb.GetAddressOf(), &stride, &offset);
    I.context->RSSetState(I.rsSolid.Get());
    I.context->Draw(3, 0);
}

void Renderer::Impl::bindIblForPbr() {
    Impl& I = *this;
    ID3D11ShaderResourceView* views[2] = {I.envSrv.Get(), I.brdfSrv.Get()};
    I.context->PSSetShaderResources(1, 2, views);
    ID3D11SamplerState* samps[2] = {I.envSampler.Get(), I.brdfSampler.Get()};
    I.context->PSSetSamplers(1, 2, samps);
}

bool Renderer::Impl::buildIbl(std::string& outError) {
    Impl& I = *this;
    const IblSkyParams sky = defaultSky();
    static constexpr UINT kEnv = 128;
    UINT envMips = 1;
    for (UINT s = kEnv; s > 1; s >>= 1) ++envMips;  // 8 for 128
    // TWO textures, not one: D3D11 force-unbinds an SRV whose RESOURCE is
    // bound as RTV — even for disjoint subresources — so sampling a cube
    // while rendering its mips reads black (proven by a zero prefilter
    // chain). srcTex (SR-only, CPU-filled base) is sampled; dstTex (RT+SR,
    // full chain) receives the prefilter mips, then gets the base copied
    // over. The scene samples dstTex via envSrv.
    ComPtr<ID3D11Texture2D> srcTex;
    ComPtr<ID3D11ShaderResourceView> srcSrv;
    D3D11_TEXTURE2D_DESC sd0{};
    sd0.Width = sd0.Height = kEnv;
    sd0.MipLevels = 1;
    sd0.ArraySize = 6;
    sd0.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sd0.SampleDesc.Count = 1;
    sd0.Usage = D3D11_USAGE_DEFAULT;
    sd0.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sd0.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (FAILED(I.device->CreateTexture2D(&sd0, nullptr, &srcTex))) {
        outError = "Failed to create IBL environment source.";
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC ssrc{};
    ssrc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ssrc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    ssrc.TextureCube.MostDetailedMip = 0;
    ssrc.TextureCube.MipLevels = 1;
    if (FAILED(I.device->CreateShaderResourceView(srcTex.Get(), &ssrc, &srcSrv))) {
        outError = "Failed to create IBL environment source view.";
        return false;
    }
    D3D11_TEXTURE2D_DESC ed{};
    ed.Width = ed.Height = kEnv;
    ed.MipLevels = 0;
    ed.ArraySize = 6;
    ed.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    ed.SampleDesc.Count = 1;
    ed.Usage = D3D11_USAGE_DEFAULT;
    ed.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ed.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (FAILED(I.device->CreateTexture2D(&ed, nullptr, &I.envTex))) {
        outError = "Failed to create IBL environment cube.";
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC esrv{};
    esrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    esrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    esrv.TextureCube.MostDetailedMip = 0;
    esrv.TextureCube.MipLevels = static_cast<UINT>(-1);
    if (FAILED(I.device->CreateShaderResourceView(I.envTex.Get(), &esrv, &I.envSrv))) {
        outError = "Failed to create IBL environment view.";
        return false;
    }
    // Base faces straight from CPU evalSky (exact by construction — no sky
    // shader, no constant duplication between C++ and HLSL).
    {
        std::vector<std::uint16_t> face(kEnv * kEnv * 4u);
        for (UINT f = 0; f < 6; ++f) {
            for (UINT y = 0; y < kEnv; ++y) {
                for (UINT x = 0; x < kEnv; ++x) {
                    const float nx = (static_cast<float>(x) + 0.5f) / kEnv * 2.0f - 1.0f;
                    const float ny = 1.0f - (static_cast<float>(y) + 0.5f) / kEnv * 2.0f;
                    const Vec3 rad = evalSky(sky, cubeDirCpu(f, nx, ny));
                    const std::size_t o = (static_cast<std::size_t>(y) * kEnv + x) * 4u;
                    face[o + 0] = floatToHalf(rad.x);
                    face[o + 1] = floatToHalf(rad.y);
                    face[o + 2] = floatToHalf(rad.z);
                    face[o + 3] = floatToHalf(1.0f);
                }
            }
            I.context->UpdateSubresource(srcTex.Get(), D3D11CalcSubresource(0, f, 1),
                                         nullptr, face.data(), kEnv * 8u, kEnv * kEnv * 8u);
        }
    }
    // Samplers: wrap for the cube, clamp for the LUT.
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MinLOD = 0.0f;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(I.device->CreateSamplerState(&sd, &I.envSampler))) {
        outError = "CreateSamplerState(env) failed.";
        return false;
    }
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(I.device->CreateSamplerState(&sd, &I.brdfSampler))) {
        outError = "CreateSamplerState(brdf) failed.";
        return false;
    }
    // Prefilter mips read the source cube (never an RTV: stays bound) and
    // write the destination chain.
    ID3D11ShaderResourceView* bake = srcSrv.Get();
    I.context->PSSetShaderResources(0, 1, &bake);
    for (UINT m = 1; m < envMips; ++m) {
        const UINT size = kEnv >> m;
        const float rough = static_cast<float>(m) / static_cast<float>(envMips - 1);
        for (UINT f = 0; f < 6; ++f) {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = m;
            rtvDesc.Texture2DArray.FirstArraySlice = f;
            rtvDesc.Texture2DArray.ArraySize = 1;
            ComPtr<ID3D11RenderTargetView> faceRtv;
            if (FAILED(I.device->CreateRenderTargetView(I.envTex.Get(), &rtvDesc, &faceRtv))) {
                outError = "Failed to create IBL prefilter target.";
                return false;
            }
            const float fFace = static_cast<float>(f);
            const float uv[3][2] = {{fFace, rough}, {fFace, rough}, {fFace, rough}};
            drawBlit(I.psPrefilter.Get(), faceRtv.Get(), size, size, uv);
        }
    }
    bake = nullptr;
    I.context->PSSetShaderResources(0, 1, &bake);
    // Base mip into the destination chain (prefilter wrote mips >= 1 only).
    for (UINT f = 0; f < 6; ++f)
        I.context->CopySubresourceRegion(I.envTex.Get(), D3D11CalcSubresource(0, f, envMips),
                                         0, 0, 0, srcTex.Get(), D3D11CalcSubresource(0, f, 1),
                                         nullptr);
    // srcTex/srcSrv die here (locals); the scene keeps envTex/envSrv.
    // BRDF LUT (kept texture + view; the RTV dies with this scope).
    static constexpr UINT kBrdf = 128;
    D3D11_TEXTURE2D_DESC bd{};
    bd.Width = bd.Height = kBrdf;
    bd.MipLevels = 1;
    bd.ArraySize = 1;
    bd.Format = DXGI_FORMAT_R16G16_FLOAT;
    bd.SampleDesc.Count = 1;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(I.device->CreateTexture2D(&bd, nullptr, &I.brdfTex))) {
        outError = "Failed to create IBL BRDF texture.";
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC bsrv{};
    bsrv.Format = DXGI_FORMAT_R16G16_FLOAT;
    bsrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    bsrv.Texture2D.MostDetailedMip = 0;
    bsrv.Texture2D.MipLevels = 1;
    if (FAILED(I.device->CreateShaderResourceView(I.brdfTex.Get(), &bsrv, &I.brdfSrv))) {
        outError = "Failed to create IBL BRDF view.";
        return false;
    }
    {
        ComPtr<ID3D11RenderTargetView> brdfRtv;
        D3D11_RENDER_TARGET_VIEW_DESC brtv{};
        brtv.Format = DXGI_FORMAT_R16G16_FLOAT;
        brtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        brtv.Texture2D.MipSlice = 0;
        if (FAILED(I.device->CreateRenderTargetView(I.brdfTex.Get(), &brtv, &brdfRtv))) {
            outError = "Failed to create IBL BRDF target.";
            return false;
        }
        // LUT layout (D3D v-down): row 0 (top) stores rough 0, bottom row
        // stores rough 1, so Sample(v=rough) lands correctly. Corners map
        // NDC y=+1 (top) -> uv.y=0 and NDC y=-1 (bottom) -> uv.y=1.
        const float corners[3][2] = {{0.0f, 1.0f}, {2.0f, 1.0f}, {0.0f, -1.0f}};
        drawBlit(I.psBrdf.Get(), brdfRtv.Get(), kBrdf, kBrdf, corners);
    }
    // Irradiance CB from CPU SH (static sky: uploaded once, never per frame).
    D3D11_BUFFER_DESC icb{};
    icb.ByteWidth = 9u * 4u * static_cast<UINT>(sizeof(float));
    icb.Usage = D3D11_USAGE_DYNAMIC;
    icb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    icb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(I.device->CreateBuffer(&icb, nullptr, &I.irrCb))) {
        outError = "Failed to create IBL irradiance buffer.";
        return false;
    }
    {
        const SphericalHarmonics sh = projectSky(sky);
        D3D11_MAPPED_SUBRESOURCE imap{};
        if (FAILED(I.context->Map(I.irrCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &imap))) {
            outError = "Failed to map IBL irradiance buffer.";
            return false;
        }
        float* idst = static_cast<float*>(imap.pData);
        for (int b = 0; b < 9; ++b) {
            idst[b * 4 + 0] = sh.c[b][0];
            idst[b * 4 + 1] = sh.c[b][1];
            idst[b * 4 + 2] = sh.c[b][2];
            idst[b * 4 + 3] = 0.0f;
        }
        I.context->Unmap(I.irrCb.Get(), 0);
    }
    // Hygiene: backbuffer is rebound by the init tail (a Renderer method —
    // Impl cannot call it); bake slot cleared here.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    return true;
}

bool Renderer::setTexture(const std::string& key, const std::uint8_t* rgba, std::uint32_t width,
                          std::uint32_t height, std::string& outError, bool generateMips,
                          bool srgb) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    if (!rgba || width == 0 || height == 0 || width > 16384 || height > 16384) {
        outError = "Bad texture image for '" + key + "'.";
        return false;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = generateMips ? 0 : 1;  // 0 = full mip chain
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                   (generateMips ? D3D11_BIND_RENDER_TARGET : 0u);
    td.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0u;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba;
    init.SysMemPitch = width * 4;
    ComPtr<ID3D11Texture2D> tex;
    // MISC_GENERATE_MIPS textures take no initial data at creation; the base
    // level is uploaded below before GenerateMips fills the chain.
    if (FAILED(I.device->CreateTexture2D(&td, generateMips ? nullptr : &init, &tex))) {
        outError = "Failed to create texture for '" + key + "'.";
        return false;
    }
    ComPtr<ID3D11ShaderResourceView> srv;
    // Views stay UNORM (inferred desc): the sRGB decode lives in-shader
    // (PsTexSrgb) because explicit UNORM_SRGB views fail creation on some
    // runtimes including the WARP test box. The srgb flag below selects the
    // decode variant per texture instead of per view format.
    if (FAILED(I.device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
        outError = "Failed to create texture view for '" + key + "'.";
        return false;
    }
    if (generateMips) {
        I.context->UpdateSubresource(tex.Get(), 0, nullptr, rgba, width * 4, 0);
        I.context->GenerateMips(srv.Get());
    }
    Impl::TextureEntry entry;
    entry.view = std::move(srv);
    entry.srgb = srgb;
    I.textures[key] = std::move(entry);
    return true;
}

bool Renderer::setTextureMips(const std::string& key, const DdsImage& image, std::string& outError,
                              bool srgb) {
    Impl& I = *impl_;
    if (!initialized_) {
        outError = "Renderer is not initialized.";
        return false;
    }
    // Authored mips are primary (solution-judge verdict); GenerateMips stays
    // the fallback inside setTexture for mipCount<=1 / procedural images.
    if (image.mips.empty()) {
        outError = "Bad authored-mip image for '" + key + "': no mip levels.";
        return false;
    }
    if (image.width == 0 || image.height == 0 || image.width > 16384 || image.height > 16384) {
        outError = "Bad authored-mip image for '" + key + "'.";
        return false;
    }
    const std::size_t levels = image.mips.size();
    if (levels > 15) {
        outError = "Too many mip levels for '" + key + "'.";
        return false;
    }
    // Validate every level against the halving chain (decoder uses floor /2,
    // min 1 per axis, with a clamped 1x1 tail) so a corrupt DdsImage can
    // never mis-upload silently.
    for (std::size_t level = 0; level < levels; ++level) {
        const std::uint32_t shift = static_cast<std::uint32_t>(level);
        const std::uint32_t ew = std::max<std::uint32_t>(1u, image.width >> shift);
        const std::uint32_t eh = std::max<std::uint32_t>(1u, image.height >> shift);
        const DdsMipLevel& mip = image.mips[level];
        if (mip.width != ew || mip.height != eh) {
            outError = "Mip level " + std::to_string(level) + " size mismatch for '" + key +
                       "'.";
            return false;
        }
        if (mip.rgba.size() !=
            static_cast<std::size_t>(ew) * static_cast<std::size_t>(eh) * 4u) {
            outError = "Mip level " + std::to_string(level) + " data size mismatch for '" + key +
                       "'.";
            return false;
        }
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = image.width;
    td.Height = image.height;
    td.MipLevels = static_cast<UINT>(levels);
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    // No RENDER_TARGET bind, no GENERATE_MIPS: the chain is authored.
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = 0u;
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(I.device->CreateTexture2D(&td, nullptr, &tex))) {
        outError = "Failed to create authored-mip texture for '" + key + "'.";
        return false;
    }
    ComPtr<ID3D11ShaderResourceView> srv;
    // Views stay UNORM (inferred desc): same in-shader decode rule as
    // setTexture (PsTexSrgb vs PsTexLinear via the srgb flag).
    if (FAILED(I.device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
        outError = "Failed to create authored-mip texture view for '" + key + "'.";
        return false;
    }
    for (std::size_t level = 0; level < levels; ++level) {
        const DdsMipLevel& mip = image.mips[level];
        I.context->UpdateSubresource(tex.Get(), static_cast<UINT>(level), nullptr,
                                     mip.rgba.data(), mip.width * 4u, 0);
    }
    Impl::TextureEntry entry;
    entry.view = std::move(srv);
    entry.srgb = srgb;
    // Re-upload replaces (idempotent): same key re-uploaded draws identically.
    I.textures[key] = std::move(entry);
    return true;
}

bool Renderer::hasTexture(const std::string& key) const {
    return initialized_ && impl_->textures.find(key) != impl_->textures.end();
}

bool Renderer::textureIsSrgb(const std::string& key) const {
    if (!initialized_) return false;
    const auto it = impl_->textures.find(key);
    return it != impl_->textures.end() && it->second.srgb;
}

void Renderer::setActiveTexture(const std::string& key) { impl_->activeTexture = key; }

void Renderer::releaseTexture(const std::string& key) {
    impl_->textures.erase(key);
    if (impl_->activeTexture == key) impl_->activeTexture.clear();
}

void Renderer::drawMeshTextured(const std::string& key, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto tit = I.textures.find(I.activeTexture);
    if (tit == I.textures.end() || !tit->second.view) {
        I.frameTexturedFallback = true;
        drawMesh(key, worldViewProj, fill);
        return;
    }
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    // Decode variant follows the upload flag: albedo (sRGB) vs data (linear).
    I.context->PSSetShader(tit->second.srgb ? I.psTexSrgb.Get() : I.psTexLin.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = tit->second.view.Get();
    I.context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = I.sampler.Get();
    I.context->PSSetSamplers(0, 1, &samp);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ++I.frameDrawCalls;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMesh(const std::string& key, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        // HLSL default is column-major: transpose our row-major matrix.
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ++I.frameDrawCalls;
}

void Renderer::drawMeshRange(const std::string& key, std::uint32_t startIndex,
                             std::uint32_t indexCount, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    if (indexCount == 0 || startIndex >= it->second.indexCount) return;
    const UINT count =
        (static_cast<std::uint64_t>(startIndex) + indexCount > it->second.indexCount)
            ? (it->second.indexCount - startIndex)
            : static_cast<UINT>(indexCount);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(count, startIndex, 0);
    ++I.frameDrawCalls;
}

void Renderer::drawMeshTexturedRange(const std::string& key, std::uint32_t startIndex,
                                     std::uint32_t indexCount, const Mat4& worldViewProj,
                                     FillMode fill) {
    Impl& I = *impl_;
    auto tit = I.textures.find(I.activeTexture);
    if (tit == I.textures.end() || !tit->second.view) {
        I.frameTexturedFallback = true;
        drawMeshRange(key, startIndex, indexCount, worldViewProj, fill);
        return;
    }
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    if (indexCount == 0 || startIndex >= it->second.indexCount) return;
    const UINT count =
        (static_cast<std::uint64_t>(startIndex) + indexCount > it->second.indexCount)
            ? (it->second.indexCount - startIndex)
            : static_cast<UINT>(indexCount);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(tit->second.srgb ? I.psTexSrgb.Get() : I.psTexLin.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = tit->second.view.Get();
    I.context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = I.sampler.Get();
    I.context->PSSetSamplers(0, 1, &samp);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(count, startIndex, 0);
    ++I.frameDrawCalls;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMeshFlat(const std::string& key, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    // Unlit display passthrough: debug ramps are authored in display space,
    // so unlike the lit paths this deliberately skips the sRGB round-trip.
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawLines(const std::vector<GpuVertex>& segments, const Mat4& worldViewProj) {
    Impl& I = *impl_;
    if (segments.empty()) return;
    if (segments.size() > I.lineVbCap) {
        I.lineVb.Reset();
        D3D11_BUFFER_DESC vd{};
        vd.ByteWidth = static_cast<UINT>(segments.size() * sizeof(GpuVertex));
        vd.Usage = D3D11_USAGE_DYNAMIC;
        vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(I.device->CreateBuffer(&vd, nullptr, &I.lineVb))) return;
        I.lineVbCap = segments.size();
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(I.lineVb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    memcpy(map.pData, segments.data(), segments.size() * sizeof(GpuVertex));
    I.context->Unmap(I.lineVb.Get(), 0);
    D3D11_MAPPED_SUBRESOURCE cmap{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cmap))) {
        float* dst = static_cast<float*>(cmap.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(I.rsWire.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, I.lineVb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    I.context->Draw(static_cast<UINT>(segments.size()), 0);
    ++I.frameDrawCalls;
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMeshWireOverlay(const std::string& key, const Mat4& worldViewProj) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    // Depth-biased wireframe so the overlay never z-fights the solid pass.
    I.context->RSSetState(I.rsWireBias.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

bool Renderer::Impl::beginSkinnedDraw(const std::string& key, const Mat4& worldViewProj) {
    Impl& I = *this;
    const auto mit = I.meshes.find(key);
    const auto sit = I.skins.find(key);
    if (mit == I.meshes.end() || sit == I.skins.end()) return false;
    if (mit->second.vertexCount == 0 || mit->second.vertexCount != sit->second.vertexCount)
        return false;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->IASetInputLayout(I.layoutSkinned.Get());
    I.context->VSSetShader(I.vsSkin.Get(), nullptr, 0);
    const UINT stride0 = sizeof(GpuVertex), stride1 = sizeof(SkinVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, mit->second.vb.GetAddressOf(), &stride0, &offset);
    I.context->IASetVertexBuffers(1, 1, sit->second.vb.GetAddressOf(), &stride1, &offset);
    I.context->IASetIndexBuffer(mit->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return true;
}

void Renderer::Impl::endSkinnedDraw() {
    Impl& I = *this;
    // Unbind the skin slot too: the static layout has no slot-1 elements and
    // would ignore it, but no stale binding outlives a skinned draw.
    ID3D11Buffer* nullVb = nullptr;
    const UINT zero = 0;
    I.context->IASetVertexBuffers(1, 1, &nullVb, &zero, &zero);
    I.context->IASetInputLayout(I.layout.Get());
    I.context->VSSetShader(I.vs.Get(), nullptr, 0);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMeshSkinned(const std::string& key, const Mat4& worldViewProj,
                               FillMode fill) {
    Impl& I = *impl_;
    if (!I.beginSkinnedDraw(key, worldViewProj)) return;
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
    I.context->DrawIndexed(I.meshes[key].indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.endSkinnedDraw();
}

void Renderer::drawMeshTexturedSkinned(const std::string& key, const Mat4& worldViewProj,
                                       FillMode fill) {
    Impl& I = *impl_;
    auto tit = I.textures.find(I.activeTexture);
    if (tit == I.textures.end() || !tit->second.view) {
        I.frameTexturedFallback = true;
        drawMeshSkinned(key, worldViewProj, fill);
        return;
    }
    if (!I.beginSkinnedDraw(key, worldViewProj)) return;
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(tit->second.srgb ? I.psTexSrgb.Get() : I.psTexLin.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = tit->second.view.Get();
    I.context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = I.sampler.Get();
    I.context->PSSetSamplers(0, 1, &samp);
    I.context->DrawIndexed(I.meshes[key].indexCount, 0, 0);
    ++I.frameDrawCalls;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    I.endSkinnedDraw();
}

void Renderer::drawMeshFlatSkinned(const std::string& key, const Mat4& worldViewProj,
                                   FillMode fill) {
    Impl& I = *impl_;
    if (!I.beginSkinnedDraw(key, worldViewProj)) return;
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    I.context->DrawIndexed(I.meshes[key].indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.endSkinnedDraw();
}

void Renderer::drawMeshWireOverlaySkinned(const std::string& key, const Mat4& worldViewProj) {
    Impl& I = *impl_;
    if (!I.beginSkinnedDraw(key, worldViewProj)) return;
    // Depth-biased wireframe so the overlay never z-fights the solid pass.
    I.context->RSSetState(I.rsWireBias.Get());
    I.context->PSSetShader(I.psFlat.Get(), nullptr, 0);
    I.context->DrawIndexed(I.meshes[key].indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.endSkinnedDraw();
}

void Renderer::setPbrMaterial(const PbrMaterial& material) {
    if (!initialized_) return;
    Impl& I = *impl_;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(I.pbrCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    float* dst = static_cast<float*>(map.pData);
    dst[0] = material.baseColor[0];
    dst[1] = material.baseColor[1];
    dst[2] = material.baseColor[2];
    dst[3] = material.baseColor[3];
    dst[4] = material.metallic;
    dst[5] = material.roughness;
    dst[6] = material.ao;
    dst[7] = material.emissive[3];
    dst[8] = material.emissive[0];
    dst[9] = material.emissive[1];
    dst[10] = material.emissive[2];
    dst[11] = 0.0f;
    I.context->Unmap(I.pbrCb.Get(), 0);
}

void Renderer::drawMeshPbr(const std::string& key, const Mat4& worldViewProj, FillMode fill) {
    Impl& I = *impl_;
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psPbr.Get(), nullptr, 0);
    I.bindIblForPbr();
    const UINT stride = sizeof(GpuVertex), offset = 0;
    I.context->IASetVertexBuffers(0, 1, it->second.vb.GetAddressOf(), &stride, &offset);
    I.context->IASetIndexBuffer(it->second.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMeshTexturedPbr(const std::string& key, const Mat4& worldViewProj,
                                   FillMode fill) {
    Impl& I = *impl_;
    // Peek first: textured-PBR without a bound texture falls back to plain
    // PBR (same honesty rule as the Blinn textured path).
    {
        const auto tit = I.textures.find(I.activeTexture);
        if (tit == I.textures.end() || !tit->second.view) {
            I.frameTexturedFallback = true;
            drawMeshPbr(key, worldViewProj, fill);
            return;
        }
    }
    auto it = I.meshes.find(key);
    if (it == I.meshes.end()) return;
    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(I.context->Map(I.frameCb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        float* dst = static_cast<float*>(map.pData);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) dst[c * 4 + r] = worldViewProj.m[r][c];
        for (int i = 0; i < 16; ++i) dst[16 + i] = I.cachedSecondHalf[i];
        I.context->Unmap(I.frameCb.Get(), 0);
    }
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    // Textures bound here are albedo by construction (single diffuse path);
    // data-map slots arrive with the per-submesh material system (Wave 27).
    I.context->PSSetShader(I.psTexPbr.Get(), nullptr, 0);
    I.bindIblForPbr();
    ID3D11ShaderResourceView* srv = I.textures[I.activeTexture].view.Get();
    I.context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = I.sampler.Get();
    I.context->PSSetSamplers(0, 1, &samp);
    I.context->DrawIndexed(it->second.indexCount, 0, 0);
    ++I.frameDrawCalls;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    I.context->PSSetShader(I.psLit.Get(), nullptr, 0);
}

void Renderer::drawMeshSkinnedPbr(const std::string& key, const Mat4& worldViewProj,
                                  FillMode fill) {
    Impl& I = *impl_;
    if (!I.beginSkinnedDraw(key, worldViewProj)) return;
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    I.context->PSSetShader(I.psPbr.Get(), nullptr, 0);
    I.bindIblForPbr();
    I.context->DrawIndexed(I.meshes[key].indexCount, 0, 0);
    ++I.frameDrawCalls;
    I.endSkinnedDraw();
}

void Renderer::drawMeshTexturedSkinnedPbr(const std::string& key, const Mat4& worldViewProj,
                                          FillMode fill) {
    Impl& I = *impl_;
    {
        const auto tit = I.textures.find(I.activeTexture);
        if (tit == I.textures.end() || !tit->second.view) {
            I.frameTexturedFallback = true;
            drawMeshSkinnedPbr(key, worldViewProj, fill);
            return;
        }
    }
    if (!I.beginSkinnedDraw(key, worldViewProj)) return;
    I.context->RSSetState(fill == FillMode::Solid ? I.rsSolid.Get() : I.rsWire.Get());
    // Albedo by construction (see drawMeshTexturedPbr).
    I.context->PSSetShader(I.psTexPbr.Get(), nullptr, 0);
    I.bindIblForPbr();
    ID3D11ShaderResourceView* srv = I.textures[I.activeTexture].view.Get();
    I.context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* samp = I.sampler.Get();
    I.context->PSSetSamplers(0, 1, &samp);
    I.context->DrawIndexed(I.meshes[key].indexCount, 0, 0);
    ++I.frameDrawCalls;
    ID3D11ShaderResourceView* nullSrv = nullptr;
    I.context->PSSetShaderResources(0, 1, &nullSrv);
    I.endSkinnedDraw();
}

void Renderer::drawLinesXRay(const std::vector<GpuVertex>& segments,
                             const Mat4& worldViewProj) {
    Impl& I = *impl_;
    if (segments.empty()) return;
    I.context->OMSetDepthStencilState(I.dsNoDepth.Get(), 0);
    drawLines(segments, worldViewProj);
    I.context->OMSetDepthStencilState(I.dsState.Get(), 0);
}

void Renderer::present(bool vsync) { impl_->swapChain->Present(vsync ? 1 : 0, 0); }

bool Renderer::readBackbuffer(std::vector<std::uint8_t>& outRgba, int& outW, int& outH) {
    Impl& I = *impl_;
    outW = outH = 0;
    outRgba.clear();
    if (!initialized_) return false;
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(I.swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    D3D11_TEXTURE2D_DESC dd{};
    back->GetDesc(&dd);
    if (dd.Width == 0 || dd.Height == 0) return false;
    D3D11_TEXTURE2D_DESC sd = dd;
    sd.BindFlags = 0;
    sd.MiscFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.Usage = D3D11_USAGE_STAGING;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(I.device->CreateTexture2D(&sd, nullptr, &staging))) return false;
    I.context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(I.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    outW = static_cast<int>(dd.Width);
    outH = static_cast<int>(dd.Height);
    outRgba.resize(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u);
    const std::uint8_t* src = static_cast<const std::uint8_t*>(map.pData);
    for (int y = 0; y < outH; ++y)
        memcpy(outRgba.data() + static_cast<std::size_t>(y) * outW * 4u,
               src + static_cast<std::size_t>(y) * map.RowPitch,
               static_cast<std::size_t>(outW) * 4u);
    I.context->Unmap(staging.Get(), 0);
    return true;
}

void* Renderer::deviceForBackend() { return impl_->device.Get(); }
void* Renderer::contextForBackend() { return impl_->context.Get(); }

}  // namespace m2rig
