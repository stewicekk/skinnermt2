#pragma once
// Minimal real DirectX 11 renderer: mesh buffers (pos/normal/tangent/color), solid +
// wireframe/flat/PBR pipelines, line renderer for grid/bones. Lighting is linear-space:
// Blinn-Phong legacy + Cook-Torrance GGX punctual PBR (sRGB albedo decode + sRGB
// output encode); IBL is roadmap (Wave 25b). Skinning preview is GPU LBS palette
// (CPU deform + re-upload kept as headless/test fallback and the CPU-DQS path).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <array>

#include "m2rig/math.hpp"
#include "m2rig/result.hpp"

struct HWND__;
typedef HWND__* HWND;

namespace m2rig {

struct Mesh;
struct Skeleton;
struct SkinVertex;  // defined below (before the GPU-view builders)
struct DdsImage;    // forward decl (dds.hpp); avoids pulling decode into the render header

enum class FillMode { Solid, Wireframe };

// Extended vertex with tangent space for normal mapping
struct GpuVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 tangent;
    Vec3 bitangent;
    float color[4];
    float uv[2];
};
static_assert(sizeof(GpuVertex) == 72, "GpuVertex must stay 72 B (pos12+norm12+tan12+bitan12+col16+uv8)");
static_assert(offsetof(GpuVertex, position) == 0, "GpuVertex.position offset");
static_assert(offsetof(GpuVertex, normal) == 12, "GpuVertex.normal offset");
static_assert(offsetof(GpuVertex, tangent) == 24, "GpuVertex.tangent offset");
static_assert(offsetof(GpuVertex, bitangent) == 36, "GpuVertex.bitangent offset");
static_assert(offsetof(GpuVertex, color) == 48, "GpuVertex.color offset");
static_assert(offsetof(GpuVertex, uv) == 64, "GpuVertex.uv offset");

// PBR Material - supports full metallic/roughness workflow
struct PbrMaterial {
    std::string name;
    // Base color (albedo) - rgb = color, a = opacity
    float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Metallic [0,1] - 0 = dielectric, 1 = metal
    float metallic = 0.0f;
    // Roughness [0,1] - 0 = smooth mirror, 1 = diffuse
    float roughness = 0.5f;
    // Ambient occlusion [0,1]
    float ao = 1.0f;
    // Emissive color (rgb), intensity in alpha
    float emissive[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Subsurface scattering (thickness in alpha)
    float subsurface[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // Clearcoat (for car paint, etc.)
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.0f;
    // Anisotropy
    float anisotropy = 0.0f;
    float anisotropyRotation = 0.0f;
    // Transmission (for glass)
    float transmission = 0.0f;
    float ior = 1.5f;
    
    // Texture slots (empty = use factor value)
    std::string albedoTexture;
    std::string normalTexture;
    std::string metallicRoughnessTexture;  // R=metallic, G=roughness, B=AO
    std::string emissiveTexture;
    std::string aoTexture;
    std::string clearcoatTexture;
    std::string transmissionTexture;
    std::string thicknessTexture;
    
    // Texture transforms
    float uvOffset[2] = {0.0f, 0.0f};
    float uvScale[2] = {1.0f, 1.0f};
    float uvRotation = 0.0f;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(HWND hwnd, int width, int height, std::string& outError);
    void shutdown();
    bool resize(int width, int height);
    bool isInitialized() const { return initialized_; }

    // Scene pass restricted to a sub-rectangle of the backbuffer (the ImGui
    // viewport panel). Coordinates are in physical pixels, origin top-left.
    // The pass also binds and clears the backbuffer so ImGui can safely render
    // even when a dock layout temporarily has no valid scene rectangle.
    // Returns false when the clamped rect is empty (caller must skip draws;
    // the backbuffer stays bound+cleared so ImGui still composites).
    bool beginScenePass(int x, int y, int w, int h, const float clearColor[4]);
    void bindBackbuffer();
    void clearBackbuffer(const float clearColor[4]);
    void setSceneView(const Mat4& view);
    void endScenePass();

    // Offscreen viewport target (Wave 21 fix for the occupied-central-node
    // opaque dock background hiding the backbuffer scene): the 3D scene
    // renders into this texture, composited via ImGui::Image() inside the
    // Viewport panel. Docking/DPI-proof; the backbuffer path above is kept
    // for the headless smoke test + invalid-rect fallback.
    struct FrameStats {
        int drawCalls = 0;
        bool texturedFallback = false;
    };
    bool ensureViewportTarget(int w, int h, std::string& outError);
    void* viewportSrv() const;
    bool beginViewportPass(const float clearColor[4]);
    void endViewportPass();
    bool readViewport(std::vector<std::uint8_t>& outRgba, int& outW, int& outH);
    FrameStats frameStats() const;
    void resetFrameStats();
    bool lastTexturedFallback() const;

    // Uploads (or re-uploads) a mesh. Colors come from the caller so view
    // modes (solid/heatmap/normals/...) stay renderer-agnostic.
    bool uploadMesh(const std::string& key, const std::vector<GpuVertex>& vertices,
                    const std::vector<std::uint32_t>& indices, std::string& outError);
    bool hasMesh(const std::string& key) const;
    void releaseMesh(const std::string& key);

    // PBR Material - metallic/roughness workflow, punctual backend (Wave 25a).
    // setPbrMaterial uploads the factor block consumed by the PBR draws;
    // IBL (irradiance + prefiltered env + BRDF LUT) is the Wave-25b follow-up.
    // clearcoat/anisotropy/transmission slots stay explicitly unsupported.
    void setPbrMaterial(const PbrMaterial& material);

    // Texture management. generateMips builds a full chain via GenerateMips
    // (needs no extra caller work); false uploads a single level.
    // srgb=true marks an albedo texture: the textured draw decodes the texel
    // in-shader (PsTexSrgb). srgb=false keeps the texel raw (PsTexLinear)
    // for data maps (normal/rough/metal/AO — linear by nature). Views stay
    // UNORM (explicit SRGB views fail creation on some runtimes, proven on
    // the WARP test box); mip tails are filtered in gamma space (accepted
    // D3D11 tradeoff until the linear-float pipeline lands).
    bool setTexture(const std::string& key, const std::uint8_t* rgba, std::uint32_t width,
                    std::uint32_t height, std::string& outError, bool generateMips = true,
                    bool srgb = true);
    // Authored-mip upload (Wave 27 Slice C): MipLevels=mips.size() with a
    // per-level UpdateSubresource and NO GenerateMips. The srgb flag selects
    // the same in-shader decode variant as setTexture. Fallback for
    // mipCount<=1 or procedural single-level images stays setTexture.
    bool setTextureMips(const std::string& key, const DdsImage& image, std::string& outError,
                        bool srgb = true);
    // Binds which uploaded texture drawMeshTextured samples; empty clears it
    // (drawMeshTextured then takes the honest untextured fallback path).
    void setActiveTexture(const std::string& key);
    void releaseTexture(const std::string& key);
    bool hasTexture(const std::string& key) const;
    // True when the texture was uploaded as sRGB albedo (decode variant).
    // False for linear data-map uploads and for unknown keys.
    bool textureIsSrgb(const std::string& key) const;
    void drawMeshTextured(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMesh(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    // Unlit display-passthrough draw (PsFlat): for Normals/Height/Weight/UV
    // debug ramps, which are display-referred and must survive the linear
    // lit pipeline exactly (and legend-consistent).
    void drawMeshFlat(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshWireOverlay(const std::string& key, const Mat4& worldViewProj);
    // Submesh-range draws (Wave 27 Slice C, minimal set): same as drawMesh /
    // drawMeshTextured but over [startIndex, startIndex+indexCount) of the
    // index buffer. Out-of-range starts skip; overruns clamp. The remaining
    // draw matrix (flat/overlay/skinned/PBR x range) is Slice C2.
    void drawMeshRange(const std::string& key, std::uint32_t startIndex,
                       std::uint32_t indexCount, const Mat4& worldViewProj, FillMode fill);
    void drawMeshTexturedRange(const std::string& key, std::uint32_t startIndex,
                               std::uint32_t indexCount, const Mat4& worldViewProj,
                               FillMode fill);

    // GPU skinning (Wave 24, LBS). The skin stream is a second VB slot paired
    // with the mesh upload: uploadMesh drops any stale skin for the key, so a
    // mesh is skinned only between a successful uploadSkinning and the next
    // uploadMesh/releaseMesh. Draws additionally require matching vertex
    // counts, otherwise they skip (never OOB reads). Palette entries beyond
    // the uploaded count read as identity (neutral bind).
    bool uploadSkinning(const std::string& key, const std::vector<SkinVertex>& skin,
                        std::string& outError);
    bool hasSkinning(const std::string& key) const;
    void releaseSkinning(const std::string& key);
    void setSkinningPalette(const std::vector<Mat4>& palette);
    void drawMeshSkinned(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshTexturedSkinned(const std::string& key, const Mat4& worldViewProj,
                                FillMode fill);
    void drawMeshFlatSkinned(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshWireOverlaySkinned(const std::string& key, const Mat4& worldViewProj);

    // PBR draws (Wave 25a, Cook-Torrance GGX punctual + ambient placeholder;
    // IBL arrives in 25b). Same pairing rules as the skinned draws; the flat
    // debug path stays display-passthrough and is never PBR-shaded.
    void drawMeshPbr(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshTexturedPbr(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshSkinnedPbr(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshTexturedSkinnedPbr(const std::string& key, const Mat4& worldViewProj,
                                    FillMode fill);

    void present(bool vsync);
    // Test/diagnostics helper: copies the backbuffer to CPU memory (RGBA8).
    // Returns false when uninitialized or the copy fails. Used by the
    // headless render test to prove draws emit pixels (not just no-crash).
    bool readBackbuffer(std::vector<std::uint8_t>& outRgba, int& outW, int& outH);
    void drawLines(const std::vector<GpuVertex>& segments, const Mat4& worldViewProj);
    void drawLinesXRay(const std::vector<GpuVertex>& segments, const Mat4& worldViewProj);

    // Opaque backend handles for the ImGui DX11 backend (valid after init).
    void* deviceForBackend();
    void* contextForBackend();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

// Builds GpuVertex arrays from a canonical mesh for a given debug coloring.
// Computes tangents/bitangents using MikkTSpace algorithm.
enum class MeshColoring { Solid, Normals, Height, Weight, UV };
std::vector<GpuVertex> buildGpuVertices(const Mesh& mesh, MeshColoring coloring);

// GPU skinning stream (Wave 24): bone ids + weights per vertex, uploaded as
// a second VB slot beside the 72 B GpuVertex (which stays untouched).
// bones[i] must be < min(boneCount, paletteBones); buildSkinVertices FAILS
// explicitly on dangling ids, ids beyond the palette, >4 influences, or
// non-finite/negative weights (never silent clamp/drop). Unweighted verts
// encode all-zero weights: the shader falls back to bind like deformVertex
// (wsum <= 1e-9 -> pos) for all accepted inputs.
struct SkinVertex {
    std::uint32_t bones[4];
    float weights[4];
};
static_assert(sizeof(SkinVertex) == 32, "SkinVertex must stay 32 B (4xu32 + 4xfp32)");
constexpr std::size_t kSkinPaletteBones = 256;
Result<std::vector<SkinVertex>> buildSkinVertices(const Mesh& mesh, std::size_t boneCount,
                                                 std::size_t paletteBones = kSkinPaletteBones);
// Weight heatmap for one bone: blue (0) -> red (1).
std::vector<GpuVertex> buildGpuVerticesWeight(const Mesh& mesh, std::uint32_t bone);
// Animation preview: positions/normals deformed by the skinning palette.
std::vector<GpuVertex> buildGpuVerticesDeformed(const Mesh& mesh,
                                                const std::vector<Mat4>& palette,
                                                MeshColoring coloring, std::uint32_t bone);
// DQS Animation preview: positions/normals deformed by dual quaternion palette.
std::vector<GpuVertex> buildGpuVerticesDeformedDqs(const Mesh& mesh,
                                                   const std::vector<DualQuat>& palette,
                                                   MeshColoring coloring, std::uint32_t bone);
// Index filter for submesh isolation (hidden submeshes skipped at upload;
// exports and picking always use the full index buffer).
std::vector<std::uint32_t> filterVisibleIndices(const Mesh& mesh,
                                                const std::set<std::size_t>& hidden);

// Grid + axis lines centered at origin. axisLen < 0 selects halfExtent*0.3
// so big scenes keep readable axes (old fixed 1.5 matches halfExtent 5.0).
std::vector<GpuVertex> buildGridLines(float halfExtent = 5.0f, float step = 0.5f,
                                       float axisLen = -1.0f);

}  // namespace m2rig
