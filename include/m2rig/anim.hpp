#pragma once
// Pose keyframe animation (wave 20, Wave 28: quaternion sampling). Keys live
// on integer SMD frame indices and store full local pos+euler snapshots
// (parallel to skeleton bones, by index) plus a parallel unit-quaternion
// cache so baking emits plain SmdFrames the existing writer, timeline and
// deform preview already understand. Interpolation is quaternion slerp for
// rotation (shortest arc, double-cover canonical via dot flip) with positions
// lerped linearly; sampled quats convert back to XYZ euler at the boundary
// via Mat4::eulerXyzFromRotation so rebuildSkeletonRuntime stays unchanged.
// Deterministic: sorted unique keys, pure functions, no hidden state.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"

namespace m2rig {

struct AnimKey {
    int frame = 0;
    std::vector<Vec3> pos;   // localPosition per skeleton bone index
    std::vector<Vec3> rot;   // localRotationEuler (radians, XYZ) per bone index
    std::vector<Quat> quat;  // parallel unit-quat cache of rot via Quat::fromEulerXyz
};

struct AnimClip {
    std::vector<AnimKey> keys;  // sorted by frame, unique frames
};

// --- Metin2 .ani format (binary) -------------------------------------------
// Wave 21: animation export for game engine
// File structure:
//  - Header: "ANI " magic, version, frameCount, boneCount, fps
//  - Frame data: per frame, per bone: pos(3), rot(3), scale(3) as float32

struct AniHeader {
    char magic[4];        // "ANI "
    uint32_t version;     // 1
    uint32_t frameCount;
    uint32_t boneCount;
    float fps;
};

struct AniBoneKey {
    Vec3 pos;
    Vec3 rot;     // Euler degrees
    Vec3 scale;
};

struct AniDocument {
    AniHeader header;
    std::vector<std::vector<AniBoneKey>> frames;  // [frame][bone]
};

// Export animation clip to .ani binary format
Result<std::string> exportAni(const AnimClip& clip, const Skeleton& skeleton,
                              float fps = 30.0f, const std::string& path = "");
// Write .ani file to disk
ResultVoid writeAniFile(const std::string& path, const AnimClip& clip,
                        const Skeleton& skeleton, float fps = 30.0f);

// Captures the skeleton's current local pose as a key at frame.
AnimKey capturePoseKey(const Skeleton& skeleton, int frame);
// Sorted insert; a key on the same frame is REPLACED (re-keying).
void addKey(AnimClip& clip, AnimKey key);
// Erases the key at frame; false when none existed.
bool removeKeyAt(AnimClip& clip, int frame);
// Last key frame, or -1 when the clip is empty.
int clipLastFrame(const AnimClip& clip);
// Samples the clip at fractional frame into skeleton locals (+ rebuild).
// Clamps outside the key range. False (skeleton untouched) when empty.
bool sampleClip(const AnimClip& clip, Skeleton& skeleton, double frame);
// Bakes frames [0..lastKey] to SMD frames (time = index, all bones posed).
// Empty clip => empty vector. Lossless reference: compression below NEVER
// touches this path (stored representation only).
std::vector<SmdFrame> bakeClipFrames(const AnimClip& clip);

// --- Wave 28 compression (stored representation only) -----------------------
// bakeClipFrames stays the lossless reference; the helpers below compress the
// STORED key/quat/pos representation. All helpers are deterministic,
// dependency-free, and key-reduction verifies through sampleBone.
struct AnimCompressStats {
    std::size_t keysIn = 0;
    std::size_t keysOut = 0;
    float maxPosErrMeasured = 0.0f;  // local units, via distance()
    float maxAngErrMeasured = 0.0f;  // radians, via 2*acos(|dot|)
};

// Angular deviation between unit quats in radians: 2*acos(|dot|).
float quatAngularError(const Quat& a, const Quat& b);

// Greedy forward key reduction: endpoints always kept, deterministic order
// (frames/bones ascending). A candidate span anchor->cand is accepted only
// when EVERY dropped original key strictly inside resamples through
// sampleBone within maxPosErr / maxAngErr. Stats report in/out counts plus
// the measured maxima over the final reduced clip (resampled the same way).
AnimClip reduceClipKeys(const AnimClip& clip, float maxPosErr, float maxAngErr,
                        AnimCompressStats* stats = nullptr);

// Smallest-three rotation quantize: canonicalize to w>=0 hemisphere, drop the
// largest-by-value component (2-bit index, always non-negative after the
// w>=0 fold so positive-sqrt reconstruction is exact in sign), uniformly
// quantize the remaining three over [-1,1] with `bits` (default 14).
// Decode reconstructs the dropped component via positive sqrt and returns
// normalized() output (re-canonicalized to w>=0).
struct SmallestThreeQuat {
    std::uint8_t largest = 0;  // 0=x, 1=y, 2=z, 3=w
    int bits = 14;
    std::uint32_t c[3] = {0u, 0u, 0u};
};

SmallestThreeQuat quantizeQuatSmallestThree(const Quat& q, int bits = 14);
Quat dequantizeQuatSmallestThree(const SmallestThreeQuat& qq);

// Position per-track range quantize: component-wise min/max across the track
// (default 16 bits, supported param 12-20, wider 1-30 accepted
// deterministically). Degenerate per-axis range (<1e-9) is a constant:
// code 0 decodes to min exactly (no division).
struct QuantizedPosTrack {
    Vec3 min{};
    Vec3 max{};
    int bits = 16;
    std::vector<std::array<std::uint32_t, 3>> codes;  // one per input, in order
};

QuantizedPosTrack quantizePosTrack(const std::vector<Vec3>& positions, int bits = 16);
std::vector<Vec3> dequantizePosTrack(const QuantizedPosTrack& track);

}  // namespace m2rig
