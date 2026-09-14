// Renderer-agnostic mesh views: solid/normals/height colorings, weight
// heatmap per bone, and grid lines. Dependency-free (no D3D includes) so
// both the core test harness and the D3D11 backend can use them.
#include "m2rig/renderer.hpp"

#include <cmath>

#include "m2rig/mesh.hpp"
#include "m2rig/skin_weights.hpp"

namespace m2rig {

std::vector<GpuVertex> buildGpuVertices(const Mesh& mesh, MeshColoring coloring) {
    std::vector<GpuVertex> out;
    out.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        GpuVertex g{};
        g.position = v.position;
        g.normal = v.normal;
        g.uv[0] = v.uv0.x;
        g.uv[1] = v.uv0.y;
        float r = 0.75f, gg = 0.76f, b = 0.78f;
        if (coloring == MeshColoring::Normals) {
            r = v.normal.x * 0.5f + 0.5f;
            gg = v.normal.y * 0.5f + 0.5f;
            b = v.normal.z * 0.5f + 0.5f;
        } else if (coloring == MeshColoring::Height) {
            const float t = v.position.y * 0.25f + 0.5f;
            r = t;
            gg = 0.2f + 0.5f * (1.0f - t);
            b = 1.0f - t;
        } else if (coloring == MeshColoring::Weight) {
            // Without a selected bone, Weight falls back to total-weight
            // intensity so the mode never renders garbage.
            float w = influenceTotal(v.influences);
            if (w < 0.0f) w = 0.0f;
            if (w > 1.0f) w = 1.0f;
            r = w;
            gg = 0.2f + 0.6f * (1.0f - w);
            b = 1.0f - w;
        } else if (coloring == MeshColoring::UV) {
            float u = v.uv0.x - std::floor(v.uv0.x);
            float vv = v.uv0.y - std::floor(v.uv0.y);
            const float cx = std::floor(u * 8.0f);
            const float cy = std::floor(vv * 8.0f);
            const float check = (static_cast<int>(cx + cy) % 2 == 0) ? 1.0f : 0.15f;
            r = u * check + 0.05f;
            gg = vv * check + 0.05f;
            b = 0.35f * check + 0.1f;
        }
        g.color[0] = r;
        g.color[1] = gg;
        g.color[2] = b;
        g.color[3] = 1.0f;
        out.push_back(g);
    }
    return out;
}

std::vector<GpuVertex> buildGpuVerticesWeight(const Mesh& mesh, std::uint32_t bone) {
    std::vector<GpuVertex> out;
    out.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        GpuVertex g{};
        g.position = v.position;
        g.normal = v.normal;
        g.uv[0] = v.uv0.x;
        g.uv[1] = v.uv0.y;
        float w = weightOfBone(v.influences, bone);
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;
        // Jet-ish ramp: blue -> cyan -> green -> yellow -> red.
        const float r =
            w < 0.5f ? (w * 2.0f) * (w * 2.0f) * 0.2f + (w > 0.25f ? (w - 0.25f) * 1.6f : 0.0f)
                     : 1.0f;
        const float b = w < 0.5f ? 1.0f - w * 1.2f : (1.0f - w) * 1.6f;
        const float gg = w < 0.25f ? w * 2.4f : (w < 0.75f ? 1.0f : (1.0f - w) * 4.0f);
        g.color[0] = r > 1.0f ? 1.0f : r;
        g.color[1] = gg < 0.0f ? 0.0f : (gg > 1.0f ? 1.0f : gg);
        g.color[2] = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
        g.color[3] = 1.0f;
        out.push_back(g);
    }
    return out;
}

std::vector<GpuVertex> buildGpuVerticesDeformed(const Mesh& mesh,
                                                const std::vector<Mat4>& palette,
                                                MeshColoring coloring, std::uint32_t bone) {
    std::vector<GpuVertex> out;
    out.reserve(mesh.vertices.size());
    for (const auto& v : mesh.vertices) {
        GpuVertex g{};
        g.position = deformVertex(v.position, v.influences, palette);
        g.normal = deformNormal(v.normal, v.influences, palette);
        g.uv[0] = v.uv0.x;
        g.uv[1] = v.uv0.y;
        float r = 0.75f, gg = 0.76f, b = 0.78f;
        if (coloring == MeshColoring::Normals) {
            r = g.normal.x * 0.5f + 0.5f;
            gg = g.normal.y * 0.5f + 0.5f;
            b = g.normal.z * 0.5f + 0.5f;
        } else if (coloring == MeshColoring::Weight) {
            float w = weightOfBone(v.influences, bone);
            if (w < 0.0f) w = 0.0f;
            if (w > 1.0f) w = 1.0f;
            r = w;
            gg = 0.2f + 0.6f * (1.0f - w);
            b = 1.0f - w;
        } else if (coloring == MeshColoring::UV) {
            float u = v.uv0.x - std::floor(v.uv0.x);
            float vv = v.uv0.y - std::floor(v.uv0.y);
            const float cx = std::floor(u * 8.0f);
            const float cy = std::floor(vv * 8.0f);
            const float check = (static_cast<int>(cx + cy) % 2 == 0) ? 1.0f : 0.15f;
            r = u * check + 0.05f;
            gg = vv * check + 0.05f;
            b = 0.35f * check + 0.1f;
        }
        g.color[0] = r;
        g.color[1] = gg;
        g.color[2] = b;
        g.color[3] = 1.0f;
        out.push_back(g);
    }
    return out;
}

std::vector<std::uint32_t> filterVisibleIndices(const Mesh& mesh,
                                                const std::set<std::size_t>& hidden) {
    if (hidden.empty()) return mesh.indices;
    std::vector<std::uint32_t> out;
    out.reserve(mesh.indices.size());
    for (std::size_t i = 0; i < mesh.subMeshes.size(); ++i) {
        if (hidden.count(i) != 0) continue;
        const auto& sm = mesh.subMeshes[i];
        const std::size_t end = sm.startIndex + sm.indexCount;
        if (sm.startIndex >= mesh.indices.size()) continue;
        for (std::size_t k = sm.startIndex; k < end && k < mesh.indices.size(); ++k)
            out.push_back(mesh.indices[k]);
    }
    return out;
}

std::vector<GpuVertex> buildGridLines(float halfExtent, float step) {
    std::vector<GpuVertex> lines;
    const float axisY = 0.0f;
    auto push = [&](const Vec3& a, const Vec3& b, float r, float g, float bl) {
        GpuVertex v0{}, v1{};
        v0.position = a;
        v1.position = b;
        v0.normal = v1.normal = {0, 1, 0};
        v0.color[0] = v1.color[0] = r;
        v0.color[1] = v1.color[1] = g;
        v0.color[2] = v1.color[2] = bl;
        v0.color[3] = v1.color[3] = 1.0f;
        lines.push_back(v0);
        lines.push_back(v1);
    };
    for (float c = -halfExtent; c <= halfExtent + 1e-5f; c += step) {
        const bool major = std::fabs(c) < 1e-5f;
        const float t = major ? 0.55f : 0.28f;
        push({c, axisY, -halfExtent}, {c, axisY, halfExtent}, t, t, t);
        push({-halfExtent, axisY, c}, {halfExtent, axisY, c}, t, t, t);
    }
    // RGB axes.
    push({0, 0.01f, 0}, {1.5f, 0.01f, 0}, 1, 0.25f, 0.25f);
    push({0, 0.01f, 0}, {0, 1.5f, 0.01f}, 0.25f, 1, 0.25f);
    push({0, 0.01f, 0}, {0, 0.01f, 1.5f}, 0.3f, 0.5f, 1);
    return lines;
}

}  // namespace m2rig
