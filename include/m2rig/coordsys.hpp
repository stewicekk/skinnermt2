#pragma once
// Coordinate system conversion profiles (spec section 87).
// Canonical internal system: Right-handed, Y-up, Z-forward (forward = -Z in view space).
// All imports convert TO canonical; all exports convert FROM canonical.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "m2rig/math.hpp"
#include "m2rig/result.hpp"
#include "m2rig/smd.hpp"

namespace m2rig {

enum class CoordSys {
    Canonical,      // Y-up, Z-forward (Metin2/D3D11 native)
    ZUp_YForward,   // Z-up, Y-forward (Blender, FBX default, Granny3D)
    YUp_ZForward,   // Y-up, Z-forward (some game engines)
    ZUp_YBackward,  // Z-up, Y-backward (rare)
    XUp,            // X-up (some CAD)
};

const char* coordSysName(CoordSys cs);

// File-level axis direction (mirrors FBX GlobalSettings without dragging the
// OpenFBX headers into the dependency-free core).
enum class AxisDir { PosX, NegX, PosY, NegY, PosZ, NegZ, Unknown };

// Maps an (up, front) axis pair from a source file to a conversion source
// space. Covers the four right-handed mainstream combos; exotic or
// left-handed pairs return nullopt so the caller falls back to the profile
// assumed source (never a guessed conversion).
//   (PosZ, PosY) -> ZUp_YForward    (PosZ, NegY) -> ZUp_YBackward
//   (PosY, PosZ) -> YUp_ZForward    (PosY, NegZ) -> Canonical
std::optional<CoordSys> coordSysFromUpFront(AxisDir up, AxisDir front);

// Conversion matrix from source -> canonical (row-vector convention: v' = v * M).
// Throws nothing; XUp is NOT convertible (left-handed CAD space with no agreed
// mapping) — use tryConvertToCanonical for a checked conversion.
Mat4 convertToCanonical(CoordSys src);

// Checked conversion: fails explicitly for unimplemented spaces (XUp) instead
// of silently returning identity (no silent mis-orientation, ever).
Result<Mat4> tryConvertToCanonical(CoordSys src);
bool isConversionImplemented(CoordSys src);

// Conversion matrix from canonical -> target (row-vector convention).
Mat4 convertFromCanonical(CoordSys dst);

// Detect coordinate system from FBX global settings (OpenFBX).
// Returns nullopt if not detectable; caller should default to ZUp_YForward for FBX.
std::optional<CoordSys> detectFbxCoordSys(const void* ofbxScene);

// Detect coordinate system from GR2/Granny file (if metadata available).
// Returns nullopt if not detectable; caller should default to ZUp_YForward for GR2.
std::optional<CoordSys> detectGr2CoordSys(const std::filesystem::path& path);

// Per-format conversion profile (deterministic, tested).
struct ConversionProfile {
    std::string formatName;      // "FBX", "GR2", "SMD", etc.
    CoordSys assumedSource = CoordSys::ZUp_YForward;  // Default assumption
    bool detectAutomatically = true;                   // Try to detect from file
    bool convertMesh = true;                           // Convert vertex positions/normals
    bool convertSkeleton = true;                       // Convert bone local transforms
    bool convertAnimFrames = true;                     // Convert animation frame poses
    // Optional: override for specific known tools/exporters
    std::string toolHint;  // e.g., "blender", "maya", "granny2", "noesis"
};

const ConversionProfile& fbxConversionProfile();
const ConversionProfile& gr2ConversionProfile();
const ConversionProfile& smdConversionProfile();
const ConversionProfile& noesisConversionProfile();

// Apply conversion profile to mesh vertices/normals/tangents.
// Fails explicitly when the resolved source space has no implemented
// conversion (never a silent identity pass-through).
ResultVoid applyConversionProfile(const ConversionProfile& profile,
                                  const std::optional<CoordSys>& detectedSource, Mesh& mesh);

// Apply conversion profile to skeleton local transforms.
ResultVoid applyConversionProfile(const ConversionProfile& profile,
                                  const std::optional<CoordSys>& detectedSource,
                                  Skeleton& skeleton);

// Apply conversion profile to animation frames.
ResultVoid applyConversionProfile(const ConversionProfile& profile,
                                  const std::optional<CoordSys>& detectedSource,
                                  std::vector<SmdFrame>& frames);

// --- Automated transform diagnostics --------------------------------------
// World-space orientation sanity: catches rotated 90/180° (head at/below
// feet), detached/offset skeletons (no vertical overlap with the mesh) and
// unit mismatches on IMPORTED assets. Operates on global joint positions vs
// mesh bounds — never on raw locals (which are parent-relative).
struct OrientationFinding {
    std::string id;       // e.g. "ORIENT_HEAD_BELOW_FEET"
    std::string message;  // human-readable, actionable
    bool blocking = false;
};

struct OrientationReport {
    std::size_t jointCount = 0;
    Aabb meshBounds;
    Aabb skeletonBounds;  // over world joint positions
    float headY = 0.0f;   // max Y over *head* joints, -1 when none found
    float footY = 0.0f;   // min Y over *foot* joints, -1 when none found
    bool hasHeadFeet = false;
    // Worst rigid-bind alignment: max over bones of
    // distance(joint, centroid of verts dominated by that bone).
    float maxRigidDistance = -1.0f;
    std::string maxRigidBone;
    std::vector<OrientationFinding> findings;
    bool sane() const;  // false when any blocking finding exists
    std::string verdictLine() const;  // "ORIENTATION SANE ..." / "... CHECK ..."
    std::string toDisplayString() const;
};

OrientationReport diagnoseOrientation(const Mesh& mesh, const Skeleton& skeleton);

}  // namespace m2rig