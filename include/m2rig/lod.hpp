#pragma once
// Edge-collapse mesh decimation (LOD1/LOD2 generator, wave 19).
// Deterministic shortest-edge collapse that preserves skinning data:
// merged vertices carry the union of both influence lists through the
// standard repair (<=4 influences, normalized, removed mass reported).
// Boundary erosion and UV-seam choices of a plain collapse scheme apply
// and are documented, not hidden. Operates on the caller's mesh in place;
// export flows (CLI/GUI) decimate a COPY so live session data is untouched.
#include <cstddef>
#include <string>

#include "m2rig/mesh.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skin_weights.hpp"

namespace m2rig {

struct LodOptions {
    float targetRatio = 0.5f;        // (0,1) exclusive: fraction of triangles to KEEP
    std::size_t minTriangles = 4;    // never decimate below this many triangles
    std::size_t maxCollapses = 500000;  // safety cap; partial output still succeeds
};

struct LodStats {
    std::size_t inputTriangles = 0;
    std::size_t outputTriangles = 0;
    std::size_t inputVertices = 0;
    std::size_t outputVertices = 0;
    std::size_t collapses = 0;
    std::size_t degenerateRemoved = 0;
    double removedMass = 0.0;  // influence mass discarded by merge repairs
    std::string toDisplayString() const;
};

// Decimates mesh in place towards targetRatio. Fails explicitly on empty
// meshes, triangle-less meshes and out-of-range options; a no-op (target >=
// input) succeeds with zero collapses. Fully deterministic: edge costs are
// compared as (length, minId, maxId) with strict-less over a fixed triangle
// order, so two runs on equal inputs produce bit-identical output.
Result<LodStats> decimateMesh(Mesh& mesh, LodOptions options = {});

}  // namespace m2rig
