// Canonical skeleton: hierarchy build, runtime transforms, validation.
#include "m2rig/skeleton.hpp"

#include <unordered_set>

#include "m2rig/diagnostics.hpp"

namespace m2rig {

const Bone* Skeleton::findByName(const std::string& boneName) const {
    for (const auto& b : bones)
        if (b.name == boneName) return &b;
    return nullptr;
}

Bone* Skeleton::findByName(const std::string& boneName) {
    for (auto& b : bones)
        if (b.name == boneName) return &b;
    return nullptr;
}

const Bone* Skeleton::findById(std::uint32_t id) const {
    return id < bones.size() ? &bones[id] : nullptr;
}

Bone* Skeleton::findById(std::uint32_t id) {
    return id < bones.size() ? &bones[id] : nullptr;
}

Result<Skeleton> buildSkeleton(std::string name, const std::vector<BoneDefinition>& defs) {
    if (defs.empty()) return Result<Skeleton>::fail("Skeleton has no bones.", "SKELETON");
    if (defs.size() > defaultLimits().maxBones) {
        return Result<Skeleton>::fail("Bone count " + std::to_string(defs.size()) +
                                          " exceeds safety limit.",
                                      "SKELETON");
    }
    // Exact names preserved; duplicates rejected (Metin2 tooling relies on names).
    {
        std::unordered_set<std::string> seen;
        for (const auto& d : defs) {
            if (d.name.empty())
                return Result<Skeleton>::fail("Bone with empty name encountered.", "SKELETON");
            if (!seen.insert(d.name).second)
                return Result<Skeleton>::fail("Duplicate bone name '" + d.name + "'.", "SKELETON");
        }
    }
    Skeleton skel;
    skel.name = std::move(name);
    skel.bones.reserve(defs.size());
    for (std::size_t i = 0; i < defs.size(); ++i) {
        const auto& d = defs[i];
        if (d.parentId != kNoParent &&
            (d.parentId < 0 || static_cast<std::size_t>(d.parentId) >= defs.size()))
            return Result<Skeleton>::fail("Bone '" + d.name + "' has invalid parent id " +
                                              std::to_string(d.parentId) + ".",
                                          "SKELETON");
        Bone b;
        b.id = static_cast<std::uint32_t>(i);
        b.name = d.name;
        b.parentId = d.parentId;
        b.localPosition = d.localPosition;
        b.localRotationEuler = d.localRotationEuler;
        b.localScale = d.localScale;
        skel.bones.push_back(std::move(b));
    }
    // Cycle detection via DFS coloring.
    {
        std::vector<char> color(defs.size(), 0);
        for (std::size_t i = 0; i < defs.size(); ++i) {
            std::size_t cur = i;
            std::unordered_set<std::size_t> path;
            while (true) {
                const std::int32_t p = skel.bones[cur].parentId;
                if (p == kNoParent) break;
                if (!path.insert(cur).second)
                    return Result<Skeleton>::fail("Bone hierarchy contains a cycle.", "SKELETON");
                cur = static_cast<std::size_t>(p);
            }
            color[i] = 1;
        }
    }
    std::int32_t root = kNoParent;
    for (auto& b : skel.bones) {
        if (b.parentId == kNoParent) {
            if (root == kNoParent) root = static_cast<std::int32_t>(b.id);
        } else {
            skel.bones[static_cast<std::size_t>(b.parentId)].children.push_back(b.id);
        }
    }
    if (root == kNoParent)
        return Result<Skeleton>::fail("Skeleton has no root bone.", "SKELETON");
    skel.rootBone = root;
    if (auto r = rebuildSkeletonRuntime(skel); !r) {
        return Result<Skeleton>::fail(r.error().message, "SKELETON");
    }
    return Result<Skeleton>::ok(std::move(skel));
}

ResultVoid rebuildSkeletonRuntime(Skeleton& skel) {
    // Parents always have lower ids? Not guaranteed (SMD keeps file order), so
    // iterate to a fixed point instead of assuming topological order.
    for (auto& b : skel.bones) b.globalTransform = Mat4::identity();
    for (std::size_t pass = 0; pass < skel.bones.size() + 1; ++pass) {
        bool changed = false;
        for (auto& b : skel.bones) {
            Mat4 local = Mat4::compose(b.localPosition, b.localRotationEuler, b.localScale);
            Mat4 parent = Mat4::identity();
            if (b.parentId != kNoParent) {
                if (b.parentId < 0 ||
                    static_cast<std::size_t>(b.parentId) >= skel.bones.size())
                    return ResultVoid::fail("Bone '" + b.name + "' has invalid parent id.",
                                            "SKELETON");
                parent = skel.bones[static_cast<std::size_t>(b.parentId)].globalTransform;
            }
            const Mat4 next = parent * local;
            // Cheap change test on translation row.
            const Vec3 a{next.m[3][0], next.m[3][1], next.m[3][2]};
            const Vec3 c{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                         b.globalTransform.m[3][2]};
            if (distance(a, c) > 1e-9f) changed = true;
            b.globalTransform = next;
        }
        if (!changed) break;
    }
    for (auto& b : skel.bones) {
        b.inverseBindTransform = b.globalTransform.inverseRigid();
        if (!b.children.empty()) {
            Vec3 centroid{0, 0, 0};
            for (std::uint32_t c : b.children) {
                const auto& cb = skel.bones[c];
                centroid += Vec3{cb.globalTransform.m[3][0], cb.globalTransform.m[3][1],
                                 cb.globalTransform.m[3][2]};
            }
            centroid = centroid / static_cast<float>(b.children.size());
            const Vec3 self{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                            b.globalTransform.m[3][2]};
            b.length = distance(self, centroid);
        } else {
            b.length = 0.0f;
        }
    }
    return ResultVoid::ok();
}

void validateSkeleton(const Skeleton& skel, const std::string& assetName,
                      ValidationReport& report) {
    const std::string asset = assetName.empty() ? skel.name : assetName;
    if (skel.bones.empty()) {
        report.add("SKEL_EMPTY", ValidationCategory::Skeleton, Severity::Fatal,
                   "Skeleton has no bones.", asset, skel.name, false);
        return;
    }
    if (skel.rootBone == kNoParent ||
        static_cast<std::size_t>(skel.rootBone) >= skel.bones.size()) {
        report.add("SKEL_NO_ROOT", ValidationCategory::Skeleton, Severity::Error,
                   "Skeleton has no valid root bone.", asset, skel.name, false);
    }
    std::size_t badParent = 0, badFloat = 0, badScale = 0;
    for (const auto& b : skel.bones) {
        if (b.parentId != kNoParent &&
            (b.parentId < 0 || static_cast<std::size_t>(b.parentId) >= skel.bones.size()))
            ++badParent;
        const float vals[9] = {b.localPosition.x, b.localPosition.y, b.localPosition.z,
                               b.localRotationEuler.x, b.localRotationEuler.y,
                               b.localRotationEuler.z, b.localScale.x, b.localScale.y,
                               b.localScale.z};
        for (float v : vals)
            if (!isFiniteF(v)) {
                ++badFloat;
                break;
            }
        if (std::fabs(b.localScale.x) < 1e-9f || std::fabs(b.localScale.y) < 1e-9f ||
            std::fabs(b.localScale.z) < 1e-9f)
            ++badScale;
    }
    if (badParent > 0)
        report.add("SKEL_BAD_PARENT", ValidationCategory::Skeleton, Severity::Error,
                   std::to_string(badParent) + " bones have invalid parents.", asset, skel.name,
                   false);
    if (badFloat > 0)
        report.add("SKEL_NON_FINITE", ValidationCategory::Skeleton, Severity::Error,
                   std::to_string(badFloat) + " bones have NaN/inf transforms.", asset, skel.name,
                   true);
    if (badScale > 0)
        report.add("SKEL_ZERO_SCALE", ValidationCategory::Skeleton, Severity::Warning,
                   std::to_string(badScale) + " bones have near-zero scale.", asset, skel.name,
                   true);
    report.add("SKEL_STATS", ValidationCategory::Skeleton, Severity::Info,
               "Skeleton '" + skel.name + "': " + std::to_string(skel.bones.size()) + " bones.",
               asset, skel.name, false);
}

}  // namespace m2rig
