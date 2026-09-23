#pragma once
// Application state: loaded assets, selection, view/brush/symmetry settings,
// validation results. UI panels render this; format parsers mutate it through
// explicit methods. No ImGui types here so tests can include it.
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "m2rig/ast/msm_ast.hpp"

#include "m2rig/anim.hpp"
#include "m2rig/camera.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/renderer.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/validation.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/self_learning.hpp"
#include "m2rig/gr2_deep_parser.hpp"

namespace m2rig {

// Display version (single source: CMake PROJECT_VERSION via M2RIG_VERSION).
#ifndef M2RIG_VERSION
#define M2RIG_VERSION "0.10.0"
#endif
inline const char* appVersion() { return M2RIG_VERSION; }

enum class ViewMode { Solid = 0, Wireframe, SolidWireframe, Normals, Height, Weights, UV };
enum class BrushMode { Add = 0, Subtract, Smooth, Normalize, Blur, Sharpen };
enum class SymmetryAxis { X = 0, Y, Z };
enum class GizmoOp { Translate = 0, Rotate = 1, Scale = 2 };
// Gizmo handle orientation: world axes, the bone's own axes, or the parent
// bone's axes (root bones degrade parent to world). The manipulated matrix
// is always world-space; core decomposition maps it back to locals.
enum class GizmoSpace { World = 0, Local = 1, Parent = 2 };

const char* brushModeName(BrushMode mode);

struct LoadedAsset {
    std::string id;
    Mesh mesh;
    Skeleton skeleton;
    // Bind-time inverse-bind matrices, captured at load while locals are the
    // bind pose. Posing (setCurrentFrame) recomputes globals AND the live
    // inverseBindTransform, so the deform preview needs this snapshot.
    std::vector<Mat4> bindInverse;
    std::vector<SmdFrame> animFrames;  // frame[0] == bind pose block
    std::size_t currentFrame = 0;
    AnimClip clip;  // authored pose keys (in-session, not persisted); when
                    // non-empty, scrub/playback sample the clip, not animFrames
    std::string sourcePath;  // empty for procedural / unsaved
    std::string profileId = "pc_warrior";
    bool dirty = false;
    bool gpuDirty = true;
    // PBR factor block for the viewport (Wave 25a, session state like clip:
    // metallic/roughness/AO/baseColor drive setPbrMaterial per draw; no GPU
    // re-upload needed, so edits never touch gpuDirty).
    PbrMaterial pbr;
};

struct App {
    std::map<std::string, LoadedAsset> assets;
    std::string current;  // asset id
    int selectedBone = -1;
    // Multi-select (view state, per asset session): primary selectedBone is
    // always a member when non-empty. Cleared on asset switch like locks.
    std::set<std::uint32_t> selectedBones;
    bool isBoneSelected(std::uint32_t bone) const;
    // Selects one bone (additive=false replaces). Keeps selectedBone primary.
    void selectBone(std::uint32_t bone, bool additive);
    void clearBoneSelection();
    // Selects bone + all descendants (hierarchy select).
    void selectBoneHierarchy(std::uint32_t bone, bool additive);
    // Named selection sets, stored by bone NAME (ids shift across imports).
    std::map<std::string, std::set<std::string>> boneSelectionSets;
    void saveBoneSelectionSet(const std::string& name);
    bool loadBoneSelectionSet(const std::string& name);
    void deleteBoneSelectionSet(const std::string& name);
    // Per-bone isolate/hide/solo (view state only, never exported).
    std::set<std::uint32_t> hiddenBones;
    int soloBone = -1;
    bool boneVisible(std::uint32_t bone) const;
    void setBoneHidden(std::uint32_t bone, bool hidden);
    void clearHiddenBones();
    // Per-vertex weight inspection selection (mesh index, -1 none).
    int selectedVertex = -1;

    ViewMode viewMode = ViewMode::Solid;
    BrushMode brushMode = BrushMode::Add;
    float brushRadius = 0.35f;
    float brushStrength = 0.6f;
    bool symmetryEnabled = false;
    SymmetryAxis symmetryAxis = SymmetryAxis::X;
    bool showGrid = true;
    bool showBones = true;
    bool showWireOverlay = false;
    // Wave 21: X-ray defaults ON so the skeleton reads through closed meshes
    // (sample armor + ninja are closed solids; LESS alone hides interior bones
    // and users conclude the viewport is broken). Toggle stays in toolbar +
    // View menu + Bone panel (single bool, no duplicate state).
    bool xrayBones = true;
    bool textured = false;  // sample material DDS in the viewport when found
    bool usePbr = false;    // Cook-Torrance PBR viewport shading (Wave 25a)
    int hoveredBone = -1;  // viewport hover highlight, not persisted
    bool previewDeform = false;  // CPU skinning preview of animation frames
    bool useDqs = false;  // Dual Quaternion Skinning (avoids candy-wrapper artifacts)
    bool timelinePlaying = false;
    float timelineFps = 24.0f;
    bool timelineLoop = true;
    GizmoOp gizmoOp = GizmoOp::Translate;
    GizmoSpace gizmoSpace = GizmoSpace::World;
    // Step snapping for gizmo drags (session view state, like the rest of
    // the gizmo settings): world-unit translate step, degree rotate step,
    // unitless scale step. Passed straight to ImGuizmo.
    bool gizmoSnap = false;
    float snapTranslate = 0.1f;
    float snapRotateDeg = 15.0f;
    float snapScale = 0.1f;
    // Gizmo display options
    bool gizmoShowAxisLabels = true;
    float gizmoHandleSize = 1.0f;
    bool gizmoShowPlaneHandles = true;
    bool gizmoShowCenterHandle = true;
    bool paintMode = false;  // when on, LMB drag paints instead of orbiting
    PaintFalloff paintFalloff = PaintFalloff::Linear;

    ArcballCamera camera;
    ValidationReport report;
    std::string statusMessage;
    std::string statusKind = "info";  // info|success|warning|error
    double fps = 0.0;
    bool vsync = true;

    // Bone visualization settings
    float boneThickness = 1.0f;
    float boneJointSize = 0.03f;
    bool boneShowLabels = true;
    bool boneShowJointCrosses = true;
    float boneSelectedColor[4] = {1.0f, 0.85f, 0.2f, 1.0f};
    float boneHoveredColor[4] = {1.0f, 0.5f, 0.1f, 1.0f};
    float boneLockedColor[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    float boneParentColor[4] = {0.3f, 0.9f, 0.3f, 1.0f};
    float boneChildColor[4] = {0.3f, 0.3f, 0.9f, 1.0f};
    float boneDefaultColor[4] = {0.9f, 0.9f, 0.95f, 1.0f};
    float boneHiddenColor[4] = {0.35f, 0.38f, 0.44f, 1.0f};
    float boneSocketColor[4] = {0.3f, 0.9f, 0.9f, 1.0f};

    // Selection state for box-select
    bool boxSelecting = false;
    Vec2 boxSelectStart;
    Vec2 boxSelectEnd;
    std::vector<std::uint32_t> boxSelectCandidates;

    // UI state persistence
    struct UISettings {
        bool showMenuBar = true;
        bool showToolbar = true;
        bool showStatusBar = true;
        float panelSpacing = 8.0f;
        float panelRounding = 6.0f;
        bool compactMode = false;
        // Dock layout will be persisted via ImGui ini file
        // Panel visibility (persisted in preferences)
        bool showBonePanel = true;
        bool showWeightsPanel = true;
        bool showMaterialsPanel = true;
        bool showMSMInspectorPanel = true;
        bool showProjectPanel = true;
        bool showExportPanel = true;
        bool showValidationPanel = true;
        bool showConsolePanel = true;
        bool showSystemPanel = true;
        bool showTimelinePanel = true;
        bool showSettingsPanel = true;
        bool showBoneDisplayPanel = false;
        bool showGizmoPanel = false;
        bool showViewportSettingsPanel = false;
    };
    UISettings uiSettings;

    // Persistent user preferences (saved to config/user_prefs.json)
    struct UserPreferences {
        // Camera
        float cameraFovY = 50.0f;
        float cameraNearZ = 0.05f;
        float cameraFarZ = 200.0f;
        bool cameraOrthographic = false;
        float cameraOrthoHeight = 4.0f;
        float cameraOrbitSensitivity = 0.005f;
        float cameraPanSensitivity = 0.001f;
        float cameraZoomSensitivity = 0.1f;
        
        // Viewport
        bool showGrid = true;
        bool showBones = true;
        bool xrayBones = true;
        bool showWireOverlay = false;
        bool textured = false;
        bool previewDeform = false;
        bool useDqs = false;
        ViewMode viewMode = ViewMode::Solid;
        
        // Bone visualization
        float boneThickness = 1.0f;
        float boneJointSize = 0.03f;
        bool boneShowLabels = true;
        bool boneShowJointCrosses = true;
        float boneSelectedColor[4] = {1.0f, 0.85f, 0.2f, 1.0f};
        float boneHoveredColor[4] = {1.0f, 0.5f, 0.1f, 1.0f};
        float boneLockedColor[4] = {0.5f, 0.5f, 0.5f, 1.0f};
        float boneParentColor[4] = {0.3f, 0.9f, 0.3f, 1.0f};
        float boneChildColor[4] = {0.3f, 0.3f, 0.9f, 1.0f};
        float boneDefaultColor[4] = {0.9f, 0.9f, 0.95f, 1.0f};
        float boneHiddenColor[4] = {0.35f, 0.38f, 0.44f, 1.0f};
        float boneSocketColor[4] = {0.3f, 0.9f, 0.9f, 1.0f};
        
        // Gizmo
        GizmoOp gizmoOp = GizmoOp::Translate;
        GizmoSpace gizmoSpace = GizmoSpace::World;
        bool gizmoSnap = false;
        float snapTranslate = 0.1f;
        float snapRotateDeg = 15.0f;
        float snapScale = 0.1f;
        bool gizmoShowAxisLabels = true;
        float gizmoHandleSize = 1.0f;
        bool gizmoShowPlaneHandles = true;
        bool gizmoShowCenterHandle = true;
        
        // Weight paint
        BrushMode brushMode = BrushMode::Add;
        float brushRadius = 0.35f;
        float brushStrength = 0.6f;
        bool symmetryEnabled = false;
        SymmetryAxis symmetryAxis = SymmetryAxis::X;
        PaintFalloff paintFalloff = PaintFalloff::Linear;
        
        // UI
        bool compactMode = false;
        float panelSpacing = 8.0f;
        float panelRounding = 6.0f;
        
        // Timeline
        float timelineFps = 24.0f;
        bool timelineLoop = true;
        
        // Export
        float lodRatio = 0.5f;
        
        // Autosave
        int autosaveMinutes = 5;
    };
    UserPreferences prefs;

    LoadedAsset* currentAsset();
    const LoadedAsset* currentAsset() const;
    void setStatus(std::string message, std::string kind = "info");
    
    // Preference persistence
    ResultVoid savePreferences(const std::filesystem::path& configDir);
    ResultVoid loadPreferences(const std::filesystem::path& configDir);
    void applyPreferences();

    // Wave 21 toast queue (view state only, never persisted to .m2rig):
    // sticky errors stay until dismissed, info/warning auto-expire.
    struct Toast {
        std::string message;
        std::string kind = "info";  // info|success|warning|error
        double until = 0.0;         // ImGui::GetTime() expiry for non-sticky
        bool sticky = false;
        std::size_t id = 0;
    };
    std::vector<Toast> toasts;
    void pushToast(std::string message, std::string kind = "info", double nowSeconds = 0.0);
    void dismissToast(std::size_t id);
    void tickToasts(double nowSeconds);
    // Throttled pixel-proof cache (session only): verdict from readViewport,
    // never from drawCalls alone. Updated at most every 2 s or on demand.
    struct PixelProof {
        double t = -1e9;
        float modelFrac = 0.0f;
        std::string verdict = "unknown";  // ok|grid-only|blank|unknown|invalid
    };
    PixelProof pixelProof;

    // Wave 1: procedural sample scene (always available offline).
    ResultVoid loadSampleArmor();
    // Shared SMD-text installer (sync core of importSmdFile + bridge finish).
    ResultVoid applySmdText(const std::string& smdText, const std::string& srcPath);
    // Shared converted-asset installer (mesh+skeleton already canonical).
    ResultVoid installConverted(Mesh mesh, Skeleton skeleton, std::vector<SmdFrame> frames,
                                const std::string& srcPath, const std::string& how);
    // Sample template bound to a race profile (transfer source/target).
    ResultVoid loadSampleArmorForProfile(const std::string& profileId);
    // Wave 2: SMD file pipeline (non-destructive: source file never touched).
    ResultVoid importSmdFile(const std::string& path);
    ResultVoid exportSmdFile(const std::string& path);
    // Skeleton-only frame preview (mesh deformation arrives in wave 6).
    // When the asset clip holds keys, the pose is sampled from the clip
    // (clamped); otherwise the imported animFrames are used as before.
    ResultVoid setCurrentFrame(std::size_t frameIndex);
    // Timeline length: max(imported frames, clip last+1), 0 without asset.
    std::size_t timelineFrameCount() const;
    // Pose keyframes over the current frame (in-session authoring).
    ResultVoid addKeyframeHere();
    ResultVoid deleteKeyframeHere();
    ResultVoid clearClip();
    // Bakes the clip to animFrames (REPLACES imported frames; pose undo
    // does not cover the frame array, the status says so explicitly).
    ResultVoid bakeClipToFrames();
    // Re-uploads current mesh to the GPU with the active coloring.
    ResultVoid refreshGpu(Renderer& renderer);
    // Runs all currently-implemented validators over the current asset.
    void runValidation();
    // Bone locks: locked bones are skipped by paint/mirror/transfer and
    // blocked in the gizmo. Guard state only (never undoable, never exported).
    // Cleared on asset switch; persisted by bone NAME in .m2rig workspaces.
    std::set<std::uint32_t> lockedBones;
    bool isBoneLocked(std::uint32_t bone) const { return lockedBones.count(bone) != 0; }
    void setBoneLocked(std::uint32_t bone, bool locked);
    void lockSocketBones();
    void unlockAllBones();
    // Submesh isolation: hidden submeshes are skipped at GPU upload only
    // (exports and picking always use the full mesh).
    std::set<std::size_t> hiddenSubmeshes;
    // Influence count for a bone id over the current mesh. Backed by a
    // per-asset histogram rebuilt only when weights change (the skeleton
    // tree + bone panel query this per bone per frame).
    std::size_t boneInfluenceCount(std::uint32_t boneId) const;
    // Weight-quality aggregates for the Weights panel, same caching rule.
    const WeightQualityMetrics& weightQuality() const;
    // Marks cached weight aggregates stale. Called by every weight-mutating
    // op and every asset install/switch.
    void noteWeightsChanged();
    // Drops undo + redo (asset install/switch/workspace restore: snapshots
    // reference a different mesh and must never leak across assets).
    void clearUndoHistory();
    // --- Weight paint (wave 3/4, real backend) --------------------------------
    // Applies one brush dab at center (mesh local space) to the current asset
    // using the active brush settings + selected bone. Pushes undo, runs
    // symmetry second pass when enabled, marks gpuDirty. Returns affected verts.
    std::size_t paintStroke(const Vec3& center);
    // Undo/redo over full influence snapshots (cap 50). Real stack, no fakes.
    void pushUndoSnapshot(const std::string& label);
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    void undo();
    void redo();
    // Mirror current mesh weights across the active symmetry axis.
    ResultVoid mirrorWeights();
    // KD-tree transfer from another loaded asset into the current one.
    ResultVoid transferWeightsFrom(const std::string& sourceAssetId);
    // Self-training transfer: coordinate descent over the bone remap.
    ResultVoid transferWeightsSelfTrained(const std::string& sourceAssetId);
    // Destructive group ops over the selected bone (undoable, lock-guarded).
    ResultVoid floodSelectedBone();
    ResultVoid pruneSelectedBone();
    // Per-vertex influence editing for the weight table (undoable,
    // lock-aware, <=4 + renormalized, mass never silent).
    ResultVoid setVertexInfluenceWeight(std::size_t vertex, std::size_t slot, float weight);
    ResultVoid removeVertexInfluence(std::size_t vertex, std::size_t slot);
    ResultVoid normalizeVertexWeights(std::size_t vertex);
    // Automatic initial rigging of the whole current mesh to its skeleton.
    ResultVoid autoRigFromSkeleton();
    // MSM export via AST writer (real output, preserves nothing lossy).
    ResultVoid exportMsmFile(const std::string& path);
    // GR2 export via external bridge (honest NOT_SUPPORTED_DIRECTLY status).
    ResultVoid exportGr2Bridge(const std::string& path);
    // FBX export via Noesis bridge with -rotate 90 0 0 for Metin2 Y-up coordinate system.
    ResultVoid exportFbxFile(const std::string& path);
    // ANI export (Metin2 animation binary format)
    ResultVoid exportAniFile(const std::string& path);
    // Import FBX/GR2 via Noesis bridge -> temp SMD -> native import.
    ResultVoid importBridgedFile(const std::string& path);
    // Async bridge import: start returns immediately (UI stays live);
    // pollBridgeImport (called once per frame) finishes and returns true
    // exactly once when the job completes. All App mutation happens on the
    // polling (UI) thread; the worker only produces SMD text.
    struct BridgeJob {
        std::string label;
        std::string sourcePath;
        std::string sourceExt;
        double startTime = 0.0;
    };
    // Worker output stays text-only (thread boundary): the UI thread parses
    // and applies the canonical coordsys profile. Origin selects the profile
    // ("gr2" -> gr2ConversionProfile, "noesis" -> noesisConversionProfile).
    struct BridgeChainOutput {
        std::string smdText;
        std::string origin;
    };
    bool bridgeBusy = false;
    BridgeJob bridgeJob;
    bool startBridgedImport(const std::string& path, double nowSeconds);
    bool pollBridgeImport();
    // Shared bridged-text installer: parse SMD text from a bridge worker, then
    // apply the canonical conversion profile matching origin on the UI thread
    // (same core as sync importBridgedFile / CLI gr22smd — never bare).
    ResultVoid applyBridgedSmdText(const std::string& smdText, const std::string& origin,
                                   const std::string& srcPath);
    // Workspace persistence (.m2rig JSON).
    ResultVoid saveWorkspaceFile(const std::string& path);
    ResultVoid loadWorkspaceFile(const std::string& path);
    // MSM inspector document (reference only, never deformed/exported).
    std::optional<MsmDocument> msmDoc;
    std::string msmPath;
    ValidationReport msmReport;
    ResultVoid openMsmInspector(const std::string& path);
    void closeMsmInspector();
    // Autosave (minutes, 0 = off) + crash-recovery bookkeeping.
    int autosaveMinutes = 5;
    double lastAutosaveTime = 0.0;
    std::string lastAutosaveInfo = "never";
    void tickAutosave(const std::filesystem::path& projectsDir, double nowSeconds);
    ResultVoid saveAutosaveNow(const std::filesystem::path& projectsDir);
    // Batch export of all loaded assets (SMD + MSM per asset).
    struct BatchRow {
        std::string id;
        std::string smdPath;
        std::string msmPath;
        std::string message;
        bool smdOk = false;
        bool msmOk = false;
    };
    Result<std::vector<BatchRow>> exportAllBatch();

    // --- Self-learning weight transfer system ---
    SelfLearningDatabase learningDb;
    std::unique_ptr<SelfLearningTransfer> learningTransfer;
    SelfLearningTransfer& getLearningTransfer();
    // Self-learning actions
    ResultVoid learnFromAsset(const std::string& assetId);
    ResultVoid learnFromDirectory(const std::string& dirPath);
    ResultVoid saveLearningDatabase(const std::string& path);
    ResultVoid loadLearningDatabase(const std::string& path);
    ResultVoid selfLearningTransfer(const std::string& sourceAssetId);
    ResultVoid selfLearningAutoRig();
    ResultVoid analyzeGr2Directory(const std::string& dirPath);

private:
    struct InfluenceSnapshot {
        std::string label;
        std::vector<std::vector<BoneInfluence>> influences;
        std::vector<Vec3> bonePos;  // parallel to skeleton bones (pose undo)
        std::vector<Vec3> boneRot;
        std::vector<Vec3> boneScale;
    };
    std::vector<InfluenceSnapshot> undoStack;
    std::vector<InfluenceSnapshot> redoStack;
    mutable std::vector<std::size_t> boneHistogram_;
    mutable WeightQualityMetrics weightQualityCache_;
    mutable bool boneHistogramDirty_ = true;
    mutable bool weightQualityDirty_ = true;
    std::future<Result<BridgeChainOutput>> bridgeFuture;
    InfluenceSnapshot takeSnapshot(const std::string& label);
    void restoreSnapshot(InfluenceSnapshot& snap);
};

}  // namespace m2rig