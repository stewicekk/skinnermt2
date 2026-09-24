#pragma once
// Canonical mesh representation (spec section 6).
#include <cstdint>
#include <string>
#include <vector>

#include "m2rig/math.hpp"
#include "m2rig/result.hpp"
#include "m2rig/validation.hpp"

namespace m2rig {

constexpr std::uint32_t kInvalidBone = 0xFFFFFFFFu;

struct BoneInfluence {
    std::uint32_t bone = kInvalidBone;  // index into Skeleton::bones
    float weight = 0.0f;
};

struct Vertex {
    Vec3 position{0, 0, 0};
    Vec3 normal{0, 0, 1};
    Vec4 tangent{0, 0, 0, 1};
    bool hasTangent = false;
    Vec2 uv0{0, 0};
    Vec2 uv1{0, 0};
    bool hasUv1 = false;
    Vec4 color{1, 1, 1, 1};
    bool hasColor = false;
    std::vector<BoneInfluence> influences;
};

struct SubMesh {
    std::string name;
    std::uint32_t materialIndex = 0;
    std::size_t startIndex = 0;
    std::size_t indexCount = 0;
};

struct MaterialRef {
    std::string name;         // e.g. "armor_body.dds"
    std::string texturePath;  // editable client path
    // Editable tangent-space normal-map path (linear data: uploaded with
    // srgb=false under <assetId>#nmat<i>, bound per submesh on the
    // textured-PBR path). Default empty = geometry normals. SMD/FBX/MSM
    // producers leave it empty; aggregate init ({name, path}) keeps
    // compiling against the defaulted third member.
    std::string normalTexturePath;
};

struct Mesh {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;  // triangles, 3 per face
    std::vector<SubMesh> subMeshes;
    std::vector<MaterialRef> materials;
    Aabb bounds;
    Vec3 boundingSphereCenter{0, 0, 0};
    float boundingSphereRadius = 0.0f;

    std::size_t triangleCount() const { return indices.size() / 3; }
};

void computeBounds(Mesh& mesh);
void computeNormals(Mesh& mesh);
void computeTangents(Mesh& mesh);
Vec3 triangleNormal(const Vec3& a, const Vec3& b, const Vec3& c);

// Edge-hash adjacency over the index buffer. Only topologically valid
// triangles (3 distinct in-range vertex ids) contribute edges; degenerate
// triangles are already flagged by MESH_DEGENERATE and out-of-range ids by
// MESH_BAD_INDEX, so both are skipped here. Counts are order-independent
// (fully deterministic regardless of triangle winding/order).
struct MeshTopology {
    std::size_t validTriangles = 0;
    std::size_t boundaryEdges = 0;     // used by exactly 1 triangle
    std::size_t manifoldEdges = 0;     // used by exactly 2 triangles
    std::size_t nonManifoldEdges = 0;  // used by 3+ triangles
    std::size_t isolatedVertices = 0;  // referenced by no valid triangle
    std::size_t openVertices = 0;      // touching at least one boundary edge
    std::string toDisplayString() const;
};

MeshTopology buildMeshTopology(const Mesh& mesh);

// Structural checks only (counts, ranges, topology). Weight semantics live
// in skin_weights.hpp. Findings are appended to the report.
void validateMeshStructure(const Mesh& mesh, const std::string& assetName,
                           ValidationReport& report);

}  // namespace m2rig
