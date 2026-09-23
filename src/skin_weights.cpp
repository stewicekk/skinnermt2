// Weight repair pipeline + quality metrics.
#include "m2rig/skin_weights.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "m2rig/profiles.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/spatial.hpp"

namespace m2rig {

bool isValidInfluence(const BoneInfluence& inf) {
    return inf.bone != kInvalidBone && isFiniteF(inf.weight) && inf.weight > 0.0f;
}

float influenceTotal(const std::vector<BoneInfluence>& infs) {
    double s = 0.0;
    for (const auto& i : infs)
        if (isFiniteF(i.weight) && i.weight > 0.0f) s += i.weight;
    return static_cast<float>(s);
}

void clampRepairVertex(std::vector<BoneInfluence>& infs, RepairStats* stats) {
    std::size_t before = infs.size();
    infs.erase(std::remove_if(infs.begin(), infs.end(),
                              [](const BoneInfluence& i) { return !isValidInfluence(i); }),
               infs.end());
    if (stats && infs.size() != before) stats->invalidRemoved += before - infs.size();
}

void mergeDuplicateBones(std::vector<BoneInfluence>& infs, RepairStats* stats) {
    if (infs.size() < 2) return;
    std::sort(infs.begin(), infs.end(), [](const BoneInfluence& a, const BoneInfluence& b) {
        return a.bone < b.bone;
    });
    std::vector<BoneInfluence> merged;
    merged.reserve(infs.size());
    for (const auto& i : infs) {
        if (!merged.empty() && merged.back().bone == i.bone) {
            merged.back().weight += i.weight;
            if (stats) ++stats->duplicatesMerged;
        } else {
            merged.push_back(i);
        }
    }
    infs = std::move(merged);
}

void sortByWeightDesc(std::vector<BoneInfluence>& infs) {
    std::sort(infs.begin(), infs.end(), [](const BoneInfluence& a, const BoneInfluence& b) {
        if (a.weight != b.weight) return a.weight > b.weight;
        return a.bone < b.bone;  // deterministic tie-break
    });
}

void reduceInfluences(std::vector<BoneInfluence>& infs, std::size_t maxCount,
                      RepairStats* stats) {
    if (infs.size() <= maxCount) return;
    sortByWeightDesc(infs);
    double dropped = 0.0;
    for (std::size_t i = maxCount; i < infs.size(); ++i) dropped += infs[i].weight;
    infs.resize(maxCount);
    if (stats) {
        stats->influencesDropped += 0;  // counted mesh-wide below via size delta
        stats->removedMass += dropped;
        if (dropped > stats->maxRemovedMassPerVertex) stats->maxRemovedMassPerVertex = dropped;
    }
}

void normalizeInfluences(std::vector<BoneInfluence>& infs) {
    const float total = influenceTotal(infs);
    if (total <= 0.0f || !isFiniteF(total)) return;
    if (std::fabs(total - 1.0f) <= kWeightTolerance) return;
    for (auto& i : infs) i.weight /= total;
}

bool repairVertexInfluences(std::vector<BoneInfluence>& infs, std::size_t maxCount,
                            RepairStats* stats) {
    clampRepairVertex(infs, stats);
    mergeDuplicateBones(infs, stats);
    sortByWeightDesc(infs);
    if (infs.size() > maxCount) {
        const std::size_t dropped = infs.size() - maxCount;
        reduceInfluences(infs, maxCount, stats);
        if (stats) stats->influencesDropped += dropped;
    }
    normalizeInfluences(infs);
    clampRepairVertex(infs, stats);  // drop anything normalization pushed to <= 0
    return !infs.empty() && std::fabs(influenceTotal(infs) - 1.0f) <= 1e-3f;
}

RepairStats repairMeshWeights(Mesh& mesh, std::size_t boneCount, std::size_t maxInfluences) {
    RepairStats stats;
    for (auto& v : mesh.vertices) {
        ++stats.verticesVisited;
        // Range check against skeleton first: out-of-range bones are invalid.
        for (auto& inf : v.influences) {
            if (inf.bone >= boneCount) {
                inf.weight = 0.0f;
                inf.bone = kInvalidBone;
            }
        }
        const std::string before = [&] {
            std::string s;
            for (const auto& i : v.influences) {
                s += std::to_string(i.bone) + ":" + std::to_string(i.weight) + ";";
            }
            return s;
        }();
        const bool usable = repairVertexInfluences(v.influences, maxInfluences, &stats);
        (void)usable;
        std::string after;
        for (const auto& i : v.influences) after += std::to_string(i.bone) + ":" + std::to_string(i.weight) + ";";
        if (after != before) ++stats.verticesChanged;
    }
    return stats;
}

void validateMeshWeights(const Mesh& mesh, std::size_t boneCount, const std::string& assetName,
                         ValidationReport& report) {
    const std::string asset = assetName.empty() ? mesh.name : assetName;
    std::size_t unweighted = 0, unnormalized = 0, invalid = 0, overLimit = 0, badBone = 0;
    std::size_t maxInf = 0;
    for (std::size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
        const auto& infs = mesh.vertices[vi].influences;
        maxInf = infs.size() > maxInf ? infs.size() : maxInf;
        if (infs.size() > kMetin2MaxInfluences) ++overLimit;
        double total = 0.0;
        bool hasBad = false;
        for (const auto& i : infs) {
            if (!isFiniteF(i.weight) || i.weight < 0.0f) hasBad = true;
            if (i.bone >= boneCount) ++badBone;
            if (i.weight > 0.0f) total += i.weight;
        }
        if (hasBad) ++invalid;
        if (total <= 1e-9) {
            ++unweighted;
        } else if (std::fabs(total - 1.0) > 1e-2) {
            ++unnormalized;
        }
    }
    if (unweighted > 0)
        report.add("WEIGHTS_UNWEIGHTED", ValidationCategory::Weights, Severity::Error,
                   std::to_string(unweighted) + " vertices have no usable weight.", asset,
                   mesh.name, true);
    if (invalid > 0)
        report.add("WEIGHTS_INVALID", ValidationCategory::Weights, Severity::Error,
                   std::to_string(invalid) + " vertices have NaN/negative weights.", asset,
                   mesh.name, true);
    if (badBone > 0)
        report.add("WEIGHTS_BAD_BONE", ValidationCategory::Weights, Severity::Error,
                   std::to_string(badBone) + " influences reference out-of-range bones.", asset,
                   mesh.name, true);
    if (overLimit > 0)
        report.add("WEIGHTS_OVER_LIMIT", ValidationCategory::Weights, Severity::Error,
                   std::to_string(overLimit) + " vertices exceed " +
                       std::to_string(kMetin2MaxInfluences) + " influences.", asset, mesh.name,
                   true);
    if (unnormalized > 0)
        report.add("WEIGHTS_UNNORMALIZED", ValidationCategory::Weights, Severity::Warning,
                   std::to_string(unnormalized) + " vertices are not normalized (sum != 1).",
                   asset, mesh.name, true);
    report.add("WEIGHTS_STATS", ValidationCategory::Weights, Severity::Info,
               "Max influences/vertex: " + std::to_string(maxInf) + ".", asset, mesh.name, false);
}

WeightQualityMetrics computeWeightQuality(const Mesh& mesh) {
    WeightQualityMetrics m;
    if (mesh.vertices.empty()) return m;
    std::size_t normalized = 0, unweighted = 0, invalid = 0;
    std::size_t totalInf = 0;
    double entropy = 0.0;
    for (const auto& v : mesh.vertices) {
        double total = 0.0;
        bool bad = false;
        for (const auto& i : v.influences) {
            if (!isFiniteF(i.weight) || i.weight < 0.0f) bad = true;
            if (i.weight > 0) total += i.weight;
        }
        if (bad) ++invalid;
        if (total <= 1e-9)
            ++unweighted;
        else if (std::fabs(total - 1.0) <= 1e-2)
            ++normalized;
        totalInf += v.influences.size();
        if (v.influences.size() > m.maxInfluenceCount) m.maxInfluenceCount = v.influences.size();
        if (total > 0) {
            for (const auto& i : v.influences) {
                const double p = i.weight / total;
                if (p > 1e-12) entropy -= p * std::log2(p);
            }
        }
    }
    const double n = static_cast<double>(mesh.vertices.size());
    m.normalizedPct = 100.0 * normalized / n;
    m.unweightedPct = 100.0 * unweighted / n;
    m.invalidCount = static_cast<double>(invalid);
    m.avgInfluenceCount = static_cast<double>(totalInf) / n;
    m.weightEntropy = entropy / n;
    return m;
}

std::string WeightQualityMetrics::toDisplayString() const {
    std::ostringstream out;
    out << "Weight Quality\n";
    out << "Normalized: " << normalizedPct << "%\n";
    out << "Unweighted: " << unweightedPct << "%\n";
    out << "Invalid: " << invalidCount << "\n";
    out << "Max influences: " << maxInfluenceCount << "\n";
    out << "Avg influences: " << avgInfluenceCount << "\n";
    return out.str();
}

// Paint falloff functions.
float evalFalloff(PaintFalloff f, float t) {
    if (t <= 0.0f) return 1.0f;
    if (t >= 1.0f) return 0.0f;
    switch (f) {
        case PaintFalloff::Linear: return 1.0f - t;
        case PaintFalloff::Cos2: {
            const float ang = kPi * t;
            return std::max(0.0f, std::cos(ang) * 0.5f + 0.5f);
        }
        case PaintFalloff::Smoothstep: {
            const float x = 1.0f - t;
            return x * x * (3.0f - 2.0f * x);
        }
    }
    return 1.0f - t;
}

const char* paintFalloffName(PaintFalloff f) {
    switch (f) {
        case PaintFalloff::Linear: return "Linear";
        case PaintFalloff::Cos2: return "Cos2";
        case PaintFalloff::Smoothstep: return "Smoothstep";
    }
    return "Linear";
}

const char* brushOpName(BrushOp op) {
    switch (op) {
        case BrushOp::Add: return "Add";
        case BrushOp::Subtract: return "Subtract";
        case BrushOp::Set: return "Set";
        case BrushOp::Smooth: return "Smooth";
        case BrushOp::Blur: return "Blur";
        case BrushOp::Sharpen: return "Sharpen";
        case BrushOp::Normalize: return "Normalize";
        case BrushOp::Prune: return "Prune";
    }
    return "Add";
}

PaintState gPaintState{};

float weightOfBone(const std::vector<BoneInfluence>& infs, std::uint32_t bone) {
    for (const auto& i : infs)
        if (i.bone == bone) return i.weight;
    return 0.0f;
}

bool paintVertexWeight(std::vector<BoneInfluence>& infs, const Vec3& vertPos,
                       const PaintParams& params, RepairStats* stats) {
    if (params.bone == kInvalidBone || params.radius <= 0.0f || params.strength <= 0.0f)
        return false;
    const float dist = distance(vertPos, params.center);
    if (dist > params.radius) return false;
    const float t = dist / params.radius;
    const float f = evalFalloff(params.falloff, t);
    if (f <= 0.0f) return false;
    const float delta = params.strength * f;

    bool modified = false;
    switch (params.op) {
        case BrushOp::Add:
        case BrushOp::Blur:
        case BrushOp::Smooth: {
            bool found = false;
            for (auto& i : infs) {
                if (i.bone == params.bone) {
                    i.weight += delta;
                    found = true;
                    break;
                }
            }
            if (!found) infs.push_back(BoneInfluence{params.bone, delta});
            modified = true;
            break;
        }
        case BrushOp::Subtract: {
            for (auto& i : infs) {
                if (i.bone == params.bone) {
                    i.weight -= delta;
                    modified = true;
                    break;
                }
            }
            break;
        }
        case BrushOp::Set: {
            for (auto& i : infs) {
                if (i.bone == params.bone) {
                    i.weight = delta;
                    modified = true;
                    break;
                }
            }
            if (!modified) {
                infs.push_back(BoneInfluence{params.bone, delta});
                modified = true;
            }
            break;
        }
        case BrushOp::Sharpen: {
            for (auto& i : infs) {
                if (i.bone == params.bone)
                    i.weight += delta * i.weight;
                else
                    i.weight -= delta * i.weight * 0.25f;
            }
            modified = true;
            break;
        }
        case BrushOp::Normalize:
        case BrushOp::Prune:
            modified = true;  // handled by repair below
            break;
    }
    if (!modified) return false;

    RepairStats local;
    RepairStats* s = stats ? stats : &local;
    const std::size_t beforeCount = infs.size();
    clampRepairVertex(infs, s);
    mergeDuplicateBones(infs, s);
    sortByWeightDesc(infs);
    if (infs.size() > params.maxInfluences) {
        const std::size_t dropped = infs.size() - params.maxInfluences;
        reduceInfluences(infs, params.maxInfluences, s);
        s->influencesDropped += dropped;
    }
    if (params.normalize) normalizeInfluences(infs);
    clampRepairVertex(infs, s);
    (void)beforeCount;
    return true;
}

PaintStrokeStats paintMeshStroke(Mesh& mesh, const PaintParams& params) {
    PaintStrokeStats out;
    RepairStats repair;
    for (auto& v : mesh.vertices) {
        ++out.verticesVisited;
        if (paintVertexWeight(v.influences, v.position, params, &repair)) {
            ++out.verticesAffected;
            out.totalStrengthApplied += params.strength;
        }
    }
    out.droppedMass = repair.removedMass;
    return out;
}

std::vector<std::pair<std::size_t, std::size_t>> findMirrorPairs(const Mesh& mesh, int axis,
                                                                  float tolerance) {
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    const std::size_t n = mesh.vertices.size();
    if (n == 0 || !(tolerance > 0.0f)) return pairs;
    // KD-tree radius search (Wave 26): same greedy rule as the old O(n^2)
    // scan (first min-by-(distance, index) among j > i, unused, inside
    // tolerance), at O(n log n) typical cost. Worst case (everything within
    // tolerance) degrades to the old quadratic — never worse.
    std::vector<Vec3> pts;
    pts.reserve(n);
    for (const auto& v : mesh.vertices) pts.push_back(v.position);
    const KdTree tree(pts);
    auto reflect = [&](const Vec3& a) {
        if (axis == 1) return Vec3{a.x, -a.y, a.z};
        if (axis == 2) return Vec3{a.x, a.y, -a.z};
        return Vec3{-a.x, a.y, a.z};
    };
    std::vector<char> used(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (used[i]) continue;
        const std::vector<KnnHit> hits = tree.queryRadius(reflect(mesh.vertices[i].position),
                                                          tolerance);
        std::size_t best = n;
        for (const auto& h : hits) {
            if (h.index <= i || used[h.index]) continue;
            best = h.index;  // hits arrive sorted by (dist, index): first eligible wins
            break;
        }
        if (best != n) {
            pairs.emplace_back(i, best);
            used[i] = 1;
            used[best] = 1;
        }
    }
    return pairs;
}

std::size_t mirrorMeshWeights(Mesh& mesh, int axis,
                              const std::vector<std::pair<std::size_t, std::size_t>>& pairs,
                              const std::vector<std::uint32_t>& boneMirror) {
    (void)axis;
    std::size_t mirrored = 0;
    auto remap = [&](std::uint32_t b) -> std::uint32_t {
        return b < boneMirror.size() ? boneMirror[b] : b;
    };
    for (const auto& [a, b] : pairs) {
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size()) continue;
        std::vector<BoneInfluence> copy;
        copy.reserve(mesh.vertices[a].influences.size());
        for (const auto& inf : mesh.vertices[a].influences)
            copy.push_back(BoneInfluence{remap(inf.bone), inf.weight});
        mesh.vertices[b].influences = std::move(copy);
        ++mirrored;
    }
    return mirrored;
}

double computeSymmetryError(const Mesh& mesh,
                            const std::vector<std::pair<std::size_t, std::size_t>>& mirrorPairs) {
    if (mirrorPairs.empty()) return -1.0;
    double err = 0.0;
    std::size_t n = 0;
    for (const auto& [a, b] : mirrorPairs) {
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size()) continue;
        const auto& ia = mesh.vertices[a].influences;
        const auto& ib = mesh.vertices[b].influences;
        for (const auto& inf : ia) {
            err += std::fabs(static_cast<double>(inf.weight) -
                             static_cast<double>(weightOfBone(ib, inf.bone)));
            ++n;
        }
    }
    return n == 0 ? -1.0 : err / static_cast<double>(n);
}

std::string WeightTransferStats::toDisplayString() const {
    std::ostringstream out;
    out << "Transfer: " << verticesMapped << "/" << verticesProcessed << " mapped, "
        << verticesUnmapped << " unmapped, avgDist " << avgDistance << ", droppedMass "
        << removedMass;
    return out.str();
}
std::string SelfTrainStats::toDisplayString() const {
    std::ostringstream out;
    out << "Self-train (" << iterations << " iters, cost " << finalCost
        << "): " << finalTransfer.toDisplayString();
    return out.str();
}

std::string AutoRigStats::toDisplayString() const {
    std::ostringstream out;
    out << "Auto-rig: " << verticesBound << " verts, avgDist " << avgDistance << ", maxDist "
        << maxDistance << ", droppedMass " << removedMass;
    return out.str();
}

namespace {

float pointSegmentDistance(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float d2 = dot(ab, ab);
    if (d2 < 1e-12f) return distance(p, a);
    float t = dot(p - a, ab) / d2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return distance(p, a + ab * t);
}

Vec3 jointPosition(const Skeleton& skel, std::uint32_t bone) {
    const Bone* b = skel.findById(bone);
    if (!b) return {0, 0, 0};
    return {b->globalTransform.m[3][0], b->globalTransform.m[3][1], b->globalTransform.m[3][2]};
}

}  // namespace

AutoRigStats autoRigMesh(Mesh& mesh, const Skeleton& skeleton, float eps,
                         std::size_t topN, RepairStats* stats) {
    AutoRigStats out;
    if (mesh.vertices.empty() || skeleton.bones.empty()) return out;
    if (topN == 0) topN = 1;
    if (topN > 8) topN = 8;
    if (!(eps > 0.0f)) eps = 0.02f;
    RepairStats local;
    RepairStats& rs = stats ? *stats : local;
    std::vector<std::pair<float, std::uint32_t>> scored;
    scored.reserve(skeleton.bones.size());
    std::vector<BoneInfluence> infs;
    infs.reserve(topN);
    for (auto& v : mesh.vertices) {
        scored.clear();
        for (const auto& b : skeleton.bones) {
            const Vec3 joint = jointPosition(skeleton, b.id);
            // Segment to the children centroid (matches Bone::length); a
            // leaf or the root falls back to a point distance.
            Vec3 tip = joint;
            if (!b.children.empty()) {
                Vec3 centroid{0, 0, 0};
                for (std::uint32_t c : b.children) centroid += jointPosition(skeleton, c);
                tip = centroid / static_cast<float>(b.children.size());
            }
            scored.emplace_back(pointSegmentDistance(v.position, joint, tip), b.id);
        }
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
        infs.clear();
        const std::size_t n = scored.size() < topN ? scored.size() : topN;
        for (std::size_t i = 0; i < n; ++i) {
            const float d = scored[i].first;
            infs.push_back({scored[i].second, 1.0f / ((d + eps) * (d + eps))});
        }
        repairVertexInfluences(infs, kMetin2MaxInfluences, &rs);
        v.influences = std::move(infs);
        out.avgDistance += scored.front().first;
        if (scored.front().first > out.maxDistance) out.maxDistance = scored.front().first;
        ++out.verticesBound;
    }
    if (out.verticesBound > 0) out.avgDistance /= static_cast<double>(out.verticesBound);
    out.removedMass = rs.removedMass;
    return out;
}

WeightTransferStats transferWeightsKDTree(const Mesh& src, Mesh& dst,
                                          const std::vector<std::uint32_t>& boneRemap,
                                          std::size_t kNearest) {
    WeightTransferStats stats;
    if (src.vertices.empty() || dst.vertices.empty()) return stats;
    if (kNearest == 0) kNearest = 1;
    if (kNearest > 8) kNearest = 8;
    auto remap = [&](std::uint32_t b) -> std::uint32_t {
        return b < boneRemap.size() ? boneRemap[b] : b;
    };
    // KD-tree kNN (Wave 26): exact top-k by (dist, index) — bit-identical to
    // the old brute-force scan it replaced (up to 1-ulp boundary ties), with
    // O(n log^2 n) build + O(log n) average queries instead of O(n*m).
    // sqrt(distSq) reproduces distance() bit-for-bit (same dot products).
    std::vector<Vec3> srcPts;
    srcPts.reserve(src.vertices.size());
    for (const auto& sv : src.vertices) srcPts.push_back(sv.position);
    const KdTree tree(srcPts);
    std::vector<std::size_t> idx(kNearest);
    std::vector<float> dists(kNearest);
    for (auto& dv : dst.vertices) {
        ++stats.verticesProcessed;
        for (std::size_t k = 0; k < kNearest; ++k) {
            idx[k] = 0;
            dists[k] = std::numeric_limits<float>::max();
        }
        const std::vector<KnnHit> hits = tree.query(dv.position, kNearest);
        for (std::size_t t = 0; t < hits.size() && t < kNearest; ++t) {
            idx[t] = hits[t].index;
            dists[t] = std::sqrt(hits[t].distSq);
        }
        // Inverse-distance blend of the k nearest.
        double wsum = 0.0;
        for (std::size_t k = 0; k < kNearest; ++k) wsum += 1.0 / (dists[k] + 1e-6);
        std::vector<BoneInfluence> blended;
        for (std::size_t k = 0; k < kNearest; ++k) {
            const double w = (1.0 / (dists[k] + 1e-6)) / wsum;
            for (const auto& inf : src.vertices[idx[k]].influences) {
                const std::uint32_t rb = remap(inf.bone);
                if (rb == kInvalidBone) continue;
                bool found = false;
                for (auto& o : blended) {
                    if (o.bone == rb) {
                        o.weight += static_cast<float>(w * inf.weight);
                        found = true;
                        break;
                    }
                }
                if (!found) blended.push_back(BoneInfluence{rb, static_cast<float>(w * inf.weight)});
            }
        }
        if (blended.empty()) {
            ++stats.verticesUnmapped;
            continue;
        }
        RepairStats repair;
        repairVertexInfluences(blended, kMetin2MaxInfluences, &repair);
        stats.removedMass += repair.removedMass;
        dv.influences = std::move(blended);
        stats.avgDistance += dists[0];
        ++stats.verticesMapped;
    }
    if (stats.verticesMapped > 0) stats.avgDistance /= static_cast<double>(stats.verticesMapped);
    return stats;
}

namespace {

// Donor table: top-k source vertices per destination vertex, computed once.
struct DonorTable {
    std::size_t k = 3;
    std::vector<std::vector<std::size_t>> idx;  // [dst][k]
    std::vector<std::vector<float>> dist;       // [dst][k]
};

DonorTable buildDonorTable(const Mesh& src, const Mesh& dst, std::size_t k) {
    DonorTable t;
    t.k = k;
    t.idx.assign(dst.vertices.size(), std::vector<std::size_t>(k, 0));
    t.dist.assign(dst.vertices.size(), std::vector<float>(k, std::numeric_limits<float>::max()));
    // Same KD-tree exactness contract as transferWeightsKDTree (Wave 26).
    // autoRigMesh deliberately stays brute-force: nearest bone SEGMENTS do
    // not reduce to point queries (a segment can win while its joints lose),
    // and O(V*B) over dozens of bones is negligible in practice.
    std::vector<Vec3> srcPts;
    srcPts.reserve(src.vertices.size());
    for (const auto& sv : src.vertices) srcPts.push_back(sv.position);
    const KdTree tree(srcPts);
    for (std::size_t di = 0; di < dst.vertices.size(); ++di) {
        const std::vector<KnnHit> hits = tree.query(dst.vertices[di].position, k);
        for (std::size_t s = 0; s < hits.size() && s < k; ++s) {
            t.dist[di][s] = std::sqrt(hits[s].distSq);
            t.idx[di][s] = hits[s].index;
        }
    }
    return t;
}

// Blend with a given remap; optionally writes into outMesh (null = evaluate only).
WeightTransferStats blendWithRemap(const Mesh& src, const DonorTable& donors,
                                   const std::vector<std::uint32_t>& remap, Mesh* outMesh) {
    WeightTransferStats stats;
    const std::size_t k = donors.k;
    std::vector<BoneInfluence> blended;
    blended.reserve(8);
    for (std::size_t di = 0; di < donors.idx.size(); ++di) {
        ++stats.verticesProcessed;
        double wsum = 0.0;
        for (std::size_t kk = 0; kk < k; ++kk)
            wsum += 1.0 / (static_cast<double>(donors.dist[di][kk]) + 1e-6);
        blended.clear();
        for (std::size_t kk = 0; kk < k; ++kk) {
            const double w =
                (1.0 / (static_cast<double>(donors.dist[di][kk]) + 1e-6)) / wsum;
            for (const auto& inf : src.vertices[donors.idx[di][kk]].influences) {
                const std::uint32_t rb = inf.bone < remap.size() ? remap[inf.bone] : kInvalidBone;
                if (rb == kInvalidBone || rb == kUnmappedBone) continue;
                bool found = false;
                for (auto& o : blended) {
                    if (o.bone == rb) {
                        o.weight += static_cast<float>(w * inf.weight);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    blended.push_back(BoneInfluence{rb, static_cast<float>(w * inf.weight)});
            }
        }
        if (blended.empty()) {
            ++stats.verticesUnmapped;
            continue;
        }
        RepairStats repair;
        repairVertexInfluences(blended, kMetin2MaxInfluences, &repair);
        stats.removedMass += repair.removedMass;
        if (outMesh) outMesh->vertices[di].influences = blended;
        stats.avgDistance += donors.dist[di][0];
        ++stats.verticesMapped;
    }
    if (stats.verticesMapped > 0) stats.avgDistance /= static_cast<double>(stats.verticesMapped);
    return stats;
}

double transferCost(const WeightTransferStats& st, double symErr, double diag) {
    const double unmappedRate =
        st.verticesProcessed > 0
            ? static_cast<double>(st.verticesUnmapped) / static_cast<double>(st.verticesProcessed)
            : 1.0;
    const double sym = symErr < 0.0 ? 0.5 : symErr;
    const double distN = diag > 1e-9 ? st.avgDistance / diag : 0.0;
    return 0.5 * sym + 0.3 * unmappedRate + 0.15 * distN + 0.05 * st.removedMass;
}

}  // namespace

SelfTrainStats transferWeightsSelfTraining(const Mesh& srcMesh, const Skeleton& srcSkel,
                                           Mesh& dstMesh, const Skeleton& dstSkel,
                                           const SkeletonProfile& profile, std::size_t kNearest,
                                           std::size_t maxIter,
                                           const std::set<std::uint32_t>* lockedDst) {
    SelfTrainStats out;
    if (srcMesh.vertices.empty() || dstMesh.vertices.empty()) return out;
    if (kNearest == 0) kNearest = 1;
    if (kNearest > 8) kNearest = 8;
    if (maxIter == 0) maxIter = 1;
    if (maxIter > 25) maxIter = 25;

    // Init remap: exact canonical match, else invalid. Locked destination
    // bones are never mapped to.
    auto dstLocked = [&](std::uint32_t d) { return lockedDst && lockedDst->count(d) != 0; };
    std::vector<std::uint32_t> remap(srcSkel.bones.size(), kInvalidBone);
    std::unordered_map<std::string, std::uint32_t> dstByCanon;
    for (const auto& b : dstSkel.bones) dstByCanon[canonicalBoneName(profile, b.name)] = b.id;
    for (const auto& b : srcSkel.bones) {
        auto it = dstByCanon.find(canonicalBoneName(profile, b.name));
        if (it != dstByCanon.end() && !dstLocked(it->second)) remap[b.id] = it->second;
    }

    const DonorTable donors = buildDonorTable(srcMesh, dstMesh, kNearest);
    const auto mirrorPairs = findMirrorPairs(dstMesh, 0);
    std::vector<std::uint32_t> dstBoneMirror(dstSkel.bones.size(), kInvalidBone);
    for (const auto& b : dstSkel.bones) {
        const std::string mn = mirrorBoneName(profile, b.name);
        if (const Bone* mb = dstSkel.findByName(mn))
            dstBoneMirror[b.id] = mb->id;
        else
            dstBoneMirror[b.id] = b.id;
    }
    const float diag = std::max(1e-6f, dstMesh.bounds.radius() * 2.0f);

    // Candidate dst bones per src bone: exact, mirror-of-exact, then neighbors
    // by centroid distance (deterministic order).
    std::vector<Vec3> srcCentroid(srcSkel.bones.size(), Vec3{0, 0, 0});
    std::vector<std::size_t> srcSupport(srcSkel.bones.size(), 0);
    for (const auto& v : srcMesh.vertices) {
        for (const auto& inf : v.influences) {
            if (inf.bone >= srcCentroid.size()) continue;
            srcCentroid[inf.bone] += v.position * inf.weight;
            srcSupport[inf.bone] += 1;
        }
    }
    std::vector<Vec3> dstCentroid(dstSkel.bones.size(), Vec3{0, 0, 0});
    for (const auto& b : dstSkel.bones) {
        if (const Bone* fb = dstSkel.findById(b.id))
            dstCentroid[b.id] = {fb->globalTransform.m[3][0], fb->globalTransform.m[3][1],
                                 fb->globalTransform.m[3][2]};
    }

    auto evalCost = [&](const std::vector<std::uint32_t>& rm, WeightTransferStats* st) {
        Mesh tmp = dstMesh;
        WeightTransferStats s = blendWithRemap(srcMesh, donors, rm, &tmp);
        // Mirror-aware symmetry: temporarily remap tmp's mirror side through
        // the dst mirror map for a fair error.
        double sym = computeSymmetryError(tmp, mirrorPairs);
        if (st) *st = s;
        return transferCost(s, sym, static_cast<double>(diag));
    };

    WeightTransferStats curStats;
    double curCost = evalCost(remap, &curStats);
    if (lockedDst && !lockedDst->empty()) {
        // Mappings that became locked are abandoned up front so the
        // coordinate descent starts from a valid baseline.
        for (auto& m : remap)
            if (m != kInvalidBone && dstLocked(m)) m = kInvalidBone;
        curCost = evalCost(remap, &curStats);
    }
    std::size_t iter = 0;
    std::size_t stillCount = 0;
    // Fixed ascending src-bone order => deterministic.
    for (; iter < maxIter; ++iter) {
        bool changed = false;
        for (std::size_t s = 0; s < remap.size(); ++s) {
            // Candidate list: current, exact, mirror, then closest centroids.
            std::vector<std::uint32_t> cands;
            cands.push_back(remap[s]);
            auto it = dstByCanon.find(canonicalBoneName(
                profile, s < srcSkel.bones.size() ? srcSkel.bones[s].name : ""));
            if (it != dstByCanon.end()) cands.push_back(it->second);
            // Mirror candidate: mirror of current mapping.
            if (remap[s] != kInvalidBone && remap[s] < dstBoneMirror.size())
                cands.push_back(dstBoneMirror[remap[s]]);
            // Closest dst centroids (up to 3).
            if (s < srcCentroid.size()) {
                std::vector<std::pair<float, std::uint32_t>> byDist;
                for (const auto& db : dstSkel.bones)
                    byDist.emplace_back(distance(srcCentroid[s], dstCentroid[db.id]), db.id);
                std::sort(byDist.begin(), byDist.end(),
                          [](const auto& a, const auto& b) {
                              return a.first < b.first ||
                                     (a.first == b.first && a.second < b.second);
                          });
                for (std::size_t i = 0; i < byDist.size() && cands.size() < 6; ++i)
                    cands.push_back(byDist[i].second);
            }
            std::sort(cands.begin(), cands.end());
            cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
            // Locked destination bones are never candidates.
            cands.erase(std::remove_if(cands.begin(), cands.end(), dstLocked), cands.end());
            if (remap[s] != kInvalidBone && !dstLocked(remap[s])) cands.push_back(remap[s]);
            double bestCost = curCost;
            std::uint32_t best = remap[s];
            WeightTransferStats bestStats = curStats;
            for (std::uint32_t c : cands) {
                if (c == remap[s]) continue;
                auto trial = remap;
                trial[s] = c;
                WeightTransferStats ts;
                const double tc = evalCost(trial, &ts);
                if (tc < bestCost - 1e-6) {
                    bestCost = tc;
                    best = c;
                    bestStats = ts;
                }
            }
            if (best != remap[s]) {
                remap[s] = best;
                curCost = bestCost;
                curStats = bestStats;
                changed = true;
            }
        }
        if (!changed) {
            ++stillCount;
            if (stillCount >= 2) {
                ++iter;
                break;
            }
        } else {
            stillCount = 0;
        }
        if (curStats.verticesUnmapped == 0 && curCost < 1e-4) {
            ++iter;
            break;
        }
    }

    // Final blend into the real mesh.
    out.finalTransfer = blendWithRemap(srcMesh, donors, remap, &dstMesh);
    out.finalRemap = remap;
    out.iterations = iter;
    out.finalCost = curCost;
    // Confidence per src bone: support x name x distance.
    out.boneConfidence.assign(remap.size(), 0.0f);
    for (std::size_t s = 0; s < remap.size(); ++s) {
        if (remap[s] == kInvalidBone) {
            out.boneConfidence[s] = 0.0f;
            continue;
        }
        const double support =
            srcMesh.vertices.empty()
                ? 0.0
                : static_cast<double>(srcSupport[s]) / static_cast<double>(srcMesh.vertices.size());
        double nameScore = 0.0;
        if (s < srcSkel.bones.size()) {
            const std::string sc = canonicalBoneName(profile, srcSkel.bones[s].name);
            if (remap[s] < dstSkel.bones.size() &&
                canonicalBoneName(profile, dstSkel.bones[remap[s]].name) == sc)
                nameScore = 1.0;
            else
                nameScore = 0.4;
        }
        double distScore = 1.0;
        if (s < srcCentroid.size() && remap[s] < dstCentroid.size())
            distScore = 1.0 / (1.0 + distance(srcCentroid[s], dstCentroid[remap[s]]) /
                                         static_cast<double>(diag));
        out.boneConfidence[s] =
            static_cast<float>(0.45 * support + 0.35 * nameScore + 0.20 * distScore);
    }
    return out;
}

std::vector<Mat4> buildSkinningPalette(const Skeleton& skel,
                                       const std::vector<Mat4>& bindInverse) {
    std::vector<Mat4> palette;
    palette.reserve(skel.bones.size());
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        const Mat4 inv = i < bindInverse.size() ? bindInverse[i] : Mat4::identity();
        palette.push_back(inv * skel.bones[i].globalTransform);
    }
    return palette;
}

std::vector<Mat4> currentBindPalette(const Skeleton& skel) {
    std::vector<Mat4> binds;
    binds.reserve(skel.bones.size());
    for (const auto& b : skel.bones) binds.push_back(b.inverseBindTransform);
    return buildSkinningPalette(skel, binds);
}

Vec3 deformVertex(const Vec3& pos, const std::vector<BoneInfluence>& infs,
                  const std::vector<Mat4>& palette) {
    Vec3 acc{0, 0, 0};
    double wsum = 0.0;
    for (const auto& inf : infs) {
        if (inf.bone >= palette.size() || inf.weight <= 0.0f || !isFiniteF(inf.weight))
            continue;
        acc += palette[inf.bone].transformPoint(pos) * inf.weight;
        wsum += inf.weight;
    }
    if (wsum <= 1e-9) return pos;
    return acc / static_cast<float>(wsum);
}

Vec3 deformNormal(const Vec3& nrm, const std::vector<BoneInfluence>& infs,
                  const std::vector<Mat4>& palette) {
    Vec3 acc{0, 0, 0};
    double wsum = 0.0;
    for (const auto& inf : infs) {
        if (inf.bone >= palette.size() || inf.weight <= 0.0f || !isFiniteF(inf.weight))
            continue;
        acc += palette[inf.bone].transformVector(nrm) * inf.weight;
        wsum += inf.weight;
    }
    if (wsum <= 1e-9) return nrm;
    return normalized(acc / static_cast<float>(wsum));
}

std::size_t floodBone(Mesh& mesh, std::uint32_t bone, std::size_t maxInfluences,
                      RepairStats* stats) {
    std::size_t affected = 0;
    for (auto& v : mesh.vertices) {
        v.influences.clear();
        v.influences.push_back({bone, 1.0f});
        RepairStats local;
        repairVertexInfluences(v.influences, maxInfluences, stats ? stats : &local);
        ++affected;
    }
    return affected;
}

std::size_t pruneBone(Mesh& mesh, std::uint32_t bone, std::size_t maxInfluences,
                      RepairStats* stats) {
    std::size_t carried = 0;
    for (auto& v : mesh.vertices) {
        bool had = false;
        for (const auto& inf : v.influences) {
            if (inf.bone == bone && inf.weight > 0.0f) {
                had = true;
                break;
            }
        }
        if (!had) continue;
        ++carried;
        v.influences.erase(
            std::remove_if(v.influences.begin(), v.influences.end(),
                           [bone](const BoneInfluence& inf) { return inf.bone == bone; }),
            v.influences.end());
        RepairStats local;
        repairVertexInfluences(v.influences, maxInfluences, stats ? stats : &local);
    }
    return carried;
}

// --- DQS (Dual Quaternion Skinning) Implementation --------------------------

std::vector<DualQuat> buildDqsPalette(const Skeleton& skel,
                                      const std::vector<Mat4>& bindInverse) {
    std::vector<DualQuat> palette;
    palette.reserve(skel.bones.size());
    for (std::size_t i = 0; i < skel.bones.size(); ++i) {
        const Mat4 inv = i < bindInverse.size() ? bindInverse[i] : Mat4::identity();
        const Mat4 m = inv * skel.bones[i].globalTransform;
        palette.push_back(DualQuat::fromMatrix(m));
    }
    return palette;
}

Vec3 deformVertexDqs(const Vec3& pos, const std::vector<BoneInfluence>& infs,
                     const std::vector<DualQuat>& palette) {
    DualQuat blended{};
    bool first = true;
    double wsum = 0.0;
    
    for (const auto& inf : infs) {
        if (inf.bone >= palette.size() || inf.weight <= 0.0f || !isFiniteF(inf.weight))
            continue;
        
        const DualQuat& dq = palette[inf.bone];
        // Ensure consistent neighborhood (shortest path on 4D sphere)
        if (!first) {
            float dot = dq.real.x * blended.real.x + dq.real.y * blended.real.y +
                        dq.real.z * blended.real.z + dq.real.w * blended.real.w;
            if (dot < 0.0f) {
                // Use antipodal to ensure shortest path
                const DualQuat neg = {
                    Quat{-dq.real.x, -dq.real.y, -dq.real.z, -dq.real.w},
                    Quat{-dq.dual.x, -dq.dual.y, -dq.dual.z, -dq.dual.w}
                };
                if (first) {
                    blended = scale(neg, inf.weight);
                } else {
                    blended = add(blended, scale(neg, inf.weight));
                }
            } else {
                if (first) {
                    blended = scale(dq, inf.weight);
                } else {
                    blended = add(blended, scale(dq, inf.weight));
                }
            }
        } else {
            blended = scale(dq, inf.weight);
            first = false;
        }
        wsum += inf.weight;
    }
    
    if (wsum <= 1e-9) return pos;
    
    // Normalize the blended dual quaternion
    blended = normalize(blended);
    
    // Apply dual quaternion to position
    // v' = q * v * q* + 2 * (q * dq* - dq * q*).vector
    const Quat& q = blended.real;
    const Quat& dq = blended.dual;
    
    // Quaternion conjugate
    const Quat qc{-q.x, -q.y, -q.z, q.w};
    const Quat dqc{-dq.x, -dq.y, -dq.z, dq.w};
    
    // Position as pure quaternion
    const Quat p{pos.x, pos.y, pos.z, 0.0f};
    
    // q * p * q*
    const Quat qpqc = mul(mul(q, p), qc);
    
    // 2 * (q * dq* - dq * q*).vector
    const Quat qdqc = mul(q, dqc);
    const Quat dqqc = mul(dq, qc);
    const Quat trans = scale(
        Quat{qdqc.x - dqqc.x, qdqc.y - dqqc.y, qdqc.z - dqqc.z, qdqc.w - dqqc.w}, 2.0f);
    
    return Vec3{qpqc.x + trans.x, qpqc.y + trans.y, qpqc.z + trans.z};
}

Vec3 deformNormalDqs(const Vec3& nrm, const std::vector<BoneInfluence>& infs,
                     const std::vector<DualQuat>& palette) {
    DualQuat blended{};
    bool first = true;
    double wsum = 0.0;
    
    for (const auto& inf : infs) {
        if (inf.bone >= palette.size() || inf.weight <= 0.0f || !isFiniteF(inf.weight))
            continue;
        
        const DualQuat& dq = palette[inf.bone];
        if (!first) {
            float dot = dq.real.x * blended.real.x + dq.real.y * blended.real.y +
                        dq.real.z * blended.real.z + dq.real.w * blended.real.w;
            if (dot < 0.0f) {
                const DualQuat neg = {
                    Quat{-dq.real.x, -dq.real.y, -dq.real.z, -dq.real.w},
                    Quat{-dq.dual.x, -dq.dual.y, -dq.dual.z, -dq.dual.w}
                };
                blended = add(blended, scale(neg, inf.weight));
            } else {
                blended = add(blended, scale(dq, inf.weight));
            }
        } else {
            blended = scale(dq, inf.weight);
            first = false;
        }
        wsum += inf.weight;
    }
    
    if (wsum <= 1e-9) return nrm;
    
    blended = normalize(blended);
    
    // For normals, only rotation part matters (dual quaternion real part)
    const Quat& q = blended.real;
    const Quat qc{-q.x, -q.y, -q.z, q.w};
    const Quat p{nrm.x, nrm.y, nrm.z, 0.0f};
    const Quat qpqc = mul(mul(q, p), qc);
    
    return normalized(Vec3{qpqc.x, qpqc.y, qpqc.z});
}

}  // namespace m2rig
