#pragma once
// Minimal Wavefront OBJ reader (static geometry, no skinning). Lives in
// m2rig_core (zero third-party deps, same posture as smd.cpp). Supports
// v/vt/vn/f with 1-based and negative (relative) indices, usemtl submesh
// grouping, o/g object names, and # comments. No influences are produced —
// the caller assigns a rigid bind (the mesh-only import path does this).
// Fails explicitly on IO errors, malformed face references, or non-finite
// positions. Never partial, never silent.
#include <string>

#include "m2rig/mesh.hpp"
#include "m2rig/result.hpp"

namespace m2rig {

// Parses an .obj file into a Mesh with empty influences (static geometry).
// Positions/normals/uvs are preserved verbatim (no recompute). Submeshes are
// grouped by usemtl (a default submesh is created when none is declared).
Result<Mesh> parseObjFile(const std::string& path);

}  // namespace m2rig
