#pragma once
// Canonical skeleton (spec section 7). Exact bone names, hierarchy order and
// transforms are preserved; this module never renames bones.
#include <cstdint>
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

void validateSkeleton(const Skeleton& skeleton, const std::string& assetName,
                      ValidationReport& report);

}  // namespace m2rig
