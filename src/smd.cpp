// SMD reader/writer/round-trip implementation.
#include "m2rig/smd.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

#include "m2rig/diagnostics.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/skin_weights.hpp"

namespace m2rig {

namespace {

constexpr double kPosTol = 1e-4;
constexpr double kWeightTol = 1e-4;

bool parseFloat(const char*& p, const char* end, float& out) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p >= end) return false;
    char* stop = nullptr;
    const double v = std::strtod(p, &stop);
    if (stop == p || !std::isfinite(v)) return false;
    if (v > std::numeric_limits<float>::max() || v < -std::numeric_limits<float>::max())
        return false;
    out = static_cast<float>(v);
    p = stop;
    return true;
}

bool parseInt(const char*& p, const char* end, long& out) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p >= end) return false;
    char* stop = nullptr;
    out = std::strtol(p, &stop, 10);
    if (stop == p) return false;
    p = stop;
    return true;
}

std::string trimRight(const std::string& s) {
    std::size_t n = s.size();
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) --n;
    return s.substr(0, n);
}

std::vector<std::string> splitLines(const std::string& text, const SafetyLimits& limits,
                                    std::string& err) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    std::size_t count = 0;
    while (start <= text.size()) {
        if (++count > limits.maxTextLines) {
            err = "SMD has too many lines.";
            return {};
        }
        std::size_t nl = text.find('\n', start);
        std::string line = (nl == std::string::npos) ? text.substr(start)
                                                     : text.substr(start, nl - start);
        if (line.size() > limits.maxTextLine) {
            err = "SMD contains an over-long line.";
            return {};
        }
        lines.push_back(trimRight(line));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// Collects lines of a named section ("nodes", "skeleton", "triangles").
// Returns false when the section is missing.
bool sectionLines(const std::vector<std::string>& lines, const std::string& name,
                  std::size_t& begin, std::size_t& end) {
    begin = end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        if (l == name || (l.size() > name.size() + 1 && l.compare(0, name.size(), name) == 0 &&
                          l[name.size()] == ' ')) {
            begin = i + 1;
            break;
        }
    }
    if (begin == lines.size()) return false;
    for (std::size_t i = begin; i < lines.size(); ++i) {
        if (lines[i] == "end") {
            end = i;
            return true;
        }
    }
    return false;  // unterminated
}

bool parseNodeLine(const std::string& line, SmdBone& out, std::string& err) {
    const char* p = line.c_str();
    const char* end = p + line.size();
    long id = 0, parent = 0;
    if (!parseInt(p, end, id) || id < 0 || id > 100000) {
        err = "Invalid SMD node line: " + line;
        return false;
    }
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p >= end || *p != '"') {
        err = "Invalid SMD node line (missing quoted name): " + line;
        return false;
    }
    ++p;
    std::string name;
    while (p < end && *p != '"') {
        if (*p == '\n' || *p == '\r') break;
        name.push_back(*p++);
    }
    if (p >= end || *p != '"' || name.empty() || !isValidBoneName(name)) {
        err = "Invalid SMD node line (bad name): " + line;
        return false;
    }
    ++p;
    if (!parseInt(p, end, parent) || parent < -1 || parent > 100000) {
        err = "Invalid SMD node line (bad parent): " + line;
        return false;
    }
    out.id = static_cast<std::uint32_t>(id);
    out.name = std::move(name);
    out.parentId = static_cast<std::int32_t>(parent);
    return true;
}

bool parsePoseLine(const std::string& line, SmdBonePose& out) {
    const char* p = line.c_str();
    const char* end = p + line.size();
    long id = 0;
    float v[6] = {};
    if (!parseInt(p, end, id) || id < 0) return false;
    for (int i = 0; i < 6; ++i)
        if (!parseFloat(p, end, v[i])) return false;
    out.boneId = static_cast<std::uint32_t>(id);
    out.position = {v[0], v[1], v[2]};
    out.rotation = {v[3], v[4], v[5]};
    return true;
}

bool parseTriVertexLine(const std::string& line, SmdVertex& out, const SafetyLimits& limits,
                        std::string& err) {
    const char* p = line.c_str();
    const char* end = p + line.size();
    long parent = 0;
    float v[8] = {};
    if (!parseInt(p, end, parent) || parent < 0) {
        err = "Invalid SMD vertex line (bad parent bone): " + line;
        return false;
    }
    for (int i = 0; i < 8; ++i)
        if (!parseFloat(p, end, v[i])) {
            err = "Invalid SMD vertex line (bad position/normal/uv): " + line;
            return false;
        }
    long linkCount = 0;
    if (!parseInt(p, end, linkCount) || linkCount < 0 ||
        linkCount > static_cast<long>(limits.maxInfluencesPerVertex)) {
        err = "Invalid SMD vertex line (bad link count): " + line;
        return false;
    }
    std::vector<SmdLink> links;
    for (long i = 0; i < linkCount; ++i) {
        long bone = 0;
        float w = 0;
        if (!parseInt(p, end, bone) || bone < 0 || !parseFloat(p, end, w)) {
            err = "Invalid SMD vertex line (bad link): " + line;
            return false;
        }
        if (w > 0.0f && isFiniteF(w)) links.push_back({static_cast<std::uint32_t>(bone), w});
    }
    out.parentBone = static_cast<std::uint32_t>(parent);
    out.position = {v[0], v[1], v[2]};
    out.normal = {v[3], v[4], v[5]};
    out.uv = {v[6], v[7]};
    out.links = std::move(links);
    return true;
}

}  // namespace

Result<SmdModel> parseSmd(const std::string& text, const std::string& asset) {
    const SafetyLimits& limits = defaultLimits();
    std::string err;
    if (!checkBytes("SMD", text.size(), limits.maxFileBytes, err))
        return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
    std::vector<std::string> lines = splitLines(text, limits, err);
    if (!err.empty()) return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
    if (lines.empty()) return Result<SmdModel>::fail("SMD file is empty.", "FORMAT", asset,
                                                      "smd.parse");
    {
        // Real-world tolerance: grnreader98 writes "Version 1" (capital V).
        std::string v = lines[0];
        for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (v != "version 1")
            return Result<SmdModel>::fail("SMD must start with 'version 1'.", "FORMAT", asset,
                                          "smd.parse");
    }

    SmdModel model;
    // nodes
    {
        std::size_t b = 0, e = 0;
        if (!sectionLines(lines, "nodes", b, e))
            return Result<SmdModel>::fail("SMD is missing the 'nodes' section.", "FORMAT", asset,
                                          "smd.parse");
        for (std::size_t i = b; i < e; ++i) {
            if (lines[i].empty()) continue;
            SmdBone bone;
            if (!parseNodeLine(lines[i], bone, err))
                return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
            model.bones.push_back(std::move(bone));
        }
        if (model.bones.empty())
            return Result<SmdModel>::fail("SMD 'nodes' section has no bones.", "FORMAT", asset,
                                          "smd.parse");
        if (!checkCount("SMD bones", model.bones.size(), limits.maxBones, err))
            return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
    }
    // skeleton
    {
        std::size_t b = 0, e = 0;
        if (!sectionLines(lines, "skeleton", b, e))
            return Result<SmdModel>::fail("SMD is missing the 'skeleton' section.", "FORMAT",
                                          asset, "smd.parse");
        SmdFrame* current = nullptr;
        for (std::size_t i = b; i < e; ++i) {
            const std::string& l = lines[i];
            if (l.empty()) continue;
            if (l.compare(0, 5, "time ") == 0) {
                long t = 0;
                const char* p = l.c_str() + 5;
                if (!parseInt(p, l.c_str() + l.size(), t) || t < 0)
                    return Result<SmdModel>::fail("Invalid SMD skeleton time: " + l, "FORMAT",
                                                  asset, "smd.parse");
                if (!checkCount("SMD frames", model.frames.size() + 1, limits.maxFrames, err))
                    return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
                model.frames.push_back(SmdFrame{static_cast<int>(t), {}});
                current = &model.frames.back();
                continue;
            }
            SmdBonePose pose;
            if (!parsePoseLine(l, pose))
                return Result<SmdModel>::fail("Invalid SMD skeleton line: " + l, "FORMAT", asset,
                                              "smd.parse");
            if (!current)
                return Result<SmdModel>::fail("SMD skeleton pose before any 'time' block.",
                                              "FORMAT", asset, "smd.parse");
            current->poses.push_back(pose);
        }
        if (model.frames.empty())
            return Result<SmdModel>::fail("SMD 'skeleton' section has no frames.", "FORMAT",
                                          asset, "smd.parse");
    }
    // triangles
    {
        std::size_t b = 0, e = 0;
        if (!sectionLines(lines, "triangles", b, e))
            return Result<SmdModel>::fail("SMD is missing the 'triangles' section.", "FORMAT",
                                          asset, "smd.parse");
        std::string material;
        int remaining = 0;
        SmdTriangle current;
        for (std::size_t i = b; i < e; ++i) {
            const std::string& l = lines[i];
            if (l.empty()) continue;
            if (remaining == 0) {
                material = l;
                if (!isValidMaterialName(material))
                    return Result<SmdModel>::fail("Invalid SMD material name: " + material,
                                                  "FORMAT", asset, "smd.parse");
                if (std::find(model.materials.begin(), model.materials.end(), material) ==
                    model.materials.end())
                    model.materials.push_back(material);
                current = SmdTriangle{};
                current.material = material;
                remaining = 3;
                continue;
            }
            SmdVertex v;
            if (!parseTriVertexLine(l, v, limits, err))
                return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
            current.v[3 - remaining] = std::move(v);
            if (--remaining == 0) {
                if (!checkCount("SMD triangles", model.triangles.size() + 1,
                                limits.maxTriangles, err))
                    return Result<SmdModel>::fail(std::move(err), "FORMAT", asset, "smd.parse");
                model.triangles.push_back(std::move(current));
            }
        }
        if (remaining != 0)
            return Result<SmdModel>::fail("SMD 'triangles' section ends mid-triangle.", "FORMAT",
                                          asset, "smd.parse");
    }
    // Cross-reference validation: every parent/pose/triangle bone id must
    // reference a declared node. Without this, dangling ids silently pass.
    {
        std::unordered_map<std::uint32_t, bool> known;
        for (const auto& bone : model.bones) {
            if (known.count(bone.id))
                return Result<SmdModel>::fail("Duplicate SMD bone id: " + std::to_string(bone.id) + ".",
                                              "FORMAT", asset, "smd.parse");
            known[bone.id] = true;
        }
        for (const auto& bone : model.bones) {
            if (bone.parentId != kNoParent &&
                !known.count(static_cast<std::uint32_t>(bone.parentId)))
                return Result<SmdModel>::fail("SMD bone '" + bone.name + "' references unknown parent " +
                                              std::to_string(bone.parentId) + ".",
                                              "FORMAT", asset, "smd.parse");
        }
        for (const auto& f : model.frames) {
            for (const auto& p : f.poses) {
                if (!known.count(p.boneId))
                    return Result<SmdModel>::fail("SMD skeleton pose references unknown bone " +
                                                  std::to_string(p.boneId) + ".",
                                                  "FORMAT", asset, "smd.parse");
            }
        }
        for (const auto& t : model.triangles) {
            for (const auto& v : t.v) {
                if (!known.count(v.parentBone))
                    return Result<SmdModel>::fail("SMD triangle references unknown bone " +
                                                  std::to_string(v.parentBone) + ".",
                                                  "FORMAT", asset, "smd.parse");
                for (const auto& l : v.links) {
                    if (!known.count(l.bone))
                        return Result<SmdModel>::fail("SMD triangle link references unknown bone " +
                                                      std::to_string(l.bone) + ".",
                                                      "FORMAT", asset, "smd.parse");
                }
            }
        }
    }
    // Bind pose from frame 0.
    {
        std::unordered_map<std::uint32_t, SmdBonePose> bind;
        for (const auto& p : model.frames.front().poses) bind[p.boneId] = p;
        for (auto& bone : model.bones) {
            auto it = bind.find(bone.id);
            if (it != bind.end()) {
                bone.bindPosition = it->second.position;
                bone.bindRotation = it->second.rotation;
            }
        }
    }
    Logger::instance().debug("Parsed SMD '" + asset + "': " +
                                 std::to_string(model.bones.size()) + " bones, " +
                                 std::to_string(model.frames.size()) + " frames, " +
                                 std::to_string(model.triangles.size()) + " triangles.",
                             "smd");
    return Result<SmdModel>::ok(std::move(model));
}

Result<SmdWriteResult> writeSmd(const Mesh& mesh, const Skeleton& skeleton,
                                const std::vector<SmdFrame>& frames) {
    if (skeleton.bones.empty())
        return Result<SmdWriteResult>::fail("Cannot write SMD without a skeleton.", "EXPORT",
                                            mesh.name, "smd.write");
    // Skeleton index -> file bone id (dense, file order == skeleton order).
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(6);
    SmdWriteStats stats;
    stats.trianglesWritten = mesh.triangleCount();

    out << "version 1\n\nnodes\n";
    for (const auto& b : skeleton.bones) out << "  " << b.id << " \"" << b.name << "\" " << b.parentId << "\n";
    out << "end\n\nskeleton\n";
    auto writePoseBlock = [&](int time, const Skeleton& skel) {
        out << "time " << time << "\n";
        for (const auto& b : skel.bones) {
            out << "  " << b.id << " " << (b.localPosition.x + 0.0f) << " " << b.localPosition.y
                << " " << b.localPosition.z << " " << b.localRotationEuler.x << " "
                << b.localRotationEuler.y << " " << b.localRotationEuler.z << "\n";
        }
    };
    if (frames.empty()) {
        writePoseBlock(0, skeleton);
    } else {
        // Frame poses reference file bone ids; apply onto a scratch skeleton.
        for (const auto& f : frames) {
            Skeleton posed = skeleton;
            std::unordered_map<std::uint32_t, std::size_t> byId;
            for (std::size_t i = 0; i < posed.bones.size(); ++i) byId[posed.bones[i].id] = i;
            for (const auto& p : f.poses) {
                auto it = byId.find(p.boneId);
                if (it == byId.end()) continue;
                posed.bones[it->second].localPosition = p.position;
                posed.bones[it->second].localRotationEuler = p.rotation;
            }
            writePoseBlock(f.time, posed);
        }
    }
    out << "end\n\ntriangles\n";
    auto materialName = [&](std::size_t i) -> std::string {
        if (i < mesh.materials.size() && !mesh.materials[i].name.empty())
            return mesh.materials[i].name;
        return "material_" + std::to_string(i) + ".dds";
    };
    // Stable order: iterate triangles, group by material via submeshes when
    // present, else by materialIndex runs.
    struct WorkTri {
        std::uint32_t materialIndex;
        std::uint32_t a, b, c;
    };
    std::vector<WorkTri> work;
    if (!mesh.subMeshes.empty()) {
        for (const auto& sm : mesh.subMeshes)
            for (std::size_t i = sm.startIndex; i + 2 < sm.startIndex + sm.indexCount &&
                                                i + 2 < mesh.indices.size();
                 i += 3)
                work.push_back({sm.materialIndex, mesh.indices[i], mesh.indices[i + 1],
                                mesh.indices[i + 2]});
    } else {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            work.push_back({0, mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]});
    }
    for (const auto& t : work) {
        if (t.a >= mesh.vertices.size() || t.b >= mesh.vertices.size() ||
            t.c >= mesh.vertices.size())
            return Result<SmdWriteResult>::fail("Mesh has out-of-range indices; run repair first.",
                                                "EXPORT", mesh.name, "smd.write");
        out << materialName(t.materialIndex) << "\n";
        const std::uint32_t idx[3] = {t.a, t.b, t.c};
        for (int k = 0; k < 3; ++k) {
            const Vertex& v = mesh.vertices[idx[k]];
            // Strongest-first, max 4, accumulate dropped mass honestly.
            std::vector<BoneInfluence> infs = v.influences;
            sortByWeightDesc(infs);
            double kept = 0.0, total = 0.0;
            for (const auto& in : infs)
                if (isValidInfluence(in)) total += in.weight;
            std::vector<BoneInfluence> links;
            for (const auto& in : infs) {
                if (!isValidInfluence(in)) continue;
                if (links.size() < kMetin2MaxInfluences) {
                    links.push_back(in);
                    kept += in.weight;
                }
            }
            if (infs.size() > links.size()) {
                ++stats.clampedVertices;
                stats.droppedMass += (total - kept);
            }
            double renorm = kept > 0.0 ? kept : 1.0;
            const std::uint32_t parent =
                !links.empty() ? links.front().bone
                               : (!v.influences.empty() ? v.influences.front().bone : 0u);
            out << parent << " " << v.position.x << " " << v.position.y << " " << v.position.z
                << " " << v.normal.x << " " << v.normal.y << " " << v.normal.z << " " << v.uv0.x
                << " " << v.uv0.y << " " << links.size();
            for (const auto& l : links) out << " " << l.bone << " " << (l.weight / renorm);
            out << "\n";
            ++stats.verticesWritten;
        }
    }
    out << "end\n";
    SmdWriteResult result{out.str(), stats};
    return Result<SmdWriteResult>::ok(std::move(result));
}

Result<ConvertedSmd> smdToAsset(const SmdModel& model, const std::string& assetName) {
    // Dense mapping file id -> skeleton index (sorted by file id for stability).
    std::vector<const SmdBone*> ordered;
    for (const auto& b : model.bones) ordered.push_back(&b);
    std::sort(ordered.begin(), ordered.end(),
              [](const SmdBone* a, const SmdBone* b) { return a->id < b->id; });
    std::unordered_map<std::uint32_t, std::uint32_t> idToIndex;
    std::vector<BoneDefinition> defs;
    defs.reserve(ordered.size());
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        idToIndex[ordered[i]->id] = static_cast<std::uint32_t>(i);
        BoneDefinition d;
        d.name = ordered[i]->name;
        d.localPosition = ordered[i]->bindPosition;
        d.localRotationEuler = ordered[i]->bindRotation;
        d.parentId = kNoParent;  // resolved below
        defs.push_back(std::move(d));
    }
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (ordered[i]->parentId == kNoParent) continue;
        auto it = idToIndex.find(static_cast<std::uint32_t>(ordered[i]->parentId));
        if (it == idToIndex.end())
            return Result<ConvertedSmd>::fail("SMD bone '" + ordered[i]->name +
                                                  "' references unknown parent id " +
                                                  std::to_string(ordered[i]->parentId) + ".",
                                              "FORMAT", assetName, "smd.convert");
        defs[i].parentId = static_cast<std::int32_t>(it->second);
    }
    auto skelRes = buildSkeleton(assetName.empty() ? "smd" : assetName, defs);
    if (!skelRes) return Result<ConvertedSmd>::fail(skelRes.error());

    Mesh mesh;
    mesh.name = assetName.empty() ? "smd-model" : assetName;
    for (const auto& m : model.materials) mesh.materials.push_back({m, m});

    auto convertVertex = [&](const SmdVertex& sv) -> Result<Vertex> {
        Vertex v;
        v.position = sv.position;
        v.normal = sv.normal;
        v.uv0 = sv.uv;
        if (sv.links.empty()) {
            auto it = idToIndex.find(sv.parentBone);
            if (it == idToIndex.end())
                return Result<Vertex>::fail("SMD vertex references unknown bone id " +
                                                std::to_string(sv.parentBone) + ".",
                                            "FORMAT", assetName, "smd.convert");
            v.influences.push_back({it->second, 1.0f});
        } else {
            for (const auto& l : sv.links) {
                auto it = idToIndex.find(l.bone);
                if (it == idToIndex.end())
                    return Result<Vertex>::fail("SMD vertex references unknown bone id " +
                                                    std::to_string(l.bone) + ".",
                                                "FORMAT", assetName, "smd.convert");
                v.influences.push_back({it->second, l.weight});
            }
        }
        RepairStats stats;
        repairVertexInfluences(v.influences, kMetin2MaxInfluences, &stats);
        if (v.influences.empty()) {
            auto it = idToIndex.find(sv.parentBone);
            const std::uint32_t fallback = (it != idToIndex.end()) ? it->second : 0u;
            v.influences.push_back({fallback, 1.0f});
        }
        return Result<Vertex>::ok(std::move(v));
    };

    // Indexed dedup: identical (pos/normal/uv/links) soup vertices share one index.
    std::unordered_map<std::string, std::uint32_t> vertIndex;
    auto keyFor = [](const Vertex& v) -> std::string {
        char buf[512];
        std::string key;
        std::snprintf(buf, sizeof(buf), "%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|%.5f|", v.position.x,
                      v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z, v.uv0.x,
                      v.uv0.y);
        key += buf;
        for (const auto& in : v.influences) {
            std::snprintf(buf, sizeof(buf), "%u:%.5f;", in.bone, in.weight);
            key += buf;
        }
        return key;
    };
    std::unordered_map<std::string, std::size_t> matIndex;
    for (std::size_t i = 0; i < model.materials.size(); ++i) matIndex[model.materials[i]] = i;
    struct PendingTri {
        std::size_t mat;
        std::uint32_t idx[3];
    };
    // Single pass: SMD soup -> indexed mesh, preserving exact SMD normals.
    std::unordered_map<std::string, std::uint32_t> vertIndex2;
    std::vector<PendingTri> pending2;
    for (const auto& t : model.triangles) {
        PendingTri pt{};
        auto mIt = matIndex.find(t.material);
        pt.mat = (mIt != matIndex.end()) ? mIt->second : 0;
        for (int k = 0; k < 3; ++k) {
            auto vr = convertVertex(t.v[k]);
            if (!vr) return Result<ConvertedSmd>::fail(vr.error());
            const std::string key = keyFor(vr.value());
            auto it = vertIndex2.find(key);
            if (it == vertIndex2.end()) {
                const std::uint32_t idx = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(std::move(vr.value()));
                vertIndex2[key] = idx;
                pt.idx[k] = idx;
            } else {
                pt.idx[k] = it->second;
            }
        }
        pending2.push_back(pt);
    }
    std::size_t cursor = 0;
    while (cursor < pending2.size()) {
        std::size_t runEnd = cursor + 1;
        while (runEnd < pending2.size() && pending2[runEnd].mat == pending2[cursor].mat) ++runEnd;
        SubMesh sm;
        sm.name = "submesh_" + std::to_string(mesh.subMeshes.size());
        sm.materialIndex = static_cast<std::uint32_t>(pending2[cursor].mat);
        sm.startIndex = mesh.indices.size();
        for (std::size_t i = cursor; i < runEnd; ++i)
            mesh.indices.insert(mesh.indices.end(),
                                {pending2[i].idx[0], pending2[i].idx[1], pending2[i].idx[2]});
        sm.indexCount = mesh.indices.size() - sm.startIndex;
        mesh.subMeshes.push_back(sm);
        cursor = runEnd;
    }
    computeBounds(mesh);
    // Tangents orthogonalize against the file-native normals (never
    // recomputed here: recompute would silently change imported shading).
    // Degenerate-UV triangles fall back to (1,0,0,1) inside computeTangents.
    computeTangents(mesh);

    ConvertedSmd out;
    out.mesh = std::move(mesh);
    out.skeleton = std::move(skelRes.value());
    // Remap frame poses from file bone ids to dense skeleton indices so the
    // writer and the timeline can address bones positionally.
    out.frames = model.frames;
    for (auto& f : out.frames) {
        for (auto& p : f.poses) {
            auto it = idToIndex.find(p.boneId);
            if (it == idToIndex.end())
                return Result<ConvertedSmd>::fail("SMD frame " + std::to_string(f.time) +
                                                      " references unknown bone id " +
                                                      std::to_string(p.boneId) + ".",
                                                  "FORMAT", assetName, "smd.convert");
            p.boneId = it->second;
        }
    }
    return Result<ConvertedSmd>::ok(std::move(out));
}

ResultVoid poseSkeletonFromFrame(Skeleton& skeleton, const std::vector<SmdFrame>& frames,
                                 std::size_t frameIndex) {
    if (frames.empty()) return ResultVoid::fail("No animation frames.", "ANIMATION");
    if (frameIndex >= frames.size())
        return ResultVoid::fail("Frame index out of range.", "ANIMATION");
    std::unordered_map<std::uint32_t, std::size_t> byId;
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) byId[skeleton.bones[i].id] = i;
    for (const auto& p : frames[frameIndex].poses) {
        auto it = byId.find(p.boneId);
        if (it == byId.end()) continue;
        skeleton.bones[it->second].localPosition = p.position;
        skeleton.bones[it->second].localRotationEuler = p.rotation;
    }
    return rebuildSkeletonRuntime(skeleton);
}

std::string SmdRoundTripReport::summary() const {
    if (diffs.empty()) return "identical";
    return std::to_string(diffs.size()) + " difference(s)";
}

Result<SmdRoundTripReport> smdRoundTrip(const std::string& text, const std::string& asset) {
    auto first = parseSmd(text, asset);
    if (!first) return Result<SmdRoundTripReport>::fail(first.error());
    auto conv1 = smdToAsset(first.value(), asset);
    if (!conv1) return Result<SmdRoundTripReport>::fail(conv1.error());
    auto written = writeSmd(conv1.value().mesh, conv1.value().skeleton, conv1.value().frames);
    if (!written) return Result<SmdRoundTripReport>::fail(written.error());
    auto second = parseSmd(written.value().text, asset);
    if (!second) return Result<SmdRoundTripReport>::fail(second.error());
    auto conv2 = smdToAsset(second.value(), asset);
    if (!conv2) return Result<SmdRoundTripReport>::fail(conv2.error());

    SmdRoundTripReport report;
    auto diff = [&](const std::string& what, const std::string& detail) {
        report.diffs.push_back({what, detail});
    };
    const Mesh& a = conv1.value().mesh;
    const Mesh& b = conv2.value().mesh;
    const Skeleton& sa = conv1.value().skeleton;
    const Skeleton& sb = conv2.value().skeleton;
    if (sa.bones.size() != sb.bones.size())
        diff("bone-count", std::to_string(sa.bones.size()) + " vs " +
                               std::to_string(sb.bones.size()));
    const std::size_t nb = std::min(sa.bones.size(), sb.bones.size());
    for (std::size_t i = 0; i < nb; ++i) {
        if (sa.bones[i].name != sb.bones[i].name)
            diff("bone-name", "index " + std::to_string(i) + ": '" + sa.bones[i].name + "' vs '" +
                                  sb.bones[i].name + "'");
        if (sa.bones[i].parentId != sb.bones[i].parentId)
            diff("bone-parent", sa.bones[i].name);
        const Vec3& pa = sa.bones[i].localPosition;
        const Vec3& pb = sb.bones[i].localPosition;
        if (distance(pa, pb) > kPosTol)
            diff("bone-position", sa.bones[i].name + " drift " + std::to_string(distance(pa, pb)));
        const Vec3& ra = sa.bones[i].localRotationEuler;
        const Vec3& rb = sb.bones[i].localRotationEuler;
        if (distance(ra, rb) > kPosTol) diff("bone-rotation", sa.bones[i].name);
    }
    if (a.vertices.size() != b.vertices.size())
        diff("vertex-count", std::to_string(a.vertices.size()) + " vs " +
                                 std::to_string(b.vertices.size()));
    if (a.triangleCount() != b.triangleCount())
        diff("triangle-count", std::to_string(a.triangleCount()) + " vs " +
                                   std::to_string(b.triangleCount()));
    const std::size_t nv = std::min(a.vertices.size(), b.vertices.size());
    double maxPosErr = 0.0, maxWtErr = 0.0;
    for (std::size_t i = 0; i < nv; ++i) {
        maxPosErr = std::max(maxPosErr, static_cast<double>(distance(a.vertices[i].position,
                                                                     b.vertices[i].position)));
        const auto& ia = a.vertices[i].influences;
        const auto& ib = b.vertices[i].influences;
        if (ia.size() != ib.size()) {
            diff("influence-count", "vertex " + std::to_string(i));
            continue;
        }
        for (std::size_t k = 0; k < ia.size(); ++k) {
            if (ia[k].bone != ib[k].bone) {
                diff("influence-bone", "vertex " + std::to_string(i));
                break;
            }
            maxWtErr = std::max(maxWtErr,
                                static_cast<double>(std::fabs(ia[k].weight - ib[k].weight)));
        }
    }
    if (maxPosErr > kPosTol)
        diff("position-error", "max drift " + std::to_string(maxPosErr));
    if (maxWtErr > kWeightTol)
        diff("weight-error", "max drift " + std::to_string(maxWtErr));
    if (a.materials.size() != b.materials.size())
        diff("material-count", std::to_string(a.materials.size()) + " vs " +
                                   std::to_string(b.materials.size()));
    for (std::size_t i = 0; i < std::min(a.materials.size(), b.materials.size()); ++i)
        if (a.materials[i].name != b.materials[i].name) diff("material-name", a.materials[i].name);
    if (conv1.value().frames.size() != conv2.value().frames.size())
        diff("frame-count", std::to_string(conv1.value().frames.size()) + " vs " +
                                std::to_string(conv2.value().frames.size()));
    return Result<SmdRoundTripReport>::ok(std::move(report));
}

Result<std::string> readTextFile(const std::string& path, const std::string& asset) {
    const SafetyLimits& limits = defaultLimits();
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec)
        return Result<std::string>::fail("Cannot stat file '" + path + "': " + ec.message(),
                                         "IO", asset.empty() ? path : asset, "read");
    std::string err;
    if (!checkBytes("file", static_cast<std::uint64_t>(size), limits.maxFileBytes, err))
        return Result<std::string>::fail(std::move(err), "IO", asset.empty() ? path : asset,
                                         "read");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Result<std::string>::fail("Cannot open file '" + path + "' for reading.", "IO",
                                         asset.empty() ? path : asset, "read");
    std::string text;
    text.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(text.data(), static_cast<std::streamsize>(size));
        text.resize(static_cast<std::size_t>(in.gcount()));
    }
    return Result<std::string>::ok(std::move(text));
}

ResultVoid writeTextFile(const std::string& path, const std::string& text,
                         const std::string& asset) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return ResultVoid::fail("Cannot open file '" + path + "' for writing.", "IO",
                                asset.empty() ? path : asset, "write");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out)
        return ResultVoid::fail("Failed while writing file '" + path + "'.", "IO",
                                asset.empty() ? path : asset, "write");
    return ResultVoid::ok();
}

}  // namespace m2rig
