#pragma once
// Granny3D / Noesis Adapter Bridge (spec sections 24-26, 69).
// This adapter explicitly does NOT implement native GR2/GR3 emit.
// Instead it routes through configured external tools (Noesis CLI,
// granny_compiler.exe) and returns a NOT_SUPPORTED_DIRECTLY status,
// allowing the UI to display a configured bridge command or honest error.
//
// Native GR2 support requires a licensed Granny SDK or external compiler.
// This header provides the boundary interface so the application never
// fakes GR2 output or claims capabilities it doesn't have.

#include "m2rig/result.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/skeleton.hpp"
#include <filesystem>
#include <string>

namespace m2rig {

// Status returned by GR2/GR3 adapter attempts.
enum class Gr2ExportStatus {
    Success,                  // Export succeeded (uncommon, via bridge)
    NotSupportedDirectly,     // Native emit not supported; use bridge
    Failed,                   // Export failed (error message provided)
    Cancelled                 // User cancelled the export operation
};

// Configuration for the GR2 export bridge.
struct Gr2BridgeConfig {
    // Path to Noesis CLI executable (or granny_compiler.exe).
    // Empty means bridge is not configured.
    std::filesystem::path noesisCliPath;
    // Optional command-line arguments.
    std::string noesisArgs;
    // Timeout in milliseconds for the external process.
    std::uint32_t timeoutMs = 30000;
    // If true, keep the temporary SMD file produced by the bridge for inspection.
    bool keepTempSmd = false;
};

// Export a mesh to GR2 format via the configured bridge.
// Returns Gr2ExportStatus and an optional error message.
Gr2ExportStatus exportGr2ViaBridge(
    const Mesh& mesh,
    const Skeleton& skeleton,
    const std::filesystem::path& outputPath,
    const Gr2BridgeConfig& config,
    std::string& errorMessage);

// Boundary probe: direct GR2 weight extraction is NOT implemented and never
// faked — this always returns Gr2ExportStatus::NotSupportedDirectly (or
// Failed when the file is missing). It exists so the boundary stays visible
// at the type level; real weight intake goes through SMD conversion
// (convertGr2ToSmdViaGrnReader). Native extraction is Wave-29 scope.
struct ExtractedGr2Weights {
    Mesh mesh;
    Skeleton skeleton;
    bool success = false;
    std::string error;
};

Gr2ExportStatus extractWeightsFromGr2ViaBridge(
    const std::filesystem::path& gr2Path,
    const Gr2BridgeConfig& config,
    ExtractedGr2Weights& outWeights);

// Default bridge configuration (noesis/ folder, or PATH).
Gr2BridgeConfig defaultGr2BridgeConfig();

// Locates grnreader98.exe (the documented GR2->SMD batch tool living in
// Data/resources/Convert gr2 to mesh/). Empty path when not present.
std::filesystem::path findGrnReader();

// Converts a GR2 file to SMD text via grnreader98 (<gr2> -a -t, batch mode,
// all submeshes + weights + skeleton). Non-destructive: the source file is
// copied to the temp dir first because grnreader writes <input>.smd next
// to its input. Returns the SMD text on success.
Result<std::string> convertGr2ToSmdViaGrnReader(const std::filesystem::path& gr2Path,
                                               std::uint32_t timeoutMs = 60000);

// Converts a GR2 file to FBX via Noesis with -rotate 90 0 0 for Metin2
// coordinate system fix (Z-up to Y-up). Returns the output FBX path on success.
Result<std::string> convertGr2ToFbxViaNoesis(const std::filesystem::path& gr2Path,
                                             const std::filesystem::path& outputFbxPath,
                                             const Gr2BridgeConfig& config);

// Validate that a GR2 file has readable magic/version before attempting export.
// Returns true if the file appears to be a valid Granny3D container.
bool isValidGr2Container(const std::filesystem::path& path);

} // namespace m2rig