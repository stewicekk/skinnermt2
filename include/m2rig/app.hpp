#pragma once
// Application state: loaded assets, selection, view/brush/symmetry settings,
// validation results. UI panels render this; format parsers mutate it through
// explicit methods. No ImGui types here so tests can include it.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "m2rig/camera.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/renderer.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/validation.hpp"
#include "m2rig/skin_weights.hpp"

namespace m2rig {

enum class ViewMode { Solid = 0, Wireframe, SolidWireframe, Normals, Height, Weights, UV };
enum class BrushMode { Add = 0, Subtract, Smooth, Normalize, Blur, Sharpen };
enum class SymmetryAxis { X = 0, Y, Z };
enum class GizmoOp { Translate = 0, Rotate = 1 };

const char* viewModeName(ViewMode mode);
const char* brushModeName(BrushMode mode);

struct UndoRedoCommand {
    virtual ~UndoRedoCommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
};

struct UndoPaintCommand : public UndoRedoCommand {
    std::size_t vertexIndex;
    std::vector<BoneInfluence> beforeInfluences;
    std::uint32_t boneId;
    float brushRadius;
    float brushStrength;
    PaintFalloff falloff;
    Vec3 brushWorldPos;
    UndoPaintCommand(std::size_t vi, std::uint32_t bid, float radius, float strength,
                     PaintFalloff fo, const Vec3& pos)
        : vertexIndex(vi), boneId(bid), brushRadius(radius), brushStrength(strength),
          falloff(fo), brushWorldPos(pos) {}
    void undo() override;
    void redo() override;
};

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
    std::string sourcePath;  // empty for procedural / unsaved
    std::string profileId = "pc_warrior";
    bool dirty = false;
    bool gpuDirty = true;
};

struct App {
    std::map<std::string, LoadedAsset> assets;
    std::string current;  // asset id
    int selectedBone = -1;

    ViewMode viewMode = ViewMode::Solid;
    BrushMode brushMode = BrushMode::Add;
    float brushRadius = 0.35f;
    float brushStrength = 0.6f;
    float brushFalloff = 1.0f;
    bool symmetryEnabled = false;
    SymmetryAxis symmetryAxis = SymmetryAxis::X;
    bool showGrid = true;
    bool showBones = true;
    bool showWireOverlay = false;
    bool xrayBones = false;
    int hoveredBone = -1;  // viewport hover highlight, not persisted
    bool previewDeform = false;  // CPU skinning preview of animation frames
    bool timelinePlaying = false;
    float timelineFps = 24.0f;
    bool timelineLoop = true;
    GizmoOp gizmoOp = GizmoOp::Translate;
    bool paintMode = false;  // when on, LMB drag paints instead of orbiting
    PaintFalloff paintFalloff = PaintFalloff::Linear;

    ArcballCamera camera;
    ValidationReport report;
    std::string statusMessage;
    std::string statusKind = "info";  // info|success|warning|error
    double fps = 0.0;
    bool vsync = true;

    LoadedAsset* currentAsset();
    const LoadedAsset* currentAsset() const;
    void setStatus(std::string message, std::string kind = "info");

    // Wave 1: procedural sample scene (always available offline).
    ResultVoid loadSampleArmor();
    // Sample template bound to a race profile (transfer source/target).
    ResultVoid loadSampleArmorForProfile(const std::string& profileId);
    // Wave 2: SMD file pipeline (non-destructive: source file never touched).
    ResultVoid importSmdFile(const std::string& path);
    ResultVoid exportSmdFile(const std::string& path);
    // Skeleton-only frame preview (mesh deformation arrives in wave 6).
    ResultVoid setCurrentFrame(std::size_t frameIndex);
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
    // Influence count for a bone id over the current mesh.
    std::size_t boneInfluenceCount(std::uint32_t boneId) const;
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
    // MSM export via AST writer (real output, preserves nothing lossy).
    ResultVoid exportMsmFile(const std::string& path);
    // GR2 export via external bridge (honest NOT_SUPPORTED_DIRECTLY status).
    ResultVoid exportGr2Bridge(const std::string& path);
    // Import FBX/GR2 via Noesis bridge -> temp SMD -> native import.
    ResultVoid importBridgedFile(const std::string& path);
    // Workspace persistence (.m2rig JSON).
    ResultVoid saveWorkspaceFile(const std::string& path);
    ResultVoid loadWorkspaceFile(const std::string& path);
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

private:
    struct InfluenceSnapshot {
        std::string label;
        std::vector<std::vector<BoneInfluence>> influences;
        std::vector<Vec3> bonePos;  // parallel to skeleton bones (pose undo)
        std::vector<Vec3> boneRot;
    };
    std::vector<InfluenceSnapshot> undoStack;
    std::vector<InfluenceSnapshot> redoStack;
};

}  // namespace m2rig