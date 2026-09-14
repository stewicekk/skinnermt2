// Canonical mesh: bounds, normals, structural validation.
#include "m2rig/mesh.hpp"

#include <cmath>
#include <unordered_set>

#include "m2rig/diagnostics.hpp"

namespace m2rig {

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

void computeNormals(Mesh& mesh, bool overwriteExisting) {
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
    (void)overwriteExisting;
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
}

}  // namespace m2rig
