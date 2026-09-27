// Minimal Wavefront OBJ reader (static geometry). See obj.hpp for the contract.
#include "m2rig/obj.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "m2rig/diagnostics.hpp"
#include "m2rig/logging.hpp"

namespace m2rig {
namespace {

bool isFinite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// Parses a long from a string (no sscanf: avoids C4996 under /W4 /WX).
bool parseLong(const std::string& s, long& out) {
    if (s.empty()) return false;
    char* stop = nullptr;
    const long v = std::strtol(s.c_str(), &stop, 10);
    if (stop == s.c_str() || *stop != '\0') return false;
    out = v;
    return true;
}

// Resolves one OBJ index (1-based, or negative = relative to current count).
// Returns false when the index is out of range or zero (OBJ has no index 0).
bool resolveIndex(long idx, std::size_t count, std::size_t& out, std::string& err,
                  const std::string& ctx) {
    if (idx == 0) {
        err = "OBJ index 0 is invalid in " + ctx + ".";
        return false;
    }
    if (idx > 0) {
        if (static_cast<std::size_t>(idx) > count) {
            err = "OBJ index " + std::to_string(idx) + " out of range in " + ctx + " (" +
                  std::to_string(count) + " declared).";
            return false;
        }
        out = static_cast<std::size_t>(idx) - 1;
        return true;
    }
    // Negative: -1 = last declared (count-1), so count + idx.
    const long rel = static_cast<long>(count) + idx;
    if (rel < 0) {
        err = "OBJ relative index " + std::to_string(idx) + " out of range in " + ctx + ".";
        return false;
    }
    out = static_cast<std::size_t>(rel);
    return true;
}

}  // namespace

Result<Mesh> parseObjFile(const std::string& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
        return Result<Mesh>::fail("Cannot open OBJ file.", "FORMAT", path, "obj.parse");
    // Size cap (untrusted files): reject absurd inputs before buffering.
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const SafetyLimits& limits = defaultLimits();
    if (size < 0 || static_cast<std::uint64_t>(size) > limits.maxFileBytes)
        return Result<Mesh>::fail("OBJ file is too large.", "FORMAT", path, "obj.parse");
    in.seekg(0, std::ios::beg);
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    Mesh mesh;
    mesh.name = path;
    std::string err;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::string currentMaterial = "default";
    bool haveMaterial = false;
    std::size_t matIndex = 0;
    std::unordered_map<std::string, std::size_t> matIndexMap;

    // Starts a new submesh run when the material changes (same grouping
    // contract as the SMD mesh-only path).
    auto ensureSubmesh = [&](std::size_t mat) {
        if (mesh.subMeshes.empty() || mesh.subMeshes.back().materialIndex != mat) {
            SubMesh sm;
            sm.name = "submesh_" + std::to_string(mesh.subMeshes.size());
            sm.materialIndex = static_cast<std::uint32_t>(mat);
            sm.startIndex = mesh.indices.size();
            mesh.subMeshes.push_back(sm);
        }
    };
    // Appends one vertex (fan-triangulated face corner) to the mesh.
    auto addCorner = [&](std::size_t posIdx, bool hasUv, std::size_t uvIdx, bool hasNrm,
                         std::size_t nrmIdx) {
        Vertex v;
        v.position = positions[posIdx];
        v.normal = hasNrm ? normals[nrmIdx] : Vec3{0, 0, 1};
        v.uv0 = hasUv ? uvs[uvIdx] : Vec2{0, 0};
        mesh.vertices.push_back(std::move(v));
        mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size() - 1));
    };

    std::istringstream iss(text);
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(iss, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        const std::string ctx = "line " + std::to_string(lineNo);
        if (tag == "v") {
            float x = 0, y = 0, z = 0;
            if (!(ls >> x >> y >> z) || !isFinite3(x, y, z))
                return Result<Mesh>::fail("Invalid OBJ vertex at " + ctx + ".", "FORMAT", path, "obj.parse");
            positions.push_back({x, y, z});
        } else if (tag == "vt") {
            float u = 0, v = 0;
            if (!(ls >> u >> v) || !std::isfinite(u) || !std::isfinite(v))
                return Result<Mesh>::fail("Invalid OBJ uv at " + ctx + ".", "FORMAT", path, "obj.parse");
            uvs.push_back({u, v});
        } else if (tag == "vn") {
            float x = 0, y = 0, z = 0;
            if (!(ls >> x >> y >> z) || !isFinite3(x, y, z))
                return Result<Mesh>::fail("Invalid OBJ normal at " + ctx + ".", "FORMAT", path, "obj.parse");
            normals.push_back({x, y, z});
        } else if (tag == "usemtl") {
            std::string m;
            ls >> m;
            if (!m.empty()) {
                currentMaterial = m;
                haveMaterial = true;
                auto it = matIndexMap.find(currentMaterial);
                if (it == matIndexMap.end()) {
                    matIndex = mesh.materials.size();
                    matIndexMap[currentMaterial] = matIndex;
                    mesh.materials.push_back({currentMaterial, currentMaterial});
                } else {
                    matIndex = it->second;
                }
            }
        } else if (tag == "f") {
            // Face: a/b/c d/e/f g/h/i ... (1-based, negative = relative).
            struct Corner {
                std::size_t pos;
                bool hasUv;
                std::size_t uv;
                bool hasNrm;
                std::size_t nrm;
            };
            std::vector<Corner> face;
            std::string tok;
            while (ls >> tok) {
                // Split on '/'.
                std::string parts[3];
                int nparts = 0;
                std::size_t start = 0;
                for (std::size_t i = 0; i <= tok.size() && nparts < 3; ++i) {
                    if (i == tok.size() || tok[i] == '/') {
                        parts[nparts++] = tok.substr(start, i - start);
                        start = i + 1;
                    }
                }
                long vi = 0, ti = 0, ni = 0;
                if (!parseLong(parts[0], vi) || vi == 0)
                    return Result<Mesh>::fail("Invalid OBJ face vertex at " + ctx + ".", "FORMAT", path, "obj.parse");
                bool hasUv = false, hasNrm = false;
                if (nparts >= 2 && !parts[1].empty()) {
                    if (parseLong(parts[1], ti)) hasUv = true;
                }
                if (nparts >= 3 && !parts[2].empty()) {
                    if (parseLong(parts[2], ni)) hasNrm = true;
                }
                Corner c;
                if (!resolveIndex(vi, positions.size(), c.pos, err, ctx))
                    return Result<Mesh>::fail(err, "FORMAT", path, "obj.parse");
                c.hasUv = hasUv;
                c.uv = 0;
                if (hasUv && !resolveIndex(ti, uvs.size(), c.uv, err, ctx))
                    return Result<Mesh>::fail(err, "FORMAT", path, "obj.parse");
                c.hasNrm = hasNrm;
                c.nrm = 0;
                if (hasNrm && !resolveIndex(ni, normals.size(), c.nrm, err, ctx))
                    return Result<Mesh>::fail(err, "FORMAT", path, "obj.parse");
                face.push_back(std::move(c));
            }
            if (face.size() < 3)
                return Result<Mesh>::fail("OBJ face with < 3 vertices at " + ctx + ".", "FORMAT", path, "obj.parse");
            ensureSubmesh(matIndex);
            // Fan-triangulate the polygon.
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                addCorner(face[0].pos, face[0].hasUv, face[0].uv, face[0].hasNrm, face[0].nrm);
                addCorner(face[i].pos, face[i].hasUv, face[i].uv, face[i].hasNrm, face[i].nrm);
                addCorner(face[i + 1].pos, face[i + 1].hasUv, face[i + 1].uv, face[i + 1].hasNrm,
                           face[i + 1].nrm);
            }
            mesh.subMeshes.back().indexCount = mesh.indices.size() - mesh.subMeshes.back().startIndex;
        }
        // o / g / mtllib / s / vp are ignored (no semantic need on the mesh-only
        // import path; o/g could name submeshes in a future wave).
    }

    if (mesh.vertices.empty())
        return Result<Mesh>::fail("OBJ file has no faces.", "FORMAT", path, "obj.parse");
    if (!haveMaterial) {
        // No usemtl declared: single default submesh.
        SubMesh sm;
        sm.name = "submesh_0";
        sm.materialIndex = 0;
        sm.startIndex = 0;
        sm.indexCount = mesh.indices.size();
        mesh.subMeshes.push_back(sm);
        mesh.materials.push_back({"default", "default"});
    }
    computeBounds(mesh);
    // Tangents: OBJ has no tangent channel; computeTangents derives them from
    // UVs (degenerate-UV triangles fall back to (1,0,0,1) inside).
    computeTangents(mesh);
    Logger::instance().debug("Parsed OBJ '" + path + "': " + std::to_string(mesh.vertices.size()) +
                                  " vertices, " + std::to_string(mesh.triangleCount()) + " triangles.",
                              "obj");
    return Result<Mesh>::ok(std::move(mesh));
}

}  // namespace m2rig
