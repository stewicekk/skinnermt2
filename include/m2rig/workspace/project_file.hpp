#pragma once
// .m2rig Workspace persistence (spec section 87, wave 8).
// Saves and loads the complete application state including:
// - Loaded assets with mesh/skeleton/animation data
// - Viewport camera settings and view mode
// - Brush state and weight paint configuration
// - Undo/redo command stack
// - Validation report and per-asset metadata
// On-disk format is JSON for human readability and editability,
// with a binary optional overlay for frequently-accessed data.

#include "m2rig/app.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skin_weights.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace m2rig {

// On-disk workspace format version.
inline constexpr std::uint32_t kM2RigWorkspaceVersion = 1;

// A workspace stores references to asset files on disk,
// not the asset data itself (meshes/skeletons are kept in memory
// or loaded on demand). This keeps .m2rg files lightweight.
struct M2RigAssetEntry {
    std::string id;             // user-friendly identifier
    std::string sourcePath;     // original file path (SMD, gr2, etc.)
    std::string profileId;      // skeleton profile used
    bool dirty = false;         // unsaved changes in this asset
};

// Describes the saved viewport/ camera state.
struct M2RigViewportState {
    float fovY = 45.0f;
    float cameraHeight = 2.0f;   // for orthographic
    float cameraTarget[3] = {0.0f, 0.0f, 0.0f};
    float cameraUp[3] = {0.0f, 1.0f, 0.0f};
    bool orthographic = false;
    float orthoWidth = 10.0f;
    float orthoHeight = 10.0f;
    float viewMatrix[16] = {1.0f};  // row-major, 4x4
};

// Full application state that can be saved/loaded as .m2rig project.
struct M2RigWorkspace {
    std::uint32_t version = kM2RigWorkspaceVersion;
    std::string title = "Metin2 Rigging Studio Project";
    // Timestamp when the workspace was last saved.
    std::string lastModified;  // ISO 8601 format, optional.
    // Viewport/ camera state (restored on workspace open).
    M2RigViewportState viewport;
    // Asset entries (referenced by id, data loaded on demand).
    std::vector<M2RigAssetEntry> assets;
    // Current selected asset id.
    std::string currentAssetId;
    // Brush state (persisted so brush settings survive relaunch).
    struct BrushState {
        float radius = 0.5f;
        float strength = 1.0f;
        bool normalize = true;
        bool symmetryX = false;
        bool symmetryY = false;
        bool symmetryZ = false;
        std::uint32_t symmetryBone = 0xFFFFFFFFu;
    } brush;
    // Locked bones by exact name (ids are not stable across imports).
    std::vector<std::string> lockedBoneNames;
    // Named selection sets: set name -> bone names (same stability rule).
    std::map<std::string, std::vector<std::string>> selectionSets;
    // Per-bone isolate state by name.
    std::vector<std::string> hiddenBoneNames;
    std::string soloBoneName;
    // Validation report snapshot (last run results).
    struct ValidationSnapshot {
        std::string summaryLine;
        bool exportBlocked = false;
        std::size_t vertexCount = 0;
        std::size_t triangleCount = 0;
        std::size_t boneCount = 0;
        std::size_t unweightedCount = 0;
        std::size_t invalidCount = 0;
        std::size_t overLimitCount = 0;
    } validation;
};

// Save the complete workspace to a .m2rig file.
Result<void> saveWorkspace(const M2RigWorkspace& workspace,
                           const std::filesystem::path& path);

// Load a workspace from a .m2rig file.
Result<M2RigWorkspace> loadWorkspace(const std::filesystem::path& path);

// Convenience: save current app state to a workspace file.
Result<void> saveCurrentWorkspace(App& app,
                                  const std::filesystem::path& path);

// Convenience: load workspace and restore app state.
Result<void> restoreWorkspace(App& app,
                              const std::filesystem::path& path);

// Internal: serialize a LoadedAsset to a serializable subset (no GPU resources).
struct SerializableAsset {
    std::string id;
    std::string profileId;
    Mesh mesh;           // mesh data (positions, norms, UVs, weights, indices)
    Skeleton skeleton;   // skeleton data (bones, transforms)
    std::vector<SmdFrame> animFrames;  // animation frames
    std::size_t currentFrame = 0;
    std::string sourcePath;  // original file path
    bool dirty = false;
    bool gpuDirty = false;   // will be false after deserialization
};

// Serialize a single asset to the JSON-compatible struct.
SerializableAsset serializeAsset(const LoadedAsset& asset);

// Deserialize a SerializableAsset back into a LoadedAsset (without GPU resources).
LoadedAsset deserializeAsset(const SerializableAsset& serial);

// Helper: write JSON with proper indentation.
std::string indentJson(const std::string& json, std::size_t spaces = 2);

// Helper: escape a string for JSON output.
std::string jsonEscape(const std::string& s);

} // namespace m2rig