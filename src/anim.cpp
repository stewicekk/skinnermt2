// Pose keyframe animation: capture, sorted keys, quaternion slerp sampling,
// bake to SMD frames via the same per-bone helper. See anim.hpp.
#include "m2rig/anim.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace m2rig {

namespace {

void applyKey(const AnimKey& key, Skeleton& skeleton) {
    const std::size_t n = std::min({key.pos.size(), key.rot.size(), skeleton.bones.size()});
    for (std::size_t i = 0; i < n; ++i) {
        skeleton.bones[i].localPosition = key.pos[i];
        skeleton.bones[i].localRotationEuler = key.rot[i];
    }
}

// Rebuilds the parallel quat cache from rot (Wave 28). Capture fills both
// channels; addKey re-syncs on insert so keys mutated before insert (the
// existing euler tests set rot after capture) still carry matching quats.
// Sampling reads the cache when it matches rot (see quatForBone) and derives
// otherwise, so legacy keys without a quat channel sample identically.
void refreshQuatForKey(AnimKey& key) {
    key.quat.resize(key.rot.size());
    for (std::size_t i = 0; i < key.rot.size(); ++i) key.quat[i] = Quat::fromEulerXyz(key.rot[i]);
}

// Boundary quat for one bone: stored cache when it matches rot, otherwise
// derived from euler. The match check (up to double-cover sign) keeps direct
// rot edits after insert from going stale, while the fast path still reads
// the parallel cache. Missing entries fall back to fromEulerXyz so legacy
// keys without a quat channel sample identically.
Quat quatForBone(const AnimKey& key, std::size_t i) {
    if (i < key.quat.size() && i < key.rot.size()) {
        const Quat stored = key.quat[i];
        const Quat derived = Quat::fromEulerXyz(key.rot[i]);
        const float dot =
            stored.x * derived.x + stored.y * derived.y + stored.z * derived.z + stored.w * derived.w;
        if (std::fabs(dot) > 1.0f - 1e-6f) return stored;
        return derived;
    }
    if (i < key.rot.size()) return Quat::fromEulerXyz(key.rot[i]);
    if (i < key.quat.size()) return key.quat[i];
    return Quat{0, 0, 0, 1};
}

// Single shared per-bone sampler used by BOTH sampleClip (preview) and
// bakeClipFrames (bake) so the two can never drift apart. Positions lerp
// linearly; rotations slerp the boundary quats, convert through a rotation
// matrix, and fold back to XYZ euler for the unchanged rebuild path.
void sampleBone(const AnimKey& a, const AnimKey& b, double t, std::size_t i, Vec3& outPos,
                Vec3& outEuler) {
    const float tf = static_cast<float>(t);
    outPos = a.pos[i] + (b.pos[i] - a.pos[i]) * tf;
    const Quat qa = quatForBone(a, i);
    const Quat qb = quatForBone(b, i);
    const Quat qm = Quat::slerp(qa, qb, tf).normalized();
    outEuler = qm.toMatrix().eulerXyzFromRotation();
}

// Segment lookup via binary search (upper_bound: first key with frame >
// value). Callers clamp frame <= front / >= back first, so the result is an
// interior hi in [1, size). For an exact interior key hit, hi points at the
// NEXT segment with t == 0 (old linear scan pointed at the previous segment
// with t == 1); both name the same key pose, so semantics are preserved.
std::size_t findSegmentHi(const AnimClip& clip, double frame) {
    const auto it =
        std::upper_bound(clip.keys.begin(), clip.keys.end(), frame,
                         [](double value, const AnimKey& key) {
                             return value < static_cast<double>(key.frame);
                         });
    if (it == clip.keys.begin()) return 1;
    if (it == clip.keys.end()) return clip.keys.size() - 1;
    return static_cast<std::size_t>(it - clip.keys.begin());
}

}  // namespace

// --- ANI Export ------------------------------------------------------------

Result<std::string> exportAni(const AnimClip& clip, const Skeleton& skeleton,
                              float fps, const std::string& path) {
    const int lastFrame = clipLastFrame(clip);
    if (lastFrame < 0) {
        return Result<std::string>::fail("Empty animation clip", "EXPORT", path, "ani.export");
    }
    
    AniDocument doc;
    doc.header.magic[0] = 'A'; doc.header.magic[1] = 'N'; doc.header.magic[2] = 'I'; doc.header.magic[3] = ' ';
    doc.header.version = 1;
    doc.header.frameCount = static_cast<uint32_t>(lastFrame + 1);
    doc.header.boneCount = static_cast<uint32_t>(skeleton.bones.size());
    doc.header.fps = fps;
    
    doc.frames.resize(doc.header.frameCount);
    for (uint32_t f = 0; f < doc.header.frameCount; ++f) {
        doc.frames[f].resize(doc.header.boneCount);
        double frameD = static_cast<double>(f);
        Skeleton tempSkel = skeleton;
        if (!sampleClip(clip, tempSkel, frameD)) {
            return Result<std::string>::fail("Failed to sample clip at frame " + std::to_string(f), "EXPORT", path, "ani.export");
        }
        for (std::size_t b = 0; b < skeleton.bones.size(); ++b) {
            doc.frames[f][b].pos = tempSkel.bones[b].localPosition;
            doc.frames[f][b].rot = tempSkel.bones[b].localRotationEuler * kRadToDeg;
            doc.frames[f][b].scale = tempSkel.bones[b].localScale;
        }
    }
    
    // Serialize to binary
    std::ostringstream out(std::ios::binary);
    
    // Write header
    out.write(doc.header.magic, 4);
    out.write(reinterpret_cast<const char*>(&doc.header.version), 4);
    out.write(reinterpret_cast<const char*>(&doc.header.frameCount), 4);
    out.write(reinterpret_cast<const char*>(&doc.header.boneCount), 4);
    out.write(reinterpret_cast<const char*>(&doc.header.fps), 4);
    
    // Write frame data
    for (const auto& frame : doc.frames) {
        for (const auto& key : frame) {
            out.write(reinterpret_cast<const char*>(&key.pos.x), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.pos.y), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.pos.z), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.rot.x), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.rot.y), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.rot.z), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.scale.x), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.scale.y), sizeof(float));
            out.write(reinterpret_cast<const char*>(&key.scale.z), sizeof(float));
        }
    }
    
    return Result<std::string>::ok(out.str());
}

ResultVoid writeAniFile(const std::string& path, const AnimClip& clip,
                        const Skeleton& skeleton, float fps) {
    auto data = exportAni(clip, skeleton, fps, path);
    if (!data) return ResultVoid::fail(data.error());
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return ResultVoid::fail("Cannot open ANI file for writing: " + path, "IO", path, "ani.export");
    }
    out.write(data.value().c_str(), data.value().size());
    return ResultVoid::ok();
}

// --- Existing functions ----------------------------------------------------

AnimKey capturePoseKey(const Skeleton& skeleton, int frame) {
    AnimKey key;
    key.frame = frame < 0 ? 0 : frame;
    key.pos.reserve(skeleton.bones.size());
    key.rot.reserve(skeleton.bones.size());
    for (const auto& b : skeleton.bones) {
        key.pos.push_back(b.localPosition);
        key.rot.push_back(b.localRotationEuler);
    }
    refreshQuatForKey(key);
    return key;
}

void addKey(AnimClip& clip, AnimKey key) {
    refreshQuatForKey(key);
    for (auto& k : clip.keys) {
        if (k.frame == key.frame) {
            k = std::move(key);
            return;
        }
    }
    clip.keys.push_back(std::move(key));
    std::sort(clip.keys.begin(), clip.keys.end(),
              [](const AnimKey& a, const AnimKey& b) { return a.frame < b.frame; });
}

bool removeKeyAt(AnimClip& clip, int frame) {
    const auto it = std::find_if(clip.keys.begin(), clip.keys.end(),
                                 [frame](const AnimKey& k) { return k.frame == frame; });
    if (it == clip.keys.end()) return false;
    clip.keys.erase(it);
    return true;
}

int clipLastFrame(const AnimClip& clip) {
    if (clip.keys.empty()) return -1;
    return clip.keys.back().frame;  // keys kept sorted by addKey
}

bool sampleClip(const AnimClip& clip, Skeleton& skeleton, double frame) {
    if (clip.keys.empty()) return false;
    if (clip.keys.size() == 1 || frame <= clip.keys.front().frame) {
        applyKey(clip.keys.front(), skeleton);
        return rebuildSkeletonRuntime(skeleton).succeeded();
    }
    if (frame >= clip.keys.back().frame) {
        applyKey(clip.keys.back(), skeleton);
        return rebuildSkeletonRuntime(skeleton).succeeded();
    }
    const std::size_t hi = findSegmentHi(clip, frame);
    const AnimKey& a = clip.keys[hi - 1];
    const AnimKey& b = clip.keys[hi];
    const double span = static_cast<double>(b.frame - a.frame);
    const double t = span > 0.0 ? (frame - a.frame) / span : 0.0;
    const std::size_t n = std::min({a.pos.size(), a.rot.size(), b.pos.size(), b.rot.size(),
                                    skeleton.bones.size()});
    for (std::size_t i = 0; i < n; ++i) {
        Vec3 p{};
        Vec3 e{};
        sampleBone(a, b, t, i, p, e);
        skeleton.bones[i].localPosition = p;
        skeleton.bones[i].localRotationEuler = e;
    }
    return rebuildSkeletonRuntime(skeleton).succeeded();
}

std::vector<SmdFrame> bakeClipFrames(const AnimClip& clip) {
    std::vector<SmdFrame> frames;
    const int last = clipLastFrame(clip);
    if (last < 0) return frames;
    frames.reserve(static_cast<std::size_t>(last) + 1);
    for (int f = 0; f <= last; ++f) {
        SmdFrame frame;
        frame.time = f;
        // Same code path as the viewport preview: clamp to the end keys,
        // otherwise run the shared per-bone slerp helper. No exact-copy
        // shortcut for interior keys so baked integers match sampleClip at
        // the same fractional frame bit-for-bit (up to float rounding).
        const double fd = static_cast<double>(f);
        if (clip.keys.size() == 1 || fd <= clip.keys.front().frame) {
            const AnimKey& k = clip.keys.front();
            const std::size_t n = std::min(k.pos.size(), k.rot.size());
            frame.poses.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                frame.poses.push_back(
                    {static_cast<std::uint32_t>(i), k.pos[i], k.rot[i]});
        } else if (fd >= clip.keys.back().frame) {
            const AnimKey& k = clip.keys.back();
            const std::size_t n = std::min(k.pos.size(), k.rot.size());
            frame.poses.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
                frame.poses.push_back(
                    {static_cast<std::uint32_t>(i), k.pos[i], k.rot[i]});
        } else {
            const std::size_t hi = findSegmentHi(clip, fd);
            const AnimKey& a = clip.keys[hi - 1];
            const AnimKey& b = clip.keys[hi];
            const double span = static_cast<double>(b.frame - a.frame);
            const double t = span > 0.0 ? (fd - a.frame) / span : 0.0;
            const std::size_t n =
                std::min({a.pos.size(), a.rot.size(), b.pos.size(), b.rot.size()});
            frame.poses.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                Vec3 p{};
                Vec3 e{};
                sampleBone(a, b, t, i, p, e);
                frame.poses.push_back({static_cast<std::uint32_t>(i), p, e});
            }
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

// --- Wave 28 compression (stored representation only) -----------------------
// bakeClipFrames above stays the lossless reference and is intentionally NOT
// routed through any quantizer.

float quatAngularError(const Quat& a, const Quat& b) {
    const double dot = static_cast<double>(a.x) * static_cast<double>(b.x) +
                       static_cast<double>(a.y) * static_cast<double>(b.y) +
                       static_cast<double>(a.z) * static_cast<double>(b.z) +
                       static_cast<double>(a.w) * static_cast<double>(b.w);
    double ad = dot < 0.0 ? -dot : dot;
    if (ad > 1.0) ad = 1.0;
    return static_cast<float>(2.0 * std::acos(ad));
}

namespace {

// Span acceptance through the shared sampler: every dropped original key
// strictly inside (anchor, cand) must resample within bounds. Positions via
// distance(), rotations via quatAngularError on the sampleBone euler output
// converted back with fromEulerXyz vs the stored quatForBone reference.
// Deterministic: bones ascending, frames ascending, no early reordering.
bool spanWithinBounds(const AnimKey& a, const AnimKey& b, const AnimClip& clip, std::size_t anchor,
                      std::size_t cand, float posBound, float angBound) {
    const double fa = static_cast<double>(a.frame);
    const double fc = static_cast<double>(b.frame);
    const double span = fc - fa;
    for (std::size_t j = anchor + 1; j < cand; ++j) {
        const AnimKey& orig = clip.keys[j];
        const double fj = static_cast<double>(orig.frame);
        const double t = span > 0.0 ? (fj - fa) / span : 0.0;
        const std::size_t n = std::min({a.pos.size(), a.rot.size(), b.pos.size(), b.rot.size(),
                                        orig.pos.size(), orig.rot.size()});
        for (std::size_t i = 0; i < n; ++i) {
            Vec3 sp{};
            Vec3 se{};
            sampleBone(a, b, t, i, sp, se);
            const float pe = distance(sp, orig.pos[i]);
            if (pe > posBound) return false;
            const Quat qs = Quat::fromEulerXyz(se);
            const Quat qo = quatForBone(orig, i);
            if (quatAngularError(qs, qo) > angBound) return false;
        }
    }
    return true;
}

}  // namespace

AnimClip reduceClipKeys(const AnimClip& clip, float maxPosErr, float maxAngErr,
                        AnimCompressStats* stats) {
    AnimCompressStats local;
    local.keysIn = clip.keys.size();
    local.keysOut = clip.keys.size();
    local.maxPosErrMeasured = 0.0f;
    local.maxAngErrMeasured = 0.0f;
    const std::size_t n = clip.keys.size();
    if (n <= 2) {
        if (stats != nullptr) *stats = local;
        return clip;
    }
    const float posBound = maxPosErr < 0.0f ? 0.0f : maxPosErr;
    const float angBound = maxAngErr < 0.0f ? 0.0f : maxAngErr;

    // Greedy forward: from each anchor keep the furthest reachable candidate.
    // Monotonic break is exact here: if anchor->cand fails, any wider span
    // covers the same failing intermediate plus more, so it fails too.
    std::vector<std::size_t> kept;
    kept.reserve(n);
    kept.push_back(0);
    std::size_t anchor = 0;
    while (anchor < n - 1) {
        std::size_t furthest = anchor + 1;
        for (std::size_t cand = anchor + 1; cand < n; ++cand) {
            const AnimKey& a = clip.keys[anchor];
            const AnimKey& b = clip.keys[cand];
            if (!spanWithinBounds(a, b, clip, anchor, cand, posBound, angBound)) break;
            furthest = cand;
        }
        kept.push_back(furthest);
        anchor = furthest;
    }

    AnimClip out;
    out.keys.reserve(kept.size());
    for (std::size_t idx : kept) out.keys.push_back(clip.keys[idx]);

    // Final measured maxima: resample every dropped original through the
    // reduced clip (same sampleBone path) and take the worst errors.
    float maxPos = 0.0f;
    float maxAng = 0.0f;
    for (std::size_t oi = 0; oi < n; ++oi) {
        bool isKept = false;
        for (std::size_t k : kept) {
            if (k == oi) {
                isKept = true;
                break;
            }
        }
        if (isKept) continue;
        std::size_t lo = kept[0];
        std::size_t hi = kept.back();
        for (std::size_t k = 0; k + 1 < kept.size(); ++k) {
            if (kept[k] < oi && oi < kept[k + 1]) {
                lo = kept[k];
                hi = kept[k + 1];
                break;
            }
        }
        const AnimKey& a = clip.keys[lo];
        const AnimKey& b = clip.keys[hi];
        const AnimKey& orig = clip.keys[oi];
        const double fa = static_cast<double>(a.frame);
        const double fc = static_cast<double>(b.frame);
        const double fj = static_cast<double>(orig.frame);
        const double span = fc - fa;
        const double t = span > 0.0 ? (fj - fa) / span : 0.0;
        const std::size_t nb = std::min({a.pos.size(), a.rot.size(), b.pos.size(), b.rot.size(),
                                         orig.pos.size(), orig.rot.size()});
        for (std::size_t i = 0; i < nb; ++i) {
            Vec3 sp{};
            Vec3 se{};
            sampleBone(a, b, t, i, sp, se);
            const float pe = distance(sp, orig.pos[i]);
            if (pe > maxPos) maxPos = pe;
            const Quat qs = Quat::fromEulerXyz(se);
            const Quat qo = quatForBone(orig, i);
            const float ae = quatAngularError(qs, qo);
            if (ae > maxAng) maxAng = ae;
        }
    }
    local.keysOut = out.keys.size();
    local.maxPosErrMeasured = maxPos;
    local.maxAngErrMeasured = maxAng;
    if (stats != nullptr) *stats = local;
    return out;
}

SmallestThreeQuat quantizeQuatSmallestThree(const Quat& q, int bits) {
    int b = bits;
    if (b < 1) b = 1;
    if (b > 30) b = 30;
    Quat nq = q.normalized();
    if (nq.w < 0.0f) {
        nq.x = -nq.x;
        nq.y = -nq.y;
        nq.z = -nq.z;
        nq.w = -nq.w;
    }
    const float vals[4] = {nq.x, nq.y, nq.z, nq.w};
    int largest = 0;
    for (int i = 1; i < 4; ++i) {
        if (vals[i] > vals[largest]) largest = i;
    }
    const unsigned ub = static_cast<unsigned>(b);
    const std::uint32_t levels = (1u << ub) - 1u;
    SmallestThreeQuat qq;
    qq.largest = static_cast<std::uint8_t>(largest);
    qq.bits = b;
    int dst = 0;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        float v = vals[i];
        if (v < -1.0f) v = -1.0f;
        if (v > 1.0f) v = 1.0f;
        const double norm = (static_cast<double>(v) + 1.0) * 0.5;
        const double scaled = norm * static_cast<double>(levels);
        std::uint32_t code = static_cast<std::uint32_t>(std::floor(scaled + 0.5));
        if (code > levels) code = levels;
        qq.c[static_cast<std::size_t>(dst)] = code;
        ++dst;
    }
    return qq;
}

Quat dequantizeQuatSmallestThree(const SmallestThreeQuat& qq) {
    int b = qq.bits;
    if (b < 1) b = 1;
    if (b > 30) b = 30;
    int largest = static_cast<int>(qq.largest);
    if (largest < 0 || largest > 3) largest = 3;
    const unsigned ub = static_cast<unsigned>(b);
    const std::uint32_t levels = (1u << ub) - 1u;
    float vals[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int src = 0;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        std::uint32_t code = qq.c[static_cast<std::size_t>(src)];
        if (code > levels) code = levels;
        const double norm = static_cast<double>(code) / static_cast<double>(levels);
        vals[i] = static_cast<float>(norm * 2.0 - 1.0);
        ++src;
    }
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) continue;
        sum += static_cast<double>(vals[i]) * static_cast<double>(vals[i]);
    }
    double rem = 1.0 - sum;
    if (rem < 0.0) rem = 0.0;
    vals[largest] = static_cast<float>(std::sqrt(rem));
    Quat q{vals[0], vals[1], vals[2], vals[3]};
    q = q.normalized();
    if (q.w < 0.0f) {
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
        q.w = -q.w;
    }
    return q;
}

QuantizedPosTrack quantizePosTrack(const std::vector<Vec3>& positions, int bits) {
    int b = bits;
    if (b < 1) b = 1;
    if (b > 30) b = 30;
    QuantizedPosTrack track;
    track.bits = b;
    if (positions.empty()) {
        track.min = Vec3{0.0f, 0.0f, 0.0f};
        track.max = Vec3{0.0f, 0.0f, 0.0f};
        return track;
    }
    Vec3 mn = positions[0];
    Vec3 mx = positions[0];
    for (std::size_t i = 1; i < positions.size(); ++i) {
        if (positions[i].x < mn.x) mn.x = positions[i].x;
        if (positions[i].y < mn.y) mn.y = positions[i].y;
        if (positions[i].z < mn.z) mn.z = positions[i].z;
        if (positions[i].x > mx.x) mx.x = positions[i].x;
        if (positions[i].y > mx.y) mx.y = positions[i].y;
        if (positions[i].z > mx.z) mx.z = positions[i].z;
    }
    track.min = mn;
    track.max = mx;
    const unsigned ub = static_cast<unsigned>(b);
    const std::uint32_t levels = (1u << ub) - 1u;
    track.codes.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3& p = positions[i];
        const float vs[3] = {p.x, p.y, p.z};
        const float mns[3] = {mn.x, mn.y, mn.z};
        const float mxs[3] = {mx.x, mx.y, mx.z};
        std::array<std::uint32_t, 3> code = {0u, 0u, 0u};
        for (int a = 0; a < 3; ++a) {
            const float range = mxs[a] - mns[a];
            if (range < 1e-9f) {
                code[static_cast<std::size_t>(a)] = 0u;
            } else {
                double norm =
                    (static_cast<double>(vs[a]) - static_cast<double>(mns[a])) /
                    static_cast<double>(range);
                if (norm < 0.0) norm = 0.0;
                if (norm > 1.0) norm = 1.0;
                std::uint32_t c =
                    static_cast<std::uint32_t>(std::floor(norm * static_cast<double>(levels) + 0.5));
                if (c > levels) c = levels;
                code[static_cast<std::size_t>(a)] = c;
            }
        }
        track.codes.push_back(code);
    }
    return track;
}

std::vector<Vec3> dequantizePosTrack(const QuantizedPosTrack& track) {
    std::vector<Vec3> out;
    out.reserve(track.codes.size());
    int b = track.bits;
    if (b < 1) b = 1;
    if (b > 30) b = 30;
    const unsigned ub = static_cast<unsigned>(b);
    const std::uint32_t levels = (1u << ub) - 1u;
    for (std::size_t i = 0; i < track.codes.size(); ++i) {
        const std::array<std::uint32_t, 3>& code = track.codes[i];
        float vs[3] = {0.0f, 0.0f, 0.0f};
        const float mns[3] = {track.min.x, track.min.y, track.min.z};
        const float mxs[3] = {track.max.x, track.max.y, track.max.z};
        for (int a = 0; a < 3; ++a) {
            const float range = mxs[a] - mns[a];
            if (range < 1e-9f || levels == 0u) {
                vs[a] = mns[a];
            } else {
                std::uint32_t c = code[static_cast<std::size_t>(a)];
                if (c > levels) c = levels;
                const double norm = static_cast<double>(c) / static_cast<double>(levels);
                vs[a] = static_cast<float>(static_cast<double>(mns[a]) + norm * range);
            }
        }
        out.push_back(Vec3{vs[0], vs[1], vs[2]});
    }
    return out;
}

}  // namespace m2rig
