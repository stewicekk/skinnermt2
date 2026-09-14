#pragma once
// Procedural Metin2-compatible sample armor + reference skeleton.
// Ported from frontend/src/lib/sampleArmor.ts (behavioral reference).
// Used as the default scene, and as a deterministic test fixture.
#include <string>
#include <vector>

#include "m2rig/mesh.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"

namespace m2rig {

std::vector<std::string> sampleBoneNames();
std::vector<BoneDefinition> sampleBoneDefinitions();

// Builds the 24-bone Bip01 reference skeleton (exact names preserved).
Result<Skeleton> makeSampleSkeleton();

struct SampleArmor {
    Mesh mesh;
    Skeleton skeleton;
};

// Procedural symmetric warrior armor: torso + 2 arms + 2 legs + head,
// 4 materials (body/trim/cloth/head), inverse-distance bone weights.
Result<SampleArmor> makeSampleArmor();

// Same procedural armor bound to a race profile (identity must resolve via
// findProfile; geometry is shared, the profile tag + asset id differ).
// Used as transfer source/target templates per character.
Result<SampleArmor> makeSampleArmorForProfile(const std::string& profileId);

}  // namespace m2rig
