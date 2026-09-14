#pragma once
// Weight representation + repair pipeline + paint engine (spec sections 9-10).
// Internal precision is float; export clamps to the target format limit and
// always reports removed influence mass (never silently truncates).
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "m2rig/mesh.hpp"
#include "m2rig/validation.hpp"

namespace m2rig {

struct Skeleton;
struct SkeletonProfile;

constexpr float kWeightTolerance = 1e-4f;
constexpr float kWeightEpsilon = 1e-6f;
constexpr std::size_t kMetin2MaxInfluences = 4;

enum class PaintFalloff { Linear = 0, Cos2 = 1, Smoothstep = 2 };
enum class BrushOp {
    Add = 0,
    Subtract = 1,
    Set = 2,
    Smooth = 3,
    Blur = 4,
    Sharpen = 5,
    Normalize = 6,
    Prune = 7
};

const char* paintFalloffName(PaintFalloff f);
const char* brushOpName(BrushOp op);

struct RepairStats {
    std::size_t verticesVisited = 0;
    std::size_t verticesChanged = 0;
    std::size_t invalidRemoved = 0;    // NaN/inf/negative/zero
    std::size_t duplicatesMerged = 0;  // same bone listed twice
    std::size_t influencesDropped = 0;
    double removedMass = 0.0;  // total weight discarded by reduction
    double maxRemovedMassPerVertex = 0.0;
};

struct WeightQualityMetrics {
    double normalizedPct = 100.0;
    double unweightedPct = 0.0;
    double invalidCount = 0;
    std::size_t maxInfluenceCount = 0;
    double avgInfluenceCount = 0.0;
    double weightEntropy = 0.0;
    double symmetryError = -1.0;  // -1 when not computed
    std::string toDisplayString() const;
};

// Single-vertex primitives (used by paint, transfer and repair alike).
bool isValidInfluence(const BoneInfluence& inf);
float influenceTotal(const std::vector<BoneInfluence>& infs);
float weightOfBone(const std::vector<BoneInfluence>& infs, std::uint32_t bone);
void clampRepairVertex(std::vector<BoneInfluence>& infs, RepairStats* stats = nullptr);
void mergeDuplicateBones(std::vector<BoneInfluence>& infs, RepairStats* stats = nullptr);
void sortByWeightDesc(std::vector<BoneInfluence>& infs);
// Keeps strongest maxCount influences; accumulates discarded mass into stats.
void reduceInfluences(std::vector<BoneInfluence>& infs, std::size_t maxCount,
                      RepairStats* stats = nullptr);
void normalizeInfluences(std::vector<BoneInfluence>& infs);
// Full pipeline: clamp -> drop invalid -> merge dups -> sort -> reduce ->
// normalize -> validate. Returns false when vertex is still unusable.
bool repairVertexInfluences(std::vector<BoneInfluence>& infs, std::size_t maxCount,
                            RepairStats* stats = nullptr);

// Mesh-wide repair with per-vertex bone-index range check against boneCount.
RepairStats repairMeshWeights(Mesh& mesh, std::size_t boneCount,
                              std::size_t maxInfluences = kMetin2MaxInfluences);

void validateMeshWeights(const Mesh& mesh, std::size_t boneCount, const std::string& assetName,
                         ValidationReport& report);

WeightQualityMetrics computeWeightQuality(const Mesh& mesh);
// Symmetry error: mean |w(v,b) - w(mirror(v),mirror(b))| over mirrored pairs.
double computeSymmetryError(const Mesh& mesh,
                            const std::vector<std::pair<std::size_t, std::size_t>>& mirrorPairs);

// --- Paint engine ----------------------------------------------------------
// Falloff curve evaluated at normalized distance t in [0,1] (0=center).
float evalFalloff(PaintFalloff f, float t);

struct PaintParams {
    std::uint32_t bone = kInvalidBone;  // target bone (selected bone)
    Vec3 center{0, 0, 0};               // brush center in mesh local space
    float radius = 0.35f;               // world units; <=0 means no-op
    float strength = 0.6f;              // 0..1 per dab
    PaintFalloff falloff = PaintFalloff::Linear;
    BrushOp op = BrushOp::Add;
    bool normalize = true;
    std::size_t maxInfluences = kMetin2MaxInfluences;
};

// Applies one brush dab to a single vertex. vertPos is the vertex position
// in the same space as params.center. Returns true when modified.
// Never lets the vertex exceed maxInfluences; dropped mass is accumulated.
bool paintVertexWeight(std::vector<BoneInfluence>& infs, const Vec3& vertPos,
                       const PaintParams& params, RepairStats* stats = nullptr);

struct PaintStrokeStats {
    std::size_t verticesVisited = 0;
    std::size_t verticesAffected = 0;
    float totalStrengthApplied = 0.0f;
    double droppedMass = 0.0;
};

// Applies a brush dab over a whole mesh. Returns per-stroke statistics.
PaintStrokeStats paintMeshStroke(Mesh& mesh, const PaintParams& params);

// --- Symmetry --------------------------------------------------------------
void mirrorVertexWeightsX(std::vector<BoneInfluence>& infs);
void mirrorVertexWeightsY(std::vector<BoneInfluence>& infs);
void mirrorVertexWeightsZ(std::vector<BoneInfluence>& infs);
// Mirror whole-mesh weights across an axis plane through origin using a
// vertex mirror-pair table (built by findMirrorPairs) and a bone mirror map.
std::vector<std::pair<std::size_t, std::size_t>> findMirrorPairs(const Mesh& mesh, int axis,
                                                                 float tolerance = 1e-4f);
std::size_t mirrorMeshWeights(Mesh& mesh, int axis,
                              const std::vector<std::pair<std::size_t, std::size_t>>& pairs,
                              const std::vector<std::uint32_t>& boneMirror);

struct PaintStats {
    std::size_t verticesAffected = 0;
    std::size_t totalStrokes = 0;
    float totalStrengthApplied = 0.0f;
};

// Global paint state (accessed by UI panels).
struct PaintState {
    float brushRadius = 0.5f;
    float brushStrength = 1.0f;
    PaintFalloff falloff = PaintFalloff::Linear;
    bool bNormalize = true;
    bool bSymmetryX = false;
    bool bSymmetryY = false;
    bool bSymmetryZ = false;
    std::uint32_t symmetryBone = kInvalidBone;
    PaintStats lastStats{};
};

// Global paint state accessor (defined in skin_weights.cpp).
extern PaintState gPaintState;

// --- Skeletal preview deformation (CPU, render-only) -----------------------
// Palette entry: bindInverse * currentGlobal (row-vector convention, so the
// bind pose deforms to identity). Pass the bind-time inverses explicitly:
// rebuildSkeletonRuntime() recomputes inverseBindTransform from the CURRENT
// locals, so posing would otherwise erase the bind reference. Canonical mesh
// data is never modified; the viewport uploads deformed copies for preview.
std::vector<Mat4> buildSkinningPalette(const Skeleton& skel,
                                       const std::vector<Mat4>& bindInverse);
std::vector<Mat4> currentBindPalette(const Skeleton& skel);
Vec3 deformVertex(const Vec3& pos, const std::vector<BoneInfluence>& infs,
                  const std::vector<Mat4>& palette);
Vec3 deformNormal(const Vec3& nrm, const std::vector<BoneInfluence>& infs,
                  const std::vector<Mat4>& palette);

// --- KD-tree weight transfer ------------------------------------------------
struct WeightTransferStats {
    std::size_t verticesProcessed = 0;
    std::size_t verticesMapped = 0;
    std::size_t verticesUnmapped = 0;
    double avgDistance = 0.0;
    double removedMass = 0.0;
    std::string toDisplayString() const;
};

// Nearest-neighbor weight transfer: for each vertex in dst, copy influences
// from the closest vertex in src (inverse-distance blend over k nearest),
// remap bone ids through boneRemap (kInvalidBone entries fall back to the
// nearest mapped bone), then repair to maxInfluences.
WeightTransferStats transferWeightsKDTree(const Mesh& src, Mesh& dst,
                                          const std::vector<std::uint32_t>& boneRemap,
                                          std::size_t kNearest = 3);

// Deterministic self-training transfer (no ML deps): coordinate descent over
// the src->dst bone remap minimizing
//   J = 0.5*symErr + 0.3*unmappedRate + 0.15*avgDist/D + 0.05*removedMass
// Donor geometry is precomputed once; each evaluation only re-blends.
// Returns the final remap plus per-source-bone confidence scores.
struct SelfTrainStats {
    WeightTransferStats finalTransfer;
    std::vector<std::uint32_t> finalRemap;  // src bone id -> dst bone id
    std::vector<float> boneConfidence;      // per src bone id, 0..1
    std::size_t iterations = 0;
    double finalCost = 0.0;
    std::string toDisplayString() const;
};

SelfTrainStats transferWeightsSelfTraining(const Mesh& srcMesh, const Skeleton& srcSkel,
                                           Mesh& dstMesh, const Skeleton& dstSkel,
                                           const SkeletonProfile& profile,
                                           std::size_t kNearest = 3,
                                           std::size_t maxIter = 12,
                                           const std::set<std::uint32_t>* lockedDst = nullptr);

// Floods one bone over the whole mesh (every vertex becomes rigidly bound
// to it), then repairs. Returns affected vertices. For sockets, debug
// baselines and pre-paint starts; always undoable at the App layer.
std::size_t floodBone(Mesh& mesh, std::uint32_t bone,
                      std::size_t maxInfluences = kMetin2MaxInfluences,
                      RepairStats* stats = nullptr);
// Removes one bone from every vertex, then repairs the remainder.
// Vertices left empty stay empty (reported by WEIGHTS_UNWEIGHTED).
// Returns vertices that carried the bone.
std::size_t pruneBone(Mesh& mesh, std::uint32_t bone,
                      std::size_t maxInfluences = kMetin2MaxInfluences,
                      RepairStats* stats = nullptr);

}  // namespace m2rig
