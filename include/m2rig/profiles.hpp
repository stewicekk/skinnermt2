#pragma once
// Metin2 skeleton profiles: expected bones, aliases, mirror pairs, race
// association (spec section 8). No single hardcoded naming convention:
// matching uses configurable alias + normalization rules.
#include <string>
#include <unordered_map>
#include <vector>

#include "m2rig/skeleton.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/validation.hpp"

namespace m2rig {

struct SkeletonProfile {
    std::string identity;    // e.g. "pc_warrior_m"
    std::string race;        // e.g. "warrior"
    std::string gender;      // "m" / "w" / "any"
    std::vector<std::string> expectedBones;
    std::unordered_map<std::string, std::string> aliases;  // alias -> canonical
    std::unordered_map<std::string, std::string> parentOf;  // canonical -> canonical parent
    std::unordered_map<std::string, std::string> mirrorOf;  // canonical -> mirrored canonical
    std::vector<std::string> allowedMissing;
    std::vector<std::string> forbiddenUnexpected;  // must never appear (e.g. sockets as deform)
    std::vector<std::string> socketBones;          // equip_right/left, stip ...
    // Verified optional bones from real Data/Models FBX bridge dumps
    // (90-bone ninja: fingers, toes, nubs, ponytail chain, armor sockets).
    // Present -> fine, missing -> never reported, unknown others -> Info.
    std::vector<std::string> optionalBones;
};

std::string normalizeBoneName(const std::string& name);  // lower, single spaces, _ -> space
std::string canonicalBoneName(const SkeletonProfile& profile, const std::string& name);
std::string mirrorBoneName(const SkeletonProfile& profile, const std::string& name);

// Built-in Metin2 PC profiles (Bip01 family), ported from
// frontend/src/lib/sampleArmor.ts + ai_backend bone table.
SkeletonProfile warriorProfile();
SkeletonProfile ninjaProfile();
SkeletonProfile suraProfile();
SkeletonProfile shamanProfile();
SkeletonProfile wolfmanProfile();
SkeletonProfile mountProfile();
// Male/female variants (same verified 23-bone core, gender tag differs).
SkeletonProfile warriorMaleProfile();
SkeletonProfile warriorFemaleProfile();
SkeletonProfile ninjaMaleProfile();
SkeletonProfile ninjaFemaleProfile();
SkeletonProfile suraMaleProfile();
SkeletonProfile suraFemaleProfile();
SkeletonProfile shamanMaleProfile();
SkeletonProfile shamanFemaleProfile();
std::vector<SkeletonProfile> allBuiltinProfiles();
const SkeletonProfile* findProfile(const std::string& identityOrRace);

// Validates a skeleton against a profile: missing/unexpected/parent/mirror.
void validateAgainstProfile(const Skeleton& skeleton, const SkeletonProfile& profile,
                            const std::string& assetName, ValidationReport& report);

// Maps source bone indices to target bone indices by canonical name.
// Unmapped targets resolve to kUnmappedBone.
constexpr std::uint32_t kUnmappedBone = 0xFFFFFFFEu;
std::vector<std::uint32_t> mapBonesByProfile(const Skeleton& source, const Skeleton& target,
                                             const SkeletonProfile& profile);

// Warns when socket bones (equip_*/stip) carry deform weights above epsilon.
// Sockets are attachment points; deform use is legal but suspicious.
void validateSocketDeformUse(const Mesh& mesh, const Skeleton& skeleton,
                             const SkeletonProfile& profile, const std::string& assetName,
                             ValidationReport& report);

}  // namespace m2rig
