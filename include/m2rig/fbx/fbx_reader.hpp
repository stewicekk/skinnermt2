#pragma once
// Native FBX reader built on OpenFBX (Wave 9). Lives in the m2rig_fbx
// adapter library so m2rig_core stays dependency-free. Strategy mirrors the
// SMD importer: mesh soup -> indexed dedup -> canonical Mesh/Skeleton with
// repaired <=4 influences. Noesis remains the fallback for exotic FBX
// features (blendshapes, legacy versions).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "m2rig/coordsys.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"

namespace m2rig {

struct ConvertedFbx {
    Mesh mesh;
    Skeleton skeleton;
    std::size_t meshCount = 0;  // source FBX mesh objects merged
    // Axis arbitration record (trust-but-verify): file headers LIE in the
    // wild (converters baking one space while declaring another), so the
    // declared space is tried first and the result is diagnosed; an insane
    // result falls back to the profile assumed source. Deterministic,
    // reported, never silent.
    std::optional<CoordSys> detectedSpace;  // from GlobalSettings (nullopt = absent/exotic)
    CoordSys appliedSpace = CoordSys::ZUp_YForward;  // space actually converted from
    bool usedFallbackSource = false;
    std::string conversionNote;  // human-readable arbitration record
};

// Reads an .fbx file into canonical form. Fails explicitly (never partial)
// on IO errors, parse errors, empty geometry, or unmapped skin links.
Result<ConvertedFbx> readFbxFile(const std::string& path, const std::string& assetName);

}  // namespace m2rig
