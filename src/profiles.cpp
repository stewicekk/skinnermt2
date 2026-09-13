// Skeleton profiles: built-in Metin2 PC families + matching rules.
#include "m2rig/profiles.hpp"

#include <algorithm>
#include <cctype>

namespace m2rig {

std::string normalizeBoneName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    bool lastSpace = true;
    for (char c : name) {
        char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (l == '_' || l == '-' || l == '\t') l = ' ';
        if (l == ' ') {
            if (!lastSpace) out.push_back(' ');
            lastSpace = true;
        } else {
            out.push_back(l);
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string canonicalBoneName(const SkeletonProfile& profile, const std::string& name) {
    for (const auto& exp : profile.expectedBones)
        if (exp == name) return exp;
    const std::string norm = normalizeBoneName(name);
    auto it = profile.aliases.find(norm);
    if (it != profile.aliases.end()) return it->second;
    for (const auto& exp : profile.expectedBones)
        if (normalizeBoneName(exp) == norm) return exp;
    return name;  // unknown: preserved as-is, reported by validation
}

std::string mirrorBoneName(const SkeletonProfile& profile, const std::string& name) {
    const std::string canon = canonicalBoneName(profile, name);
    auto it = profile.mirrorOf.find(canon);
    if (it != profile.mirrorOf.end()) return it->second;
    return canon;  // center bones mirror to themselves
}

namespace {

SkeletonProfile baseBip01(const std::string& identity, const std::string& race,
                          const std::string& gender) {
    SkeletonProfile p;
    p.identity = identity;
    p.race = race;
    p.gender = gender;
    p.expectedBones = {"Bip01",         "Bip01 Pelvis",    "Bip01 Spine",     "Bip01 Spine1",
                       "Bip01 Neck",    "Bip01 Head",      "Bip01 L Clavicle", "Bip01 R Clavicle",
                       "Bip01 L UpperArm", "Bip01 R UpperArm", "Bip01 L Forearm", "Bip01 R Forearm",
                       "Bip01 L Hand",  "Bip01 R Hand",    "Bip01 L Thigh",   "Bip01 R Thigh",
                       "Bip01 L Calf",  "Bip01 R Calf",    "Bip01 L Foot",    "Bip01 R Foot",
                       "equip_left",    "equip_right",     "stip"};
    // Canonical parent map (mirrors sampleArmor.ts parentFor).
    p.parentOf = {{"Bip01 Pelvis", "Bip01"},   {"stip", "Bip01"},          {"Bip01 Spine", "Bip01 Pelvis"},
                  {"Bip01 Spine1", "Bip01 Spine"}, {"Bip01 Neck", "Bip01 Spine1"}, {"Bip01 Head", "Bip01 Neck"},
                  {"Bip01 L Clavicle", "Bip01 Spine1"}, {"Bip01 R Clavicle", "Bip01 Spine1"},
                  {"Bip01 L UpperArm", "Bip01 L Clavicle"}, {"Bip01 R UpperArm", "Bip01 R Clavicle"},
                  {"Bip01 L Forearm", "Bip01 L UpperArm"}, {"Bip01 R Forearm", "Bip01 R UpperArm"},
                  {"Bip01 L Hand", "Bip01 L Forearm"}, {"Bip01 R Hand", "Bip01 R Forearm"},
                  {"equip_left", "Bip01 L Hand"}, {"equip_right", "Bip01 R Hand"},
                  {"Bip01 L Thigh", "Bip01 Pelvis"}, {"Bip01 R Thigh", "Bip01 Pelvis"},
                  {"Bip01 L Calf", "Bip01 L Thigh"}, {"Bip01 R Calf", "Bip01 R Thigh"},
                  {"Bip01 L Foot", "Bip01 L Calf"}, {"Bip01 R Foot", "Bip01 R Calf"}};
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"Bip01 L Clavicle", "Bip01 R Clavicle"}, {"Bip01 L UpperArm", "Bip01 R UpperArm"},
        {"Bip01 L Forearm", "Bip01 R Forearm"}, {"Bip01 L Hand", "Bip01 R Hand"},
        {"Bip01 L Thigh", "Bip01 R Thigh"}, {"Bip01 L Calf", "Bip01 R Calf"},
        {"Bip01 L Foot", "Bip01 R Foot"}, {"equip_left", "equip_right"},
        {"Bip01 L Toe0", "Bip01 R Toe0"}, {"Bip01 L Toe0Nub", "Bip01 R Toe0Nub"},
        {"Bip01 L Finger0", "Bip01 R Finger0"}, {"Bip01 L Finger1", "Bip01 R Finger1"},
        {"Bip01 L Finger2", "Bip01 R Finger2"}, {"Bip01 L Finger3", "Bip01 R Finger3"},
        {"Bip01 L Finger4", "Bip01 R Finger4"}, {"Bip01 L Finger01", "Bip01 R Finger01"},
        {"Bip01 L Finger02", "Bip01 R Finger02"}, {"Bip01 L Finger11", "Bip01 R Finger11"},
        {"Bip01 L Finger12", "Bip01 R Finger12"}, {"Bip01 L Finger21", "Bip01 R Finger21"},
        {"Bip01 L Finger22", "Bip01 R Finger22"}, {"Bip01 L Finger31", "Bip01 R Finger31"},
        {"Bip01 L Finger32", "Bip01 R Finger32"}, {"Bip01 L Finger41", "Bip01 R Finger41"},
        {"Bip01 L Finger42", "Bip01 R Finger42"}, {"Bip01 L Finger0Nub", "Bip01 R Finger0Nub"},
        {"Bip01 L Finger1Nub", "Bip01 R Finger1Nub"}, {"Bip01 L Finger2Nub", "Bip01 R Finger2Nub"},
        {"Bip01 L Finger3Nub", "Bip01 R Finger3Nub"}, {"Bip01 L Finger4Nub", "Bip01 R Finger4Nub"}};
    for (const auto& pr : pairs) {
        p.mirrorOf[pr.first] = pr.second;
        p.mirrorOf[pr.second] = pr.first;
    }
    // Verified optional bones (Noesis FBX bridge dump of Data/Models/ninja.fbx,
    // 90 bones): never required, never flagged beyond Info.
    p.optionalBones = {
        "Bip01 Spine2", "Bip01 Spine3", "Bip01 HeadNub", "Bip01 Ponytail1", "Bip01 Ponytail11",
        "Bip01 Ponytail12", "Bip01 Ponytail13", "Bip01 Ponytail14", "Bip01 Ponytail1Nub",
        "Bip01 L Toe0", "Bip01 R Toe0", "Bip01 L Toe0Nub", "Bip01 R Toe0Nub",
        "Bip01 L Finger0", "Bip01 R Finger0", "Bip01 L Finger1", "Bip01 R Finger1",
        "Bip01 L Finger2", "Bip01 R Finger2", "Bip01 L Finger3", "Bip01 R Finger3",
        "Bip01 L Finger4", "Bip01 R Finger4", "Bip01 L Finger01", "Bip01 R Finger01",
        "Bip01 L Finger02", "Bip01 R Finger02", "Bip01 L Finger11", "Bip01 R Finger11",
        "Bip01 L Finger12", "Bip01 R Finger12", "Bip01 L Finger21", "Bip01 R Finger21",
        "Bip01 L Finger22", "Bip01 R Finger22", "Bip01 L Finger31", "Bip01 R Finger31",
        "Bip01 L Finger32", "Bip01 R Finger32", "Bip01 L Finger41", "Bip01 R Finger41",
        "Bip01 L Finger42", "Bip01 R Finger42", "Bip01 L Finger0Nub", "Bip01 R Finger0Nub",
        "Bip01 L Finger1Nub", "Bip01 R Finger1Nub", "Bip01 L Finger2Nub", "Bip01 R Finger2Nub",
        "Bip01 L Finger3Nub", "Bip01 R Finger3Nub", "Bip01 L Finger4Nub", "Bip01 R Finger4Nub",
        "Bip01 Tail1", "Bip01 Tail2", "B_01", "B_02", "B_03", "R_01", "R_02", "R_03", "L_01",
        "L_02", "L_03", "F_01", "F_02", "F_03", "Bone01", "Bone02", "Bone09", "Bone10"};
    for (const auto& opt : p.optionalBones) p.aliases[normalizeBoneName(opt)] = opt;
    // Lowercase/underscore aliases for every expected bone.
    for (const auto& exp : p.expectedBones) p.aliases[normalizeBoneName(exp)] = exp;
    p.aliases["bip01 l upper arm"] = "Bip01 L UpperArm";
    p.aliases["bip01 r upper arm"] = "Bip01 R UpperArm";
    p.aliases["bip01 l fore arm"] = "Bip01 L Forearm";
    p.aliases["bip01 r fore arm"] = "Bip01 R Forearm";
    // Real-model variants (verified in Data/Models/warrior_m.gr2.smd via grnreader98).
    p.aliases["equip right hand"] = "equip_right";
    p.aliases["equip left hand"] = "equip_left";
    p.socketBones = {"equip_left", "equip_right", "stip"};
    p.forbiddenUnexpected = {};  // deform use of sockets is a warning, not fatal
    p.allowedMissing = {"equip_left", "equip_right", "stip"};
    return p;
}

}  // namespace

SkeletonProfile warriorProfile() { return baseBip01("pc_warrior", "warrior", "any"); }
SkeletonProfile ninjaProfile() { return baseBip01("pc_ninja", "ninja", "any"); }
SkeletonProfile suraProfile() { return baseBip01("pc_sura", "sura", "any"); }
SkeletonProfile shamanProfile() { return baseBip01("pc_shaman", "shaman", "any"); }

// Male/female variants. Verified against Data/resources bone library
// (00-BONES/pc = warrior_m, sura_m, assassin_f, shaman_f;
//  00-BONES/pc2 = warrior_f, sura_f, assassin_m, shaman_m):
// both genders share the exact 23-bone Bip01 core in the Metin2 client
// (gender differences are mesh-level), so variants carry the gender tag
// while reusing the verified core + optional sets.
SkeletonProfile warriorMaleProfile() { return baseBip01("pc_warrior_m", "warrior", "m"); }
SkeletonProfile warriorFemaleProfile() { return baseBip01("pc_warrior_f", "warrior", "w"); }
SkeletonProfile ninjaMaleProfile() { return baseBip01("pc_assassin_m", "assassin", "m"); }
SkeletonProfile ninjaFemaleProfile() { return baseBip01("pc_assassin_f", "assassin", "w"); }
SkeletonProfile suraMaleProfile() { return baseBip01("pc_sura_m", "sura", "m"); }
SkeletonProfile suraFemaleProfile() { return baseBip01("pc_sura_f", "sura", "w"); }
SkeletonProfile shamanMaleProfile() { return baseBip01("pc_shaman_m", "shaman", "m"); }
SkeletonProfile shamanFemaleProfile() { return baseBip01("pc_shaman_f", "shaman", "w"); }

SkeletonProfile wolfmanProfile() {
    SkeletonProfile p = baseBip01("pc_wolfman", "wolfman", "any");
    // Wolfman carries extra bones; allow them explicitly instead of flagging.
    p.allowedMissing.insert(p.allowedMissing.end(), {"Bip01 Tail1", "Bip01 Tail2"});
    return p;
}

SkeletonProfile mountProfile() {
    // Mount rig (Data/resources/00-BONES/mount/mount.max). Per the Mounts
    // tutorial the saddle bone must never be scaled; it is registered as a
    // socket so deform use is reported, exactly like equip_*/stip.
    SkeletonProfile p = baseBip01("pc_mount", "mount", "any");
    p.socketBones.push_back("saddle");
    p.allowedMissing.push_back("saddle");
    p.aliases["saddle"] = "saddle";
    return p;
}

std::vector<SkeletonProfile> allBuiltinProfiles() {
    return {warriorProfile(),      warriorMaleProfile(),   warriorFemaleProfile(),
            ninjaProfile(),        ninjaMaleProfile(),     ninjaFemaleProfile(),
            suraProfile(),         suraMaleProfile(),      suraFemaleProfile(),
            shamanProfile(),       shamanMaleProfile(),    shamanFemaleProfile(),
            wolfmanProfile(),      mountProfile()};
}

const SkeletonProfile* findProfile(const std::string& identityOrRace) {
    static const std::vector<SkeletonProfile> kAll = allBuiltinProfiles();
    const std::string norm = normalizeBoneName(identityOrRace);
    // Exact identity first (pc_warrior_m before the pc_warrior race fallback).
    for (const auto& p : kAll) {
        if (normalizeBoneName(p.identity) == norm) return &p;
    }
    for (const auto& p : kAll) {
        if (normalizeBoneName(p.race) == norm) return &p;
    }
    return nullptr;
}

void validateAgainstProfile(const Skeleton& skel, const SkeletonProfile& profile,
                            const std::string& assetName, ValidationReport& report) {
    const std::string asset = assetName.empty() ? skel.name : assetName;
    auto isAllowedMissing = [&](const std::string& canon) {
        return std::find(profile.allowedMissing.begin(), profile.allowedMissing.end(), canon) !=
               profile.allowedMissing.end();
    };
    for (const auto& exp : profile.expectedBones) {
        bool found = false;
        for (const auto& b : skel.bones)
            if (canonicalBoneName(profile, b.name) == exp) {
                found = true;
                break;
            }
        if (!found && !isAllowedMissing(exp)) {
            report.add("PROFILE_MISSING_BONE", ValidationCategory::Skeleton, Severity::Warning,
                       "Expected bone '" + exp + "' is missing (profile " + profile.identity + ").",
                       asset, exp, false);
        }
    }
    for (const auto& b : skel.bones) {
        const std::string canon = canonicalBoneName(profile, b.name);
        const bool known = std::find(profile.expectedBones.begin(), profile.expectedBones.end(),
                                     canon) != profile.expectedBones.end() ||
                           std::find(profile.optionalBones.begin(), profile.optionalBones.end(),
                                     canon) != profile.optionalBones.end();
        if (!known) {
            const bool forbidden =
                std::find(profile.forbiddenUnexpected.begin(), profile.forbiddenUnexpected.end(),
                          canon) != profile.forbiddenUnexpected.end();
            report.add("PROFILE_UNEXPECTED_BONE", ValidationCategory::Skeleton,
                       forbidden ? Severity::Error : Severity::Info,
                       "Bone '" + b.name + "' is not part of profile " + profile.identity + ".",
                       asset, b.name, false);
        }
    }
    // Parent relationships.
    for (const auto& b : skel.bones) {
        const std::string canon = canonicalBoneName(profile, b.name);
        auto it = profile.parentOf.find(canon);
        if (it == profile.parentOf.end()) continue;
        if (b.parentId == kNoParent) continue;  // root-level tolerance for test skeletons
        const Bone* parent = skel.findById(static_cast<std::uint32_t>(b.parentId));
        if (!parent) continue;
        if (canonicalBoneName(profile, parent->name) != it->second) {
            report.add("PROFILE_BAD_PARENT", ValidationCategory::Skeleton, Severity::Warning,
                       "Bone '" + b.name + "' hangs under '" + parent->name + "', expected '" +
                           it->second + "'.",
                       asset, b.name, false);
        }
    }
}

std::vector<std::uint32_t> mapBonesByProfile(const Skeleton& source, const Skeleton& target,
                                              const SkeletonProfile& profile) {
    std::vector<std::uint32_t> mapping(source.bones.size(), kUnmappedBone);
    std::unordered_map<std::string, std::uint32_t> targetByCanon;
    for (const auto& b : target.bones) targetByCanon[canonicalBoneName(profile, b.name)] = b.id;
    for (const auto& b : source.bones) {
        auto it = targetByCanon.find(canonicalBoneName(profile, b.name));
        if (it != targetByCanon.end()) mapping[b.id] = it->second;
    }
    return mapping;
}

void validateSocketDeformUse(const Mesh& mesh, const Skeleton& skeleton,
                             const SkeletonProfile& profile, const std::string& assetName,
                             ValidationReport& report) {
    const std::string asset = assetName.empty() ? mesh.name : assetName;
    for (const auto& sock : profile.socketBones) {
        const Bone* b = skeleton.findByName(sock);
        if (!b) {
            // Also try canonical match.
            bool found = false;
            for (const auto& sb : skeleton.bones) {
                if (canonicalBoneName(profile, sb.name) == sock) {
                    found = true;
                    break;
                }
            }
            if (!found) continue;
        }
        std::size_t deformVerts = 0;
        for (const auto& v : mesh.vertices) {
            for (const auto& inf : v.influences) {
                if (inf.bone >= skeleton.bones.size()) continue;
                if (canonicalBoneName(profile, skeleton.bones[inf.bone].name) == sock &&
                    inf.weight > 1e-4f) {
                    ++deformVerts;
                    break;
                }
            }
        }
        if (deformVerts > 0) {
            report.add("SOCKET_DEFORM_USE", ValidationCategory::Weights, Severity::Warning,
                        "Socket bone '" + sock + "' deforms " + std::to_string(deformVerts) +
                            " vertices (sockets are attachment points).",
                        asset, sock, false);
        }
    }
}

}  // namespace m2rig
