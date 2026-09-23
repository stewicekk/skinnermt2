// Edge-collapse decimation (LOD generator). Shortest-edge-first, fully
// deterministic, skinning-preserving. See lod.hpp for the contract.
#include "m2rig/lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <vector>

namespace m2rig {

namespace {

constexpr std::size_t kNoSubMesh = static_cast<std::size_t>(-1);

struct WorkTri {
    std::uint32_t v[3];
    std::size_t sub;  // index into mesh.subMeshes, or kNoSubMesh
};

bool triDegenerate(const WorkTri& t) { return t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]; }

}  // namespace

std::string LodStats::toDisplayString() const {
    std::ostringstream out;
    const double pct = inputTriangles > 0
                           ? 100.0 * static_cast<double>(outputTriangles) /
                                 static_cast<double>(inputTriangles)
                           : 0.0;
    out << "LOD: " << inputTriangles << " -> " << outputTriangles << " tris (" << pct
        << "% kept), " << inputVertices << " -> " << outputVertices << " verts, " << collapses
        << " collapses, " << degenerateRemoved << " degenerate removed, droppedMass "
        << removedMass;
    return out.str();
}

Result<LodStats> decimateMesh(Mesh& mesh, LodOptions options) {
    LodStats stats;
    stats.inputVertices = mesh.vertices.size();
    if (mesh.vertices.empty())
        return Result<LodStats>::fail("LOD needs a non-empty mesh.", "MESH", mesh.name);
    if (!(options.targetRatio > 0.0f) || !(options.targetRatio < 1.0f))
        return Result<LodStats>::fail("LOD targetRatio must be in (0,1) exclusive.", "MESH",
                                      mesh.name);

    // Build the working triangle soup: only 3 distinct in-range ids survive
    // (degenerate/out-of-range tris are their validators' business and are
    // skipped here, exactly like buildMeshTopology).
    std::vector<WorkTri> tris;
    tris.reserve(mesh.triangleCount());
    const std::size_t nVerts = mesh.vertices.size();
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= nVerts || b >= nVerts || c >= nVerts) continue;
        if (a == b || b == c || a == c) continue;
        std::size_t sub = kNoSubMesh;
        for (std::size_t s = 0; s < mesh.subMeshes.size(); ++s) {
            const auto& sm = mesh.subMeshes[s];
            if (i >= sm.startIndex && i + 2 < sm.startIndex + sm.indexCount) {
                sub = s;
                break;
            }
        }
        tris.push_back({{a, b, c}, sub});
    }
    stats.inputTriangles = tris.size();
    if (tris.empty())
        return Result<LodStats>::fail("LOD needs at least one valid triangle.", "MESH", mesh.name);

    const std::size_t keep =
        std::max(options.minTriangles,
                 static_cast<std::size_t>(static_cast<double>(tris.size()) * options.targetRatio));
    if (keep >= tris.size()) {
        // No-op success: report the census, change nothing.
        stats.outputTriangles = tris.size();
        stats.outputVertices = mesh.vertices.size();
        return Result<LodStats>::ok(stats);
    }

    RepairStats repair;
    // Shortest-edge collapse loop. Cost order (length, minId, maxId) with
    // strict-less over fixed triangle/edge order => deterministic.
    while (tris.size() > keep && stats.collapses < options.maxCollapses) {
        float bestLen = std::numeric_limits<float>::infinity();
        std::uint32_t bestLo = 0, bestHi = 0;
        bool found = false;
        for (const auto& t : tris) {
            for (int e = 0; e < 3; ++e) {
                const std::uint32_t a = t.v[e], b = t.v[(e + 1) % 3];
                const float len = distance(mesh.vertices[a].position, mesh.vertices[b].position);
                const std::uint32_t lo = a < b ? a : b;
                const std::uint32_t hi = a < b ? b : a;
                // Strict-less lexicographic (length, lo, hi) over a fixed
                // order: NaN lengths never win, ties go to the first edge.
                if (len < bestLen ||
                    (len == bestLen && found &&
                     (lo < bestLo || (lo == bestLo && hi < bestHi)))) {
                    bestLen = len;
                    bestLo = lo;
                    bestHi = hi;
                    found = true;
                }
            }
        }
        if (!found || !std::isfinite(bestLen)) break;  // NaN positions: stop, keep partial
        const std::uint32_t survivor = bestLo < bestHi ? bestLo : bestHi;
        const std::uint32_t removed = bestLo < bestHi ? bestHi : bestLo;
        // Merge: midpoint position, survivor's UV/color, union of influences
        // through the standard repair (<=4, normalized, mass accounted).
        Vertex& s = mesh.vertices[survivor];
        const Vertex& o = mesh.vertices[removed];
        s.position = (s.position + o.position) * 0.5f;
        std::vector<BoneInfluence> merged = s.influences;
        merged.insert(merged.end(), o.influences.begin(), o.influences.end());
        repairVertexInfluences(merged, kMetin2MaxInfluences, &repair);
        s.influences = std::move(merged);
        for (auto& t : tris)
            for (int k = 0; k < 3; ++k)
                if (t.v[k] == removed) t.v[k] = survivor;
        const std::size_t before = tris.size();
        tris.erase(std::remove_if(tris.begin(), tris.end(), triDegenerate), tris.end());
        stats.degenerateRemoved += before - tris.size();
        ++stats.collapses;
    }

    // Compact vertices (old-id order => deterministic) and regroup indices
    // by original submesh order; emptied submeshes are dropped (documented:
    // LOD output is an export artifact, never a live-session mutation).
    std::vector<std::uint32_t> remap(nVerts, 0xFFFFFFFFu);
    std::vector<Vertex> verts;
    verts.reserve(nVerts);
    for (const auto& t : tris) {
        for (int k = 0; k < 3; ++k) {
            if (remap[t.v[k]] == 0xFFFFFFFFu) {
                remap[t.v[k]] = static_cast<std::uint32_t>(verts.size());
                verts.push_back(mesh.vertices[t.v[k]]);
            }
        }
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(tris.size() * 3);
    std::vector<SubMesh> subMeshes;
    for (std::size_t s = 0; s < mesh.subMeshes.size(); ++s) {
        SubMesh sm;
        sm.name = mesh.subMeshes[s].name;
        sm.materialIndex = mesh.subMeshes[s].materialIndex;
        sm.startIndex = indices.size();
        for (const auto& t : tris) {
            if (t.sub != s) continue;
            indices.push_back(remap[t.v[0]]);
            indices.push_back(remap[t.v[1]]);
            indices.push_back(remap[t.v[2]]);
        }
        sm.indexCount = indices.size() - sm.startIndex;
        if (sm.indexCount > 0) subMeshes.push_back(sm);
    }
    bool orphans = false;
    for (const auto& t : tris)
        if (t.sub == kNoSubMesh) {
            orphans = true;
            break;
        }
    if (orphans) {
        SubMesh sm;
        sm.name = "lod_orphans";
        sm.materialIndex = 0;
        sm.startIndex = indices.size();
        for (const auto& t : tris) {
            if (t.sub != kNoSubMesh) continue;
            indices.push_back(remap[t.v[0]]);
            indices.push_back(remap[t.v[1]]);
            indices.push_back(remap[t.v[2]]);
        }
        sm.indexCount = indices.size() - sm.startIndex;
        if (sm.indexCount > 0) subMeshes.push_back(sm);
    }

    mesh.vertices = std::move(verts);
    mesh.indices = std::move(indices);
    mesh.subMeshes = std::move(subMeshes);
    computeBounds(mesh);
    computeNormals(mesh);
    // Tangents follow the decimated positions/normals (stale source tangents
    // would shade wrong); the normal-map consumers arrive in Wave 27.
    computeTangents(mesh);
    stats.outputTriangles = tris.size();
    stats.outputVertices = mesh.vertices.size();
    stats.removedMass = repair.removedMass;
    return Result<LodStats>::ok(stats);
}

}  // namespace m2rig
