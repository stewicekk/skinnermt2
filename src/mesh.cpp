// Canonical mesh: bounds, normals, structural validation.
#include "m2rig/mesh.hpp"

#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "m2rig/diagnostics.hpp"

namespace m2rig {

namespace {

std::uint64_t edgeKey(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t lo = a < b ? a : b;
    const std::uint32_t hi = a < b ? b : a;
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

}  // namespace

std::string MeshTopology::toDisplayString() const {
    std::ostringstream out;
    out << "topology: " << validTriangles << " tris, " << boundaryEdges << " boundary edges, "
        << manifoldEdges << " manifold, " << nonManifoldEdges << " non-manifold, "
        << isolatedVertices << " isolated verts, " << openVertices << " open verts";
    return out.str();
}

MeshTopology buildMeshTopology(const Mesh& mesh) {
    MeshTopology topo;
    const std::size_t vertCount = mesh.vertices.size();
    if (vertCount == 0) return topo;
    std::unordered_map<std::uint64_t, std::uint32_t> edgeUse;
    edgeUse.reserve(mesh.indices.size());
    std::vector<char> referenced(vertCount, 0);
    std::vector<char> open(vertCount, 0);
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= vertCount || b >= vertCount || c >= vertCount) continue;  // MESH_BAD_INDEX
        if (a == b || b == c || a == c) continue;                         // MESH_DEGENERATE
        ++topo.validTriangles;
        referenced[a] = referenced[b] = referenced[c] = 1;
        ++edgeUse[edgeKey(a, b)];
        ++edgeUse[edgeKey(b, c)];
        ++edgeUse[edgeKey(c, a)];
    }
    for (const auto& [key, count] : edgeUse) {
        if (count == 1) {
            ++topo.boundaryEdges;
            const auto u = static_cast<std::uint32_t>(key >> 32);
            const auto v = static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
            open[u] = open[v] = 1;
        } else if (count == 2) {
            ++topo.manifoldEdges;
        } else {
            ++topo.nonManifoldEdges;
        }
    }
    for (std::size_t i = 0; i < vertCount; ++i) {
        if (!referenced[i]) ++topo.isolatedVertices;
        if (open[i]) ++topo.openVertices;
    }
    return topo;
}

void computeBounds(Mesh& mesh) {
    Aabb box;
    for (const auto& v : mesh.vertices) box.grow(v.position);
    mesh.bounds = box;
    if (!box.empty) {
        mesh.boundingSphereCenter = box.center();
        mesh.boundingSphereRadius = box.radius();
    } else {
        mesh.boundingSphereCenter = {0, 0, 0};
        mesh.boundingSphereRadius = 0.0f;
    }
}

Vec3 triangleNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    return normalized(cross(b - a, c - a));
}

// Recomputes smooth area-weighted normals from topology (always overwrites:
// there is no preservation mode — importers that must keep file normals
// simply do not call this).
void computeNormals(Mesh& mesh) {
    for (auto& v : mesh.vertices) v.normal = {0, 0, 0};
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())
            continue;
        const Vec3 n = cross(mesh.vertices[b].position - mesh.vertices[a].position,
                             mesh.vertices[c].position - mesh.vertices[a].position);
        mesh.vertices[a].normal += n;
        mesh.vertices[b].normal += n;
        mesh.vertices[c].normal += n;
    }
    for (auto& v : mesh.vertices) {
        const float l = length(v.normal);
        v.normal = (l > 1e-12f) ? v.normal / l : Vec3{0, 0, 1};
    }
}

void validateMeshStructure(const Mesh& mesh, const std::string& assetName,
                           ValidationReport& report) {
    const std::string asset = assetName.empty() ? mesh.name : assetName;
    if (mesh.vertices.empty()) {
        report.add("MESH_EMPTY", ValidationCategory::Mesh, Severity::Error, "Mesh has no vertices.",
                   asset, mesh.name, false);
        return;
    }
    if (mesh.indices.size() % 3 != 0) {
        report.add("MESH_INDEX_COUNT", ValidationCategory::Mesh, Severity::Error,
                   "Index count " + std::to_string(mesh.indices.size()) +
                       " is not a multiple of 3.",
                   asset, mesh.name, true);
    }
    std::size_t badIndex = 0;
    for (std::size_t i = 0; i < mesh.indices.size(); ++i)
        if (mesh.indices[i] >= mesh.vertices.size()) ++badIndex;
    if (badIndex > 0) {
        report.add("MESH_BAD_INDEX", ValidationCategory::Mesh, Severity::Error,
                   std::to_string(badIndex) + " indices reference non-existent vertices.", asset,
                   mesh.name, true);
    }
    std::size_t badFloat = 0;
    for (const auto& v : mesh.vertices) {
        if (!isFiniteF(v.position.x) || !isFiniteF(v.position.y) || !isFiniteF(v.position.z) ||
            !isFiniteF(v.normal.x) || !isFiniteF(v.normal.y) || !isFiniteF(v.normal.z) ||
            !isFiniteF(v.uv0.x) || !isFiniteF(v.uv0.y)) {
            ++badFloat;
        }
    }
    if (badFloat > 0) {
        report.add("MESH_NON_FINITE", ValidationCategory::Mesh, Severity::Error,
                   std::to_string(badFloat) + " vertices contain NaN/inf data.", asset, mesh.name,
                   true);
    }
    // Degenerate triangles.
    std::size_t degenerate = 0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())
            continue;
        const Vec3 e1 = mesh.vertices[b].position - mesh.vertices[a].position;
        const Vec3 e2 = mesh.vertices[c].position - mesh.vertices[a].position;
        if (length(cross(e1, e2)) < 1e-12f) ++degenerate;
    }
    if (degenerate > 0) {
        report.add("MESH_DEGENERATE", ValidationCategory::Mesh, Severity::Warning,
                   std::to_string(degenerate) + " degenerate (zero-area) triangles.", asset,
                   mesh.name, true);
    }
    // Submesh ranges.
    for (std::size_t s = 0; s < mesh.subMeshes.size(); ++s) {
        const auto& sm = mesh.subMeshes[s];
        if (sm.startIndex + sm.indexCount > mesh.indices.size()) {
            report.add("MESH_SUBMESH_RANGE", ValidationCategory::Mesh, Severity::Error,
                       "Submesh '" + sm.name + "' range exceeds index buffer.", asset, sm.name,
                       true);
        }
        if (sm.materialIndex >= mesh.materials.size() && !mesh.materials.empty()) {
            report.add("MESH_SUBMESH_MATERIAL", ValidationCategory::Material, Severity::Warning,
                       "Submesh '" + sm.name + "' references missing material index " +
                           std::to_string(sm.materialIndex) + ".",
                       asset, sm.name, true);
        }
    }
    if (mesh.materials.empty()) {
        report.add("MESH_NO_MATERIAL", ValidationCategory::Material, Severity::Warning,
                   "Mesh has no materials; export will assign a default.", asset, mesh.name, true);
    }
    for (const auto& m : mesh.materials) {
        if (!isValidMaterialName(m.name)) {
            report.add("MESH_BAD_MATERIAL_NAME", ValidationCategory::Material, Severity::Warning,
                       "Suspicious material name '" + m.name + "'.", asset, mesh.name, true);
        }
        if (m.texturePath.empty()) {
            report.add("MESH_MISSING_TEXTURE", ValidationCategory::Texture, Severity::Warning,
                       "Material '" + m.name + "' has no texture path.", asset, m.name, true);
        }
    }
    report.add("MESH_STATS", ValidationCategory::Mesh, Severity::Info,
               "Mesh '" + mesh.name + "': " + std::to_string(mesh.vertices.size()) +
                   " vertices, " + std::to_string(mesh.triangleCount()) + " triangles, " +
                   std::to_string(mesh.subMeshes.size()) + " submeshes.",
               asset, mesh.name, false);
    // Per-submesh triangle budget (hero costume guideline: 10k total).
    constexpr std::size_t kHeroBudgetTris = 10000;
    for (std::size_t s = 0; s < mesh.subMeshes.size(); ++s) {
        const auto& sm = mesh.subMeshes[s];
        const std::size_t tris = sm.indexCount / 3;
        if (tris > kHeroBudgetTris) {
            report.add("MESH_BUDGET", ValidationCategory::Mesh, Severity::Warning,
                       "Submesh '" + sm.name + "' has " + std::to_string(tris) +
                           " triangles (budget " + std::to_string(kHeroBudgetTris) + ").",
                       asset, sm.name, false);
        }
    }
    if (mesh.triangleCount() > kHeroBudgetTris) {
        report.add("MESH_BUDGET", ValidationCategory::Mesh, Severity::Warning,
                   "Mesh total " + std::to_string(mesh.triangleCount()) + " triangles exceeds " +
                       std::to_string(kHeroBudgetTris) + " hero budget.",
                   asset, mesh.name, false);
    }
    // Near-duplicate vertex report (weld candidates, report-only, no auto-merge).
    {
        constexpr float kWeldTol = 1e-4f;
        std::unordered_set<std::int64_t> cells;
        std::size_t nearDup = 0;
        for (const auto& v : mesh.vertices) {
            const std::int64_t key =
                (static_cast<std::int64_t>(std::floor(v.position.x / kWeldTol)) * 73856093LL) ^
                (static_cast<std::int64_t>(std::floor(v.position.y / kWeldTol)) * 19349663LL) ^
                (static_cast<std::int64_t>(std::floor(v.position.z / kWeldTol)) * 83492791LL);
            if (!cells.insert(key).second) ++nearDup;
        }
        if (nearDup > 0) {
            report.add("MESH_NEAR_DUP", ValidationCategory::Mesh, Severity::Info,
                       std::to_string(nearDup) + " vertices share a weld cell (tol 1e-4).",
                       asset, mesh.name, false);
        }
    }
    // Edge-hash topology: boundary/non-manifold census + isolated vertices.
    // Boundary edges are normal for open armor pieces (info only);
    // non-manifold edges and isolated vertices warn without blocking export.
    {
        const MeshTopology topo = buildMeshTopology(mesh);
        report.add("MESH_TOPOLOGY", ValidationCategory::Mesh, Severity::Info,
                   "Mesh '" + mesh.name + "': " + topo.toDisplayString() + ".", asset, mesh.name,
                   false);
        if (topo.nonManifoldEdges > 0) {
            report.add("MESH_NON_MANIFOLD", ValidationCategory::Mesh, Severity::Warning,
                       std::to_string(topo.nonManifoldEdges) +
                           " non-manifold edges (shared by 3+ triangles); smoothing and LOD "
                           "decimation cannot cross them cleanly.",
                       asset, mesh.name, false);
        }
        if (topo.isolatedVertices > 0) {
            report.add("MESH_ISOLATED_VERTS", ValidationCategory::Mesh, Severity::Warning,
                       std::to_string(topo.isolatedVertices) +
                           " vertices are referenced by no triangle (dead weight data).",
                       asset, mesh.name, false);
        }
    }
}

void computeTangents(Mesh& mesh) {
    const std::size_t vertCount = mesh.vertices.size();
    if (vertCount == 0) return;
    std::vector<Vec3> tan1(vertCount, {0,0,0});
    std::vector<Vec3> tan2(vertCount, {0,0,0});
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= vertCount || b >= vertCount || c >= vertCount) continue;
        const Vertex& v0 = mesh.vertices[a];
        const Vertex& v1 = mesh.vertices[b];
        const Vertex& v2 = mesh.vertices[c];
        const Vec3 pos1 = v1.position - v0.position;
        const Vec3 pos2 = v2.position - v0.position;
        const Vec2 uv1 = {v1.uv0.x - v0.uv0.x, v1.uv0.y - v0.uv0.y};
        const Vec2 uv2 = {v2.uv0.x - v0.uv0.x, v2.uv0.y - v0.uv0.y};
        const float det = uv1.x * uv2.y - uv1.y * uv2.x;
        if (std::fabs(det) < 1e-12f) continue;
        const float invDet = 1.0f / det;
        const Vec3 tangent = { (pos1.x * uv2.y - pos2.x * uv1.y) * invDet,
                               (pos1.y * uv2.y - pos2.y * uv1.y) * invDet,
                               (pos1.z * uv2.y - pos2.z * uv1.y) * invDet };
        const Vec3 bitangent = { (pos2.x * uv1.x - pos1.x * uv2.x) * invDet,
                                 (pos2.y * uv1.x - pos1.y * uv2.x) * invDet,
                                 (pos2.z * uv1.x - pos1.z * uv2.x) * invDet };
        tan1[a] += tangent; tan1[b] += tangent; tan1[c] += tangent;
        tan2[a] += bitangent; tan2[b] += bitangent; tan2[c] += bitangent;
    }
    for (std::size_t i = 0; i < vertCount; ++i) {
        Vertex& v = mesh.vertices[i];
        const Vec3 n = v.normal;
        Vec3 t = tan1[i];
        // Gram-Schmidt orthogonalize
        float tn = dot(t, n);
        t = {t.x - tn * n.x, t.y - tn * n.y, t.z - tn * n.z};
        const float tl = length(t);
        if (tl > 1e-12f) {
            t = {t.x / tl, t.y / tl, t.z / tl};
            // Calculate handedness (w component of tangent)
            const Vec3 b = cross(n, t);
            const float handedness = (dot(b, tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            v.tangent = {t.x, t.y, t.z, handedness};
            v.hasTangent = true;
        } else {
            v.tangent = {1, 0, 0, 1};
            v.hasTangent = true;
        }
    }
}

}  // namespace m2rig
