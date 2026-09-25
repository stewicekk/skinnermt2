// Canonical mesh: bounds, normals, structural validation.
#include "m2rig/mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

// Order-independent key for the MESH_UV_OVERLAP census: three quantized
// (u, v) pairs sorted lexicographically, so winding and index order cannot
// affect the key. Only counts derived from the map are reported, keeping
// the finding fully deterministic regardless of hash iteration order.
struct UvTriKey {
    std::int64_t q[6] = {0, 0, 0, 0, 0, 0};
    bool operator==(const UvTriKey& o) const {
        for (int i = 0; i < 6; ++i)
            if (q[i] != o.q[i]) return false;
        return true;
    }
};

struct UvTriKeyHash {
    std::size_t operator()(const UvTriKey& k) const noexcept {
        std::size_t h = static_cast<std::size_t>(1469598103934665603ULL);
        for (int i = 0; i < 6; ++i) {
            h ^= static_cast<std::size_t>(static_cast<std::uint64_t>(k.q[i]));
            h *= static_cast<std::size_t>(1099511628211ULL);
        }
        return h;
    }
};

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
    // UV range census (MESH_UV_RANGE, Info only): tiling/wrap outside [0,1]
    // is normal in game assets, so this never warns. Non-finite UVs are
    // already a hard MESH_NON_FINITE error above and are skipped here so
    // they cannot pollute the min/max. Counts and extrema are
    // order-independent (no iteration-order dependence).
    {
        std::size_t outOfRange = 0;
        bool haveFinite = false;
        double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0;
        for (const auto& v : mesh.vertices) {
            const float uf = v.uv0.x, vf = v.uv0.y;
            if (!isFiniteF(uf) || !isFiniteF(vf)) continue;  // MESH_NON_FINITE
            const double u = static_cast<double>(uf);
            const double vv = static_cast<double>(vf);
            if (!haveFinite) {
                uMin = uMax = u;
                vMin = vMax = vv;
                haveFinite = true;
            } else {
                if (u < uMin) uMin = u;
                if (u > uMax) uMax = u;
                if (vv < vMin) vMin = vv;
                if (vv > vMax) vMax = vv;
            }
            if (uf < 0.0f || uf > 1.0f || vf < 0.0f || vf > 1.0f) ++outOfRange;
        }
        if (haveFinite) {
            std::ostringstream msg;
            msg << outOfRange << "/" << mesh.vertices.size() << " verts outside [0,1] (u "
                << uMin << ".." << uMax << ", v " << vMin << ".." << vMax << ").";
            report.add("MESH_UV_RANGE", ValidationCategory::Mesh, Severity::Info, msg.str(),
                        asset, mesh.name, false);
        }
    }
    // Degenerate-UV census (MESH_UV_DEGENERATE, Warning, non-blocking):
    // zero UV area breaks computeTangents (skips det<1e-12 and falls back
    // to (1,0,0,1), degrading normal-mapped shading). Same valid-triangle
    // policy as the topology census (3 distinct in-range ids); non-finite
    // UVs are covered by MESH_NON_FINITE and skipped. The count is
    // order-independent; the reported first index is positional only.
    {
        const std::size_t vertCount = mesh.vertices.size();
        constexpr double kIdentSq = 1e-18;  // (1e-9 Euclidean)^2
        constexpr double kDetTol = 1e-12;   // matches computeTangents skip
        std::size_t degUv = 0;
        std::size_t firstTri = 0;
        bool haveFirst = false;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const std::uint32_t a = mesh.indices[i];
            const std::uint32_t b = mesh.indices[i + 1];
            const std::uint32_t c = mesh.indices[i + 2];
            if (a >= vertCount || b >= vertCount || c >= vertCount) continue;
            if (a == b || b == c || a == c) continue;
            const float u0 = mesh.vertices[a].uv0.x, v0 = mesh.vertices[a].uv0.y;
            const float u1 = mesh.vertices[b].uv0.x, v1 = mesh.vertices[b].uv0.y;
            const float u2 = mesh.vertices[c].uv0.x, v2 = mesh.vertices[c].uv0.y;
            if (!isFiniteF(u0) || !isFiniteF(v0) || !isFiniteF(u1) || !isFiniteF(v1) ||
                !isFiniteF(u2) || !isFiniteF(v2))
                continue;
            const double du01 = static_cast<double>(u1) - static_cast<double>(u0);
            const double dv01 = static_cast<double>(v1) - static_cast<double>(v0);
            const double du12 = static_cast<double>(u2) - static_cast<double>(u1);
            const double dv12 = static_cast<double>(v2) - static_cast<double>(v1);
            const double du20 = static_cast<double>(u0) - static_cast<double>(u2);
            const double dv20 = static_cast<double>(v0) - static_cast<double>(v2);
            const bool identical = (du01 * du01 + dv01 * dv01 < kIdentSq) &&
                                   (du12 * du12 + dv12 * dv12 < kIdentSq) &&
                                   (du20 * du20 + dv20 * dv20 < kIdentSq);
            const double e1u = static_cast<double>(u1) - static_cast<double>(u0);
            const double e1v = static_cast<double>(v1) - static_cast<double>(v0);
            const double e2u = static_cast<double>(u2) - static_cast<double>(u0);
            const double e2v = static_cast<double>(v2) - static_cast<double>(v0);
            const double det = e1u * e2v - e1v * e2u;
            if (identical || std::fabs(det) < kDetTol) {
                ++degUv;
                if (!haveFirst) {
                    firstTri = i / 3;
                    haveFirst = true;
                }
            }
        }
        if (degUv > 0) {
            std::ostringstream msg;
            msg << degUv << " triangles with degenerate UVs (zero UV area; first tri "
                << firstTri << "; tangents skipped where det<1e-12).";
            report.add("MESH_UV_DEGENERATE", ValidationCategory::Mesh, Severity::Warning,
                        msg.str(), asset, mesh.name, false);
        }
    }
    // Duplicate-UV census (MESH_UV_OVERLAP, Info only): exact-duplicate UV
    // triangles via O(T) hash of quantized (1e-6) uv triples, sorted
    // order-independent so winding/index order cannot affect the key. Only
    // group/wasted counts are reported. Same valid-triangle policy as the
    // topology census; non-finite or extreme-magnitude UVs are skipped
    // (MESH_NON_FINITE covers the former; llround would overflow on the
    // latter, so they are honestly left out of this census).
    {
        const std::size_t vertCount = mesh.vertices.size();
        constexpr double kQuant = 1e6;
        constexpr double kMaxQuantMag = 8e12;  // llround(int64) stays in range
        std::unordered_map<UvTriKey, std::size_t, UvTriKeyHash> freq;
        freq.reserve(mesh.indices.size() / 3 + 1);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const std::uint32_t a = mesh.indices[i];
            const std::uint32_t b = mesh.indices[i + 1];
            const std::uint32_t c = mesh.indices[i + 2];
            if (a >= vertCount || b >= vertCount || c >= vertCount) continue;
            if (a == b || b == c || a == c) continue;
            const float u0 = mesh.vertices[a].uv0.x, v0 = mesh.vertices[a].uv0.y;
            const float u1 = mesh.vertices[b].uv0.x, v1 = mesh.vertices[b].uv0.y;
            const float u2 = mesh.vertices[c].uv0.x, v2 = mesh.vertices[c].uv0.y;
            if (!isFiniteF(u0) || !isFiniteF(v0) || !isFiniteF(u1) || !isFiniteF(v1) ||
                !isFiniteF(u2) || !isFiniteF(v2))
                continue;
            if (std::fabs(u0) > kMaxQuantMag || std::fabs(v0) > kMaxQuantMag ||
                std::fabs(u1) > kMaxQuantMag || std::fabs(v1) > kMaxQuantMag ||
                std::fabs(u2) > kMaxQuantMag || std::fabs(v2) > kMaxQuantMag)
                continue;
            const std::int64_t q0u = static_cast<std::int64_t>(
                std::llround(static_cast<double>(u0) * kQuant));
            const std::int64_t q0v = static_cast<std::int64_t>(
                std::llround(static_cast<double>(v0) * kQuant));
            const std::int64_t q1u = static_cast<std::int64_t>(
                std::llround(static_cast<double>(u1) * kQuant));
            const std::int64_t q1v = static_cast<std::int64_t>(
                std::llround(static_cast<double>(v1) * kQuant));
            const std::int64_t q2u = static_cast<std::int64_t>(
                std::llround(static_cast<double>(u2) * kQuant));
            const std::int64_t q2v = static_cast<std::int64_t>(
                std::llround(static_cast<double>(v2) * kQuant));
            std::array<std::pair<std::int64_t, std::int64_t>, 3> pairs = {
                std::make_pair(q0u, q0v), std::make_pair(q1u, q1v),
                std::make_pair(q2u, q2v)};
            std::sort(pairs.begin(), pairs.end());
            UvTriKey key;
            key.q[0] = pairs[0].first;
            key.q[1] = pairs[0].second;
            key.q[2] = pairs[1].first;
            key.q[3] = pairs[1].second;
            key.q[4] = pairs[2].first;
            key.q[5] = pairs[2].second;
            ++freq[key];
        }
        std::size_t groups = 0, inGroups = 0;
        for (const auto& kv : freq) {
            if (kv.second > 1) {
                ++groups;
                inGroups += kv.second;
            }
        }
        if (groups > 0) {
            const std::size_t wasted = inGroups - groups;
            std::ostringstream msg;
            msg << groups << " duplicate-UV groups covering " << inGroups << " triangles ("
                << wasted << " wasted; quantized at 1e-6, order-independent).";
            report.add("MESH_UV_OVERLAP", ValidationCategory::Mesh, Severity::Info, msg.str(),
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
