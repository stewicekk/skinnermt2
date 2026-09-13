#pragma once
// Minimal real DirectX 11 renderer: mesh buffers (pos/normal/color), solid +
// wireframe pipelines, line renderer for grid/bones, Lambert shading.
// GPU skinning palette arrives in wave 6; this draws the bind pose, which is
// exactly what the skeleton viewer and solid/wire/normal modes need.
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "m2rig/math.hpp"

struct HWND__;
typedef HWND__* HWND;

namespace m2rig {

struct Mesh;
struct Skeleton;

enum class FillMode { Solid, Wireframe };

struct GpuVertex {
    Vec3 position;
    Vec3 normal;
    float color[4];
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
    void beginScenePass(int x, int y, int w, int h, const float clearColor[4]);
    void endScenePass();

    // Uploads (or re-uploads) a mesh. Colors come from the caller so view
    // modes (solid/heatmap/normals/...) stay renderer-agnostic.
    bool uploadMesh(const std::string& key, const std::vector<GpuVertex>& vertices,
                    const std::vector<std::uint32_t>& indices, std::string& outError);
    void releaseMesh(const std::string& key);

    void drawMesh(const std::string& key, const Mat4& worldViewProj, FillMode fill);
    void drawMeshWireOverlay(const std::string& key, const Mat4& worldViewProj);
    void drawLines(const std::vector<GpuVertex>& segments, const Mat4& worldViewProj);
    void drawLinesXRay(const std::vector<GpuVertex>& segments, const Mat4& worldViewProj);

    void present(bool vsync);

    // Opaque backend handles for the ImGui DX11 backend (valid after init).
    void* deviceForBackend();
    void* contextForBackend();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

// Builds GpuVertex arrays from a canonical mesh for a given debug coloring.
enum class MeshColoring { Solid, Normals, Height, Weight, UV };
std::vector<GpuVertex> buildGpuVertices(const Mesh& mesh, MeshColoring coloring);
// Weight heatmap for one bone: blue (0) -> red (1).
std::vector<GpuVertex> buildGpuVerticesWeight(const Mesh& mesh, std::uint32_t bone);
// Animation preview: positions/normals deformed by the skinning palette.
std::vector<GpuVertex> buildGpuVerticesDeformed(const Mesh& mesh,
                                                const std::vector<Mat4>& palette,
                                                MeshColoring coloring, std::uint32_t bone);

// Grid + axis lines centered at origin.
std::vector<GpuVertex> buildGridLines(float halfExtent = 5.0f, float step = 0.5f);

}  // namespace m2rig
