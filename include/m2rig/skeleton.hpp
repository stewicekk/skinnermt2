#pragma once
// Canonical skeleton (spec section 7). Exact bone names, hierarchy order and
// transforms are preserved; this module never renames bones.
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "m2rig/math.hpp"
#include "m2rig/result.hpp"
#include "m2rig/validation.hpp"

namespace m2rig {

constexpr std::int32_t kNoParent = -1;

struct Bone {
    std::uint32_t id = 0;  // stable index == position in Skeleton::bones
    std::string name;      // exact, never renamed on import
    std::int32_t parentId = kNoParent;
    std::vector<std::uint32_t> children;
    Vec3 localPosition{0, 0, 0};
    Vec3 localRotationEuler{0, 0, 0};  // radians, XYZ order
    Vec3 localScale{1, 1, 1};
    Mat4 globalTransform = Mat4::identity();
    Mat4 inverseBindTransform = Mat4::identity();
    float length = 0.0f;  // distance to first child centroid, 0 for leaves
    std::unordered_map<std::string, std::string> metadata;
};

struct Skeleton {
    std::string name;
    std::vector<Bone> bones;
    std::int32_t rootBone = kNoParent;
    std::unordered_map<std::string, std::string> metadata;

    const Bone* findByName(const std::string& name) const;
    Bone* findByName(const std::string& name);
    const Bone* findById(std::uint32_t id) const;
    Bone* findById(std::uint32_t id);
};

struct BoneDefinition {
    std::string name;
    std::int32_t parentId = kNoParent;
    Vec3 localPosition{0, 0, 0};
    Vec3 localRotationEuler{0, 0, 0};
    Vec3 localScale{1, 1, 1};
};

// Builds hierarchy, children lists, globals, inverse binds and lengths.
// Rejects duplicate names, bad parents and cycles with explicit errors.
Result<Skeleton> buildSkeleton(std::string name, const std::vector<BoneDefinition>& defs);
ResultVoid rebuildSkeletonRuntime(Skeleton& skeleton);  // globals + binds + lengths only

// --- Viewport gizmo decomposition (row-vector: world = local * parent) ----
// Pure-math core behind the ImGuizmo manipulators (all orientation spaces),
// shared by the GUI and the test harness so the two can never drift apart.
enum class LocalEditOp { Translate = 0, Rotate = 1, Scale = 2 };

struct BoneLocalEdit {
    Vec3 position{0, 0, 0};
    Vec3 rotationEuler{0, 0, 0};  // radians, XYZ order
    Vec3 scale{1, 1, 1};
};

// Recovers the parent-relative local TRS implied by a manipulated WORLD
// matrix for one op. Position always refreshes (drags can nudge it); the
// rotation/scale channels pass through from `current` unless their op is
// active, so per-op drags never clobber untouched channels.
BoneLocalEdit decomposeWorldToLocal(const Mat4& parentGlobal, const Mat4& newWorld,
                                    LocalEditOp op, const BoneLocalEdit& current);

// Draw matrix for parent-space gizmo handles: the parent orientation (rows
// normalized; parent scale/shear is display-only) at the bone's world
// position. Root bones (identity parent) degrade exactly to world space.
Mat4 parentAlignedDrawMatrix(const Mat4& parentGlobal, const Vec3& worldPos);

// Parent-space rotate: the manipulated parent-aligned draw matrix carries a
// world-frame rigid delta (drawNew vs the parent orientation); premultiplying
// the local rotation by it swings the bone about parent axes. Derived purely
// from gizmo OUTPUT matrices, so it is agnostic to ImGuizmo's internal delta
// conventions. Exact for rigid parents; approximate under parent
// scale/shear like every other gizmo path.
Vec3 applyParentRotationDelta(const Mat4& parentGlobal, const Mat4& drawNew,
                              const Vec3& currentEuler);

// Component-wise guarded |new|/|old| row lengths for parent-axis scale
// steps; degenerate axes yield 1.0 (no-op) instead of inf/nan.
Vec3 drawScaleRatios(const Mat4& drawBefore, const Mat4& drawAfter);

// Unified parent-space delta: maps a manipulated parent-aligned draw matrix
// (drawAfter) back to parent-relative local TRS for one op, replacing the
// three ad-hoc per-op branches with a single testable core. drawBefore is
// the pre-drag parent-aligned matrix (parent orientation at the joint);
// drawAfter is the gizmo output. Equivalent to the former per-op paths
// (translate via a moved-world decomposition, rotate via the draw-matrix
// delta, scale via row-length ratios) but expressed through before/after so
// it is agnostic to how the draw matrix was produced.
BoneLocalEdit decomposeParentDelta(const Mat4& parentGlobal, const Mat4& drawBefore,
                                    const Mat4& drawAfter, LocalEditOp op,
                                    const BoneLocalEdit& current);

// Median (component-wise) world position of the selected bones — the bulk
// gizmo pivot. Returns nullopt for an empty set.
std::optional<Vec3> computeSelectionPivot(const Skeleton& skel,
                                         const std::set<std::uint32_t>& ids);

// Apply a world-frame delta (from a bulk gizmo drag) to every bone in the
// selection, each through its own parent frame. Returns the number of bones
// edited. Honors per-bone locks (locked bones are skipped).
std::size_t applyBulkDelta(Skeleton& skel, const std::set<std::uint32_t>& ids,
                           const Mat4& deltaWorld, LocalEditOp op);

void validateSkeleton(const Skeleton& skeleton, const std::string& assetName,
                      ValidationReport& report);

}  // namespace m2rig
