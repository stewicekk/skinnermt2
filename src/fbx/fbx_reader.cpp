// Native FBX reader on top of OpenFBX (nem0, MIT) + bundled libdeflate.
// Design notes (all verified against src/ofbx.h at pin 4d4a45a):
// - Object names live in Object::name[128]; LIMB_NODE objects are bones.
// - getLocalTranslation/Scaling are raw units; getLocalRotation is degrees.
// - Rotation order is read but our canonical euler is XYZ; game bind poses
//   are ~identity there, so the assumption is validated, not hidden.
// - Cluster indices address geometry control points (positions values
//   array); triangulated vertices resolve through positions.indices.
// - Mesh geometric matrices apply only when non-identity (column-major).
#include "m2rig/fbx/fbx_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "ofbx.h"

#include "m2rig/math.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/coordsys.hpp"

namespace m2rig {

namespace {

constexpr float kDegToRadF = kPi / 180.0f;

bool isIdentityDMatrix(const ofbx::DMatrix& m) {
    for (int i = 0; i < 16; ++i) {
        const double want = (i % 5 == 0) ? 1.0 : 0.0;
        if (std::fabs(m.m[i] - want) > 1e-9) return false;
    }
    return true;
}

// Column-major DMatrix * point.
Vec3 applyDMatrixPoint(const ofbx::DMatrix& m, const Vec3& p) {
    const double x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    const double y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    const double z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

Vec3 applyDMatrixVector(const ofbx::DMatrix& m, const Vec3& v) {
    const double x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z;
    const double y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z;
    const double z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z;
    return normalized({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
}

std::string cleanName(const char* raw) {
    // FBX node names may carry namespace prefixes (Model::Bip01); keep the
    // trailing component so Metin2 profiles match.
    std::string s = raw ? raw : "";
    const std::size_t pos = s.find_last_of(":|");
    if (pos != std::string::npos) s = s.substr(pos + 1);
    return s.empty() ? std::string("unnamed") : s;
}

// Indexed dedup key (Wave-29): exact bits for pos/nrm/uv (no welding
// epsilon) + repaired bone ids + weights quantized to 1e-6. Repair runs
// first (sorted, <=4, normalized) so the key observes canonical influences.
std::uint32_t floatBits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

std::int64_t quantizeWeight(float w) {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(w) * 1000000.0));
}

struct FbxVertexKey {
    std::uint32_t px = 0, py = 0, pz = 0;
    std::uint32_t nx = 0, ny = 0, nz = 0;
    std::uint32_t ux = 0, uy = 0;
    std::uint32_t bones[4] = {kInvalidBone, kInvalidBone, kInvalidBone, kInvalidBone};
    std::int64_t weights[4] = {0, 0, 0, 0};
    std::uint32_t count = 0;
    bool operator==(const FbxVertexKey& o) const {
        if (px != o.px || py != o.py || pz != o.pz) return false;
        if (nx != o.nx || ny != o.ny || nz != o.nz) return false;
        if (ux != o.ux || uy != o.uy || count != o.count) return false;
        for (int i = 0; i < 4; ++i) {
            if (bones[i] != o.bones[i]) return false;
            if (weights[i] != o.weights[i]) return false;
        }
        return true;
    }
};

struct FbxVertexKeyHash {
    std::size_t operator()(const FbxVertexKey& k) const noexcept {
        // FNV-1a 64-bit over the raw fields.
        std::size_t h = static_cast<std::size_t>(1469598103934665603ULL);
        const auto mix = [&](std::uint64_t w) {
            h ^= static_cast<std::size_t>(w);
            h *= static_cast<std::size_t>(1099511628211ULL);
        };
        mix(k.px);
        mix(k.py);
        mix(k.pz);
        mix(k.nx);
        mix(k.ny);
        mix(k.nz);
        mix(k.ux);
        mix(k.uy);
        mix(k.count);
        for (int i = 0; i < 4; ++i) {
            mix(static_cast<std::uint64_t>(k.bones[i]));
            mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(k.weights[i])));
        }
        return h;
    }
};

FbxVertexKey keyForVertex(const Vertex& v) {
    FbxVertexKey k{};
    k.px = floatBits(v.position.x);
    k.py = floatBits(v.position.y);
    k.pz = floatBits(v.position.z);
    k.nx = floatBits(v.normal.x);
    k.ny = floatBits(v.normal.y);
    k.nz = floatBits(v.normal.z);
    k.ux = floatBits(v.uv0.x);
    k.uy = floatBits(v.uv0.y);
    const std::size_t n = v.influences.size() > 4u ? 4u : v.influences.size();
    for (std::size_t i = 0; i < 4u; ++i) {
        if (i < n) {
            k.bones[i] = v.influences[i].bone;
            k.weights[i] = quantizeWeight(v.influences[i].weight);
        } else {
            k.bones[i] = kInvalidBone;
            k.weights[i] = 0;
        }
    }
    k.count = static_cast<std::uint32_t>(v.influences.size());
    return k;
}

}  // namespace

// Implements coordsys.hpp: reads the FBX GlobalSettings axis system from the
// live scene (verified present at OpenFBX pin 4d4a45a: IScene::
// getGlobalSettings + CoordinateAxis enum). Unknown/exotic combos fall back
// to the profile assumed source via nullopt — never a guessed conversion.
std::optional<CoordSys> detectFbxCoordSys(const void* scenePtr) {
    const auto* scene = static_cast<const ofbx::IScene*>(scenePtr);
    if (!scene) return std::nullopt;
    const ofbx::GlobalSettings* gs = scene->getGlobalSettings();
    if (!gs) return std::nullopt;
    const auto toDir = [](ofbx::CoordinateAxis a) {
        using CA = ofbx::CoordinateAxis;
        switch (a) {
            case CA::POSITIVE_X: return AxisDir::PosX;
            case CA::NEGATIVE_X: return AxisDir::NegX;
            case CA::POSITIVE_Y: return AxisDir::PosY;
            case CA::NEGATIVE_Y: return AxisDir::NegY;
            case CA::POSITIVE_Z: return AxisDir::PosZ;
            case CA::NEGATIVE_Z: return AxisDir::NegZ;
            default: return AxisDir::Unknown;
        }
    };
    return coordSysFromUpFront(toDir(gs->UpAxis), toDir(gs->FrontAxis));
}

Result<ConvertedFbx> readFbxFile(const std::string& path, const std::string& assetName) {
    const std::string asset = assetName.empty() ? path : assetName;
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return Result<ConvertedFbx>::fail("Cannot open FBX file: " + path, "IO", asset,
                                          "fbx.read");
    const std::streamsize size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > 512ULL * 1024ULL * 1024ULL)
        return Result<ConvertedFbx>::fail("Suspicious FBX file size.", "FORMAT", asset,
                                          "fbx.read");
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        return Result<ConvertedFbx>::fail("Failed reading FBX file: " + path, "IO", asset,
                                          "fbx.read");

    ofbx::IScene* scene =
        ofbx::load(data.data(), static_cast<ofbx::usize>(data.size()), 0);
    if (!scene) {
        const char* err = ofbx::getError();
        return Result<ConvertedFbx>::fail(
            std::string("OpenFBX parse failed: ") + (err ? err : "unknown"), "FORMAT", asset,
            "fbx.parse");
    }

    ConvertedFbx out;
    // --- skeleton: LIMB_NODE objects in stable id order ---------------------
    struct RawBone {
        const ofbx::Object* obj = nullptr;
        std::string name;
    };
    std::vector<RawBone> rawBones;
    const int objCount = scene->getAllObjectCount();
    const ofbx::Object* const* objs = scene->getAllObjects();
    if (objs) {
        // LIMB_NODEs are bones; NULL_NODEs are included too because game
        // exports (incl. Metin2) use them as attachment/dummy bones.
        for (int i = 0; i < objCount; ++i) {
            const auto t = objs[i]->getType();
            if (t == ofbx::Object::Type::LIMB_NODE || t == ofbx::Object::Type::NULL_NODE)
                rawBones.push_back({objs[i], cleanName(objs[i]->name)});
        }
    }
    std::sort(rawBones.begin(), rawBones.end(), [](const RawBone& a, const RawBone& b) {
        return a.obj->id < b.obj->id;
    });
    std::unordered_map<const ofbx::Object*, std::uint32_t> boneIndex;
    for (std::uint32_t i = 0; i < rawBones.size(); ++i) boneIndex[rawBones[i].obj] = i;

    std::vector<BoneDefinition> defs;
    defs.reserve(rawBones.size());
    for (const auto& rb : rawBones) {
        BoneDefinition d;
        d.name = rb.name;
        // Nearest LIMB_NODE ancestor (or root when none).
        d.parentId = kNoParent;
        for (const ofbx::Object* p = rb.obj->getParent(); p; p = p->getParent()) {
            auto it = boneIndex.find(p);
            if (it != boneIndex.end()) {
                d.parentId = static_cast<std::int32_t>(it->second);
                break;
            }
            if (p->getType() == ofbx::Object::Type::ROOT) break;
        }
        const ofbx::DVec3 t = rb.obj->getLocalTranslation();
        const ofbx::DVec3 r = rb.obj->getLocalRotation();
        const ofbx::DVec3 s = rb.obj->getLocalScaling();
        d.localPosition = {static_cast<float>(t.x), static_cast<float>(t.y),
                           static_cast<float>(t.z)};
        d.localRotationEuler = {static_cast<float>(r.x) * kDegToRadF,
                                static_cast<float>(r.y) * kDegToRadF,
                                static_cast<float>(r.z) * kDegToRadF};
        d.localScale = {static_cast<float>(s.x == 0.0 ? 1.0 : s.x),
                        static_cast<float>(s.y == 0.0 ? 1.0 : s.y),
                        static_cast<float>(s.z == 0.0 ? 1.0 : s.z)};
        if (rb.obj->getRotationOrder() != ofbx::RotationOrder::EULER_XYZ &&
            rb.obj->getRotationOrder() != ofbx::RotationOrder::SPHERIC_XYZ) {
            scene->destroy();
            return Result<ConvertedFbx>::fail(
                "Bone '" + d.name + "' uses a non-XYZ rotation order (unsupported).", "FORMAT",
                asset, "fbx.skeleton");
        }
        defs.push_back(std::move(d));
    }
    if (defs.empty()) {
        scene->destroy();
        return Result<ConvertedFbx>::fail("FBX has no LIMB_NODE skeleton.", "FORMAT", asset,
                                          "fbx.skeleton");
    }
    auto built = buildSkeleton(asset, defs);
    if (!built) {
        scene->destroy();
        return Result<ConvertedFbx>::fail(built.error());
    }
    out.skeleton = std::move(built.value());

    // --- meshes --------------------------------------------------------------
    Mesh mesh;
    mesh.name = asset;
    std::size_t totalTris = 0;
    // Indexed dedup over repaired corners (soup -> indexed). Exact-hash only:
    // positions/normals/uvs compare by bit pattern (no welding epsilon) so
    // the 21/25 model-set gate cannot merge distinct surface points.
    std::unordered_map<FbxVertexKey, std::uint32_t, FbxVertexKeyHash> vertCache;
    vertCache.reserve(8192);
    RepairStats fbxRepair;
    for (int mi = 0; mi < scene->getMeshCount(); ++mi) {
        const ofbx::Mesh* fbxMesh = scene->getMesh(mi);
        if (!fbxMesh) continue;
        const ofbx::Geometry* geomObj = fbxMesh->getGeometry();
        const ofbx::GeometryData& geom = geomObj ? geomObj->getGeometryData()
                                                 : fbxMesh->getGeometryData();
        if (!geom.hasVertices()) continue;
        const ofbx::Vec3Attributes pos = geom.getPositions();
        const ofbx::Vec3Attributes nrm = geom.getNormals();
        const ofbx::Vec2Attributes uv = geom.getUVs(0);
        if (!pos.values || pos.count <= 0) continue;

        // Skin: control-point -> [(bone, weight)].
        std::unordered_map<int, std::vector<std::pair<std::uint32_t, float>>> cpWeights;
        const ofbx::Skin* skin = geomObj ? geomObj->getSkin() : fbxMesh->getSkin();
        if (skin) {
            for (int ci = 0; ci < skin->getClusterCount(); ++ci) {
                const ofbx::Cluster* cl = skin->getCluster(ci);
                if (!cl) continue;
                const ofbx::Object* link = cl->getLink();
                auto bit = link ? boneIndex.find(link) : boneIndex.end();
                if (bit == boneIndex.end()) {
                    std::string linkName = link ? cleanName(link->name) : "<null>";
                    scene->destroy();
                    return Result<ConvertedFbx>::fail(
                        "Skin cluster links unknown bone '" + linkName + "'.", "FORMAT", asset,
                        "fbx.skin");
                }
                const int* idx = cl->getIndices();
                const double* wts = cl->getWeights();
                if (!idx || !wts) continue;
                const int n = std::min(cl->getIndicesCount(), cl->getWeightsCount());
                for (int k = 0; k < n; ++k) {
                    if (idx[k] < 0 || idx[k] >= pos.count || !std::isfinite(wts[k])) {
                        scene->destroy();
                        return Result<ConvertedFbx>::fail(
                            "FBX skin cluster contains an invalid control-point index or weight.",
                            "FORMAT", asset, "fbx.skin");
                    }
                    if (wts[k] > 0.0)
                        cpWeights[idx[k]].emplace_back(bit->second,
                                                       static_cast<float>(wts[k]));
                }
            }
        }

        const ofbx::DMatrix geoM = fbxMesh->getGeometricMatrix();
        const bool useGeo = !isIdentityDMatrix(geoM);
        const int partitions = geom.getPartitionCount();
        for (int pi = 0; pi < partitions; ++pi) {
            const ofbx::GeometryPartition part = geom.getPartition(pi);
            if (!part.polygons || part.polygon_count <= 0 || part.triangles_count <= 0)
                continue;
            std::string matName = "default.dds";
            if (pi < fbxMesh->getMaterialCount()) {
                const ofbx::Material* mat = fbxMesh->getMaterial(pi);
                if (mat) matName = cleanName(mat->name);
            }
            std::uint32_t matIdx = 0;
            bool matFound = false;
            for (std::uint32_t m = 0; m < mesh.materials.size(); ++m) {
                if (mesh.materials[m].name == matName) {
                    matIdx = m;
                    matFound = true;
                    break;
                }
            }
            if (!matFound) {
                MaterialRef ref;
                ref.name = matName;
                ref.texturePath = matName;
                matIdx = static_cast<std::uint32_t>(mesh.materials.size());
                mesh.materials.push_back(std::move(ref));
            }
            SubMesh sm;
            sm.name = "part" + std::to_string(mesh.subMeshes.size());
            sm.materialIndex = matIdx;
            sm.startIndex = mesh.indices.size();
            std::vector<int> tri(
                static_cast<std::size_t>(std::max(3, part.max_polygon_triangles)) * 3u, 0);
            for (int pj = 0; pj < part.polygon_count; ++pj) {
                // Returns the number of INDICES (3 per triangle).
                const ofbx::u32 nidx = ofbx::triangulate(geom, part.polygons[pj], tri.data());
                for (ofbx::u32 t = 0; t + 2 < nidx; t += 3) {
                    std::uint32_t triIdx[3];
                    for (int k = 0; k < 3; ++k) {
                        const int vi = tri[t + static_cast<ofbx::u32>(k)];
                        if (vi < 0 || vi >= pos.count) {
                            scene->destroy();
                            return Result<ConvertedFbx>::fail(
                                "FBX polygon references an invalid control point.", "FORMAT",
                                asset, "fbx.mesh");
                        }
                        Vertex v;
                        ofbx::Vec3 p = pos.get(vi);
                        if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                            !std::isfinite(p.z)) {
                            scene->destroy();
                            return Result<ConvertedFbx>::fail(
                                "FBX mesh contains non-finite vertex coordinates.", "FORMAT",
                                asset, "fbx.mesh");
                        }
                        v.position = {p.x, p.y, p.z};
                        ofbx::Vec3 n = nrm.values ? nrm.get(vi) : ofbx::Vec3{0, 0, 1};
                        if (!std::isfinite(n.x) || !std::isfinite(n.y) ||
                            !std::isfinite(n.z)) {
                            scene->destroy();
                            return Result<ConvertedFbx>::fail(
                                "FBX mesh contains non-finite normals.", "FORMAT", asset,
                                "fbx.mesh");
                        }
                        v.normal = {n.x, n.y, n.z};
                        if (uv.values) {
                            ofbx::Vec2 u = uv.get(vi);
                            if (!std::isfinite(u.x) || !std::isfinite(u.y)) {
                                scene->destroy();
                                return Result<ConvertedFbx>::fail(
                                    "FBX mesh contains non-finite texture coordinates.",
                                    "FORMAT", asset, "fbx.mesh");
                            }
                            v.uv0 = {u.x, u.y};
                        }
                        if (useGeo) {
                            v.position = applyDMatrixPoint(geoM, v.position);
                            v.normal = applyDMatrixVector(geoM, v.normal);
                        }
                        const int cp = pos.indices ? pos.indices[vi] : vi;
                        auto it = cpWeights.find(cp);
                        if (it != cpWeights.end()) {
                            for (const auto& bw : it->second)
                                v.influences.push_back({bw.first, bw.second});
                            RepairStats rs;
                            repairVertexInfluences(v.influences, kMetin2MaxInfluences, &rs);
                            fbxRepair.invalidRemoved += rs.invalidRemoved;
                            fbxRepair.duplicatesMerged += rs.duplicatesMerged;
                            fbxRepair.influencesDropped += rs.influencesDropped;
                            fbxRepair.removedMass += rs.removedMass;
                            if (rs.maxRemovedMassPerVertex > fbxRepair.maxRemovedMassPerVertex)
                                fbxRepair.maxRemovedMassPerVertex = rs.maxRemovedMassPerVertex;
                        }
                        const FbxVertexKey key = keyForVertex(v);
                        const auto hit = vertCache.find(key);
                        if (hit != vertCache.end()) {
                            triIdx[k] = hit->second;
                        } else {
                            triIdx[k] = static_cast<std::uint32_t>(mesh.vertices.size());
                            vertCache.emplace(key, triIdx[k]);
                            mesh.vertices.push_back(std::move(v));
                        }
                    }
                    mesh.indices.push_back(triIdx[0]);
                    mesh.indices.push_back(triIdx[1]);
                    mesh.indices.push_back(triIdx[2]);
                    ++totalTris;
                }
            }
            sm.indexCount = mesh.indices.size() - sm.startIndex;
            if (sm.indexCount > 0) mesh.subMeshes.push_back(std::move(sm));
        }
        ++out.meshCount;
    }
    // Coordinate-system detection runs on the live scene (before destroy).
    const std::optional<CoordSys> detected = detectFbxCoordSys(scene);
    scene->destroy();

    if (mesh.vertices.empty() || totalTris == 0) {
        return Result<ConvertedFbx>::fail("FBX has no usable mesh geometry.", "FORMAT", asset,
                                          "fbx.mesh");
    }

    // Canonical conversion with axis arbitration (trust-but-verify).
    // File headers LIE in the wild (Noesis experiment outputs declare Y-up
    // while carrying Z-up data); the declared space is tried first and the
    // result is diagnosed with diagnoseOrientation. An insane result falls
    // back to the profile assumed source on PRISTINE copies (never by
    // inverting an euler-converted pose). Deterministic + reported.
    const auto& profile = fbxConversionProfile();
    out.detectedSpace = detected;
    std::vector<CoordSys> candidates;
    if (detected.has_value()) candidates.push_back(detected.value());
    if (!detected.has_value() || detected.value() != profile.assumedSource)
        candidates.push_back(profile.assumedSource);
    const Mesh pristineMesh = mesh;
    const Skeleton pristineSkeleton = out.skeleton;
    Mesh winnerMesh;
    Skeleton winnerSkeleton;
    bool haveWinner = false;
    std::size_t bestBlocking = 0;
    std::size_t bestTotal = 0;
    for (CoordSys src : candidates) {
        Mesh tryMesh = pristineMesh;
        Skeleton trySkeleton = pristineSkeleton;
        const std::optional<CoordSys> forced = src;
        std::string convertErr;
        bool convertOk = true;
        if (auto r = applyConversionProfile(profile, forced, tryMesh); !r) {
            convertOk = false;
            convertErr = r.error().message;
        }
        if (convertOk) {
            if (auto r = applyConversionProfile(profile, forced, trySkeleton); !r) {
                convertOk = false;
                convertErr = r.error().message;
            }
        }
        OrientationReport rep;
        if (convertOk) {
            rep = diagnoseOrientation(tryMesh, trySkeleton);
        } else {
            rep.findings.push_back({"ORIENT_CONVERT_FAILED",
                                    "Conversion from " + std::string(coordSysName(src)) +
                                        " failed: " + convertErr,
                                    true});
        }
        std::size_t nb = 0;
        for (const auto& f : rep.findings)
            if (f.blocking) ++nb;
        if (!haveWinner || nb < bestBlocking ||
            (nb == bestBlocking && rep.findings.size() < bestTotal)) {
            haveWinner = true;
            bestBlocking = nb;
            bestTotal = rep.findings.size();
            winnerMesh = std::move(tryMesh);
            winnerSkeleton = std::move(trySkeleton);
            out.appliedSpace = src;
            out.usedFallbackSource = (src != candidates.front());
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                          "FBX axis: declared %s, applied %s%s (%s, %zu blocking).",
                          detected.has_value() ? coordSysName(detected.value()) : "<absent>",
                          coordSysName(src), out.usedFallbackSource ? " [fallback]" : "",
                          rep.verdictLine().c_str(), nb);
            out.conversionNote = buf;
        }
    }
    if (!haveWinner) {
        return Result<ConvertedFbx>::fail("FBX axis arbitration produced no candidate.", "FORMAT",
                                          asset, "fbx.coordsys");
    }
    // Surface the per-corner repair totals (previously discarded via
    // (void)rs): unique verts vs soup corners + dropped influence mass.
    {
        char repairBuf[256];
        std::snprintf(repairBuf, sizeof(repairBuf),
                      " Weights: %zu unique verts (%zu soup corners), repair dropped %zu "
                      "influences (%.6g mass).",
                      mesh.vertices.size(), totalTris * 3u,
                      fbxRepair.invalidRemoved + fbxRepair.duplicatesMerged +
                          fbxRepair.influencesDropped,
                      fbxRepair.removedMass);
        out.conversionNote += repairBuf;
    }
    out.mesh = std::move(winnerMesh);
    out.skeleton = std::move(winnerSkeleton);
    // NOTE: FBX animation frames not currently imported; would apply to frames if present

    computeBounds(out.mesh);
    computeTangents(out.mesh);
    return Result<ConvertedFbx>::ok(std::move(out));
}

}  // namespace m2rig
