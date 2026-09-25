// Dear ImGui panels: toolbar, asset/scene/skeleton, viewport, properties,
// weights, materials, validation, console, project, export, timeline.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#ifdef M2RIG_WITH_GIZMO
#include "ImGuizmo.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <string>

#include "m2rig/app.hpp"
#include "m2rig/file_dialog.hpp"
#include "m2rig/lod.hpp"
#include "m2rig/mse.hpp"
#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/panels.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"

namespace m2rig {
namespace {

void* g_mainWindow = nullptr;

}  // namespace

namespace {

// --- Live dock layout -------------------------------------------------------
// File-scope (NOT function-static): both Reset paths must force a rebuild
// without restart. The 14 App::UISettings panel flags are live-read every
// frame in drawAllPanels, so presets apply instantly; the dock *structure*
// rebuilds via buildDefaultDockLayout() (declared here, defined just before
// drawAllPanels). Viewport compositing (NoBackground + offscreen texture in
// drawViewportPanel) is untouched by design.
bool g_dockBuilt = false;
void requestDockRebuild() { g_dockBuilt = false; }
void markDockBuilt() { g_dockBuilt = true; }
void buildDefaultDockLayout(ImGuiID dockspaceId);

// --- Semantic theme palette (single source for UI feedback colors) --------
// The teal-cyan accent lives in applyDarkTheme (main.cpp) and is
// intentionally not duplicated here; overlay U32 colors (selection box,
// labels, brush ring) are a separate draw-list family and stay literal.
namespace Theme {
const ImVec4 kOk{0.35f, 0.9f, 0.5f, 1.0f};
const ImVec4 kWarn{0.95f, 0.75f, 0.3f, 1.0f};
const ImVec4 kErr{1.0f, 0.4f, 0.35f, 1.0f};
const ImVec4 kInfo{0.6f, 0.65f, 0.7f, 1.0f};
const ImVec4 kDanger{0.55f, 0.22f, 0.20f, 1.0f};
const ImVec4 kDangerHover{0.70f, 0.28f, 0.25f, 1.0f};
const ImVec4 kDangerActive{0.80f, 0.32f, 0.28f, 1.0f};
inline ImVec4 statusColor(const std::string& kind) {
    if (kind == "success") return kOk;
    if (kind == "warning") return kWarn;
    if (kind == "error") return kErr;
    return kInfo;
}
}  // namespace Theme

}  // namespace

void setMainWindowHandle(void* hwnd) { g_mainWindow = hwnd; }

namespace {

// --- helpers -------------------------------------------------------------

// Single Frame entry point: every Frame (F) surface routes here so the verb
// cannot drift again (P0-1 class). The overlay keeps its smooth flight;
// all other surfaces snap instantly (historical behavior, preserved).
// Null-safe (callers additionally disable without an asset).
inline void frameWholeModel(App& app, bool smooth) {
    const LoadedAsset* a = app.currentAsset();
    if (!a) return;
    if (smooth)
        app.camera.frameAabbSmooth(a->mesh.bounds, ImGui::GetTime());
    else
        app.camera.frameAabb(a->mesh.bounds);
}
// Tooltip for the preceding item (tooltip-pass unification: destructive
// and silent actions explain themselves instead of surprising).
inline void tipFor(const char* t) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t);
}

std::vector<GpuVertex> boneSegments(const Skeleton& skel, const App& app,
                                    float jointSize = 0.03f) {
    std::vector<GpuVertex> segs;
    auto colorFor = [&](const Bone& b) -> Vec3 {
        if (app.isBoneSelected(b.id) || static_cast<int>(b.id) == app.selectedBone)
            return {app.boneSelectedColor[0], app.boneSelectedColor[1], app.boneSelectedColor[2]};
        if (static_cast<int>(b.id) == app.hoveredBone) return {app.boneHoveredColor[0], app.boneHoveredColor[1], app.boneHoveredColor[2]};
        if (app.isBoneLocked(b.id)) return {app.boneLockedColor[0], app.boneLockedColor[1], app.boneLockedColor[2]};
        if (!app.boneVisible(b.id)) return {app.boneHiddenColor[0], app.boneHiddenColor[1], app.boneHiddenColor[2]};
        // Check if bone is a parent of selected bone
        if (app.selectedBone >= 0) {
            const Bone* selBone = skel.findById(static_cast<std::uint32_t>(app.selectedBone));
            if (selBone) {
                // Check if this bone is a parent of selected
                std::uint32_t parentId = selBone->parentId;
                while (parentId != kNoParent) {
                    if (parentId == b.id) {
                        return {app.boneParentColor[0], app.boneParentColor[1], app.boneParentColor[2]};
                    }
                    const Bone* pb = skel.findById(parentId);
                    if (!pb) break;
                    parentId = pb->parentId;
                }
                // Check if this bone is a child of selected
                for (std::uint32_t childId : selBone->children) {
                    if (childId == b.id) {
                        return {app.boneChildColor[0], app.boneChildColor[1], app.boneChildColor[2]};
                    }
                }
            }
        }
        if (b.name == "equip_left" || b.name == "equip_right" || b.name == "stip")
            return {app.boneSocketColor[0], app.boneSocketColor[1], app.boneSocketColor[2]};
        return {app.boneDefaultColor[0], app.boneDefaultColor[1], app.boneDefaultColor[2]};
    };
    for (const auto& b : skel.bones) {
        if (!app.boneVisible(b.id)) continue;
        const Vec3 c{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                     b.globalTransform.m[3][2]};
        const Vec3 col = colorFor(b);
        GpuVertex v0{};
        v0.position = c;
        v0.normal = {0, 1, 0};
        v0.color[0] = col.x;
        v0.color[1] = col.y;
        v0.color[2] = col.z;
        v0.color[3] = 1.0f;
        // Root bones (and orphans) have no parent segment, but they still get
        // a joint marker so a root-only skeleton is never invisible.
        if (b.parentId != kNoParent) {
            if (const Bone* p = skel.findById(static_cast<std::uint32_t>(b.parentId))) {
                if (!app.boneVisible(static_cast<std::uint32_t>(b.parentId))) {
                    // Hidden parent: joint marker only, no segment.
                } else {
                    GpuVertex v1 = v0;
                    v1.position = {p->globalTransform.m[3][0], p->globalTransform.m[3][1],
                                   p->globalTransform.m[3][2]};
                    segs.push_back(v1);
                    segs.push_back(v0);
                }
            }
        }
        // Joint cross (X/Y/Z ticks) so joints read at any angle.
        if (app.boneShowJointCrosses) {
            // jointSize is screen-aware (see renderScene); default fits the sample armor.
            const float t = jointSize > 1e-9f ? jointSize : app.boneJointSize;
            const Vec3 axes[3] = {{t, 0, 0}, {0, t, 0}, {0, 0, t}};
            for (const Vec3& ax : axes) {
                GpuVertex j0 = v0, j1 = v0;
                j0.position = c + ax * -1.0f;
                j1.position = c + ax;
                segs.push_back(j0);
                segs.push_back(j1);
            }
        }
        // Bone label (rendered separately via ImGui overlay)
    }
    return segs;
}

void drawSkeletonTree(App& app, std::int32_t boneId, int depth = 0) {
    LoadedAsset* asset = app.currentAsset();
    if (!asset) return;
    const Bone* b = asset->skeleton.findById(static_cast<std::uint32_t>(boneId));
    if (!b) return;
    if (depth == 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (b->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (app.isBoneSelected(b->id) || app.selectedBone == boneId)
        flags |= ImGuiTreeNodeFlags_Selected;
    char label[256];
    std::snprintf(label, sizeof(label), "%s%s%s  [%zu]", app.isBoneLocked(b->id) ? "[L] " : "",
                  app.boneVisible(b->id) ? "" : "[H] ", b->name.c_str(),
                  app.boneInfluenceCount(b->id));
    const bool open = ImGui::TreeNodeEx(label, flags);
    if (ImGui::IsItemClicked()) {
        // Plain click replaces, Ctrl+click toggles into the multi-select.
        const bool additive = ImGui::GetIO().KeyCtrl != 0;
        if (additive && app.isBoneSelected(b->id) && app.selectedBones.size() > 1) {
            app.selectedBones.erase(b->id);
            app.selectedBone = static_cast<int>(*app.selectedBones.rbegin());
        } else {
            app.selectBone(b->id, additive);
        }
        if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
    }
    if (open) {
        for (std::uint32_t child : b->children)
            drawSkeletonTree(app, static_cast<std::int32_t>(child), depth + 1);
        ImGui::TreePop();
    }
}

// --- panels --------------------------------------------------------------

#ifdef M2RIG_WITH_GIZMO
// Bone manipulator for the selected bone (WORLD space). Runs whenever the
// viewport panel has an area — deliberately NOT gated on hover: hover-gating
// made the gizmo vanish under popups/menus and froze the drag-end
// bookkeeping (undo snapshot, validation, status) when the pointer left the
// panel mid-drag. Input arbitration (orbit/pick/paint defer to the gizmo)
// still uses the IsOver/IsUsing outputs.
void updateBoneGizmo(App& app, const ImVec2& cursor, const ImVec2& avail, bool& gizmoUsing,
                     bool& gizmoOver) {
    static bool wasUsing = false;
    static bool wasBlocked = false;
    if (avail.x <= 0.0f || avail.y <= 0.0f) {
        // Degenerate panel (docking transition): never feed ImGuizmo a
        // zero-size rect (aspect 0 -> infinite projection).
        gizmoUsing = false;
        gizmoOver = false;
        wasUsing = false;
        wasBlocked = false;
        return;
    }
    LoadedAsset* ga = app.currentAsset();
    Bone* mb = (ga && app.selectedBone >= 0)
                   ? ga->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone))
                   : nullptr;
    if (!mb) {
        wasUsing = false;
        wasBlocked = false;
        return;
    }
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(cursor.x, cursor.y, avail.x, avail.y);
    ImGuizmo::SetOrthographic(app.camera.orthographic);
    Mat4 parentG = Mat4::identity();
    if (mb->parentId != kNoParent) {
        if (const Bone* pb = ga->skeleton.findById(static_cast<std::uint32_t>(mb->parentId)))
            parentG = pb->globalTransform;
    }
    const Vec3 boneWorldPos{mb->globalTransform.m[3][0], mb->globalTransform.m[3][1],
                            mb->globalTransform.m[3][2]};
    const bool parentSpace = app.gizmoSpace == GizmoSpace::Parent;
    // Parent space: handles aligned to the parent orientation at the joint
    // (LOCAL mode on a parent-aligned draw matrix). World/Local: the bone's
    // own world matrix in WORLD/LOCAL mode. Output is always a world-space
    // matrix; the core decomposition below maps it back to locals.
    const Mat4 drawBefore =
        parentSpace ? parentAlignedDrawMatrix(parentG, boneWorldPos) : mb->globalTransform;
    const Mat4 vv = app.camera.viewMatrix();
    const Mat4 pp = app.camera.projMatrix(avail.x / avail.y);
    float view[16], proj[16], mtx[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            view[c * 4 + r] = vv.m[r][c];
            proj[c * 4 + r] = pp.m[r][c];
            mtx[c * 4 + r] = drawBefore.m[r][c];
        }
    }
    // Optional step snapping (world units / degrees / scale steps).
    float snapVals[3] = {0, 0, 0};
    float* snapPtr = nullptr;
    if (app.gizmoSnap) {
        if (app.gizmoOp == GizmoOp::Translate) {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapTranslate;
            snapPtr = snapVals;
        } else if (app.gizmoOp == GizmoOp::Rotate) {
            snapVals[0] = app.snapRotateDeg;
            snapPtr = snapVals;
        } else {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapScale;
            snapPtr = snapVals;
        }
    }
    
    // Enable plane handles and center handle for translate/scale
    // ImGuizmo automatically shows plane handles (squares between axes) for translate
    // and the center cube for all operations.
    ImGuizmo::Manipulate(view, proj,
                         app.gizmoOp == GizmoOp::Translate ? ImGuizmo::TRANSLATE
                         : app.gizmoOp == GizmoOp::Rotate  ? ImGuizmo::ROTATE
                                                           : ImGuizmo::SCALE,
                         parentSpace || app.gizmoSpace == GizmoSpace::Local ? ImGuizmo::LOCAL
                                                                            : ImGuizmo::WORLD,
                         mtx, nullptr, snapPtr);
    gizmoUsing = ImGuizmo::IsUsing();
    gizmoOver = ImGuizmo::IsOver();
    const bool locked = app.isBoneLocked(mb->id);
    if (gizmoUsing && !wasUsing) {
        if (locked) {
            wasBlocked = true;
            app.setStatus("Gizmo blocked: bone is locked.", "warning");
        } else {
            wasBlocked = false;
            app.pushUndoSnapshot("gizmo " + mb->name);
        }
    }
    if (gizmoUsing) {
        if (locked) {
            // Status already set on drag start; avoid per-frame spam.
        } else {
            Mat4 outW;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) outW.m[r][c] = mtx[c * 4 + r];
            const LocalEditOp leOp = app.gizmoOp == GizmoOp::Rotate ? LocalEditOp::Rotate
                                     : app.gizmoOp == GizmoOp::Scale ? LocalEditOp::Scale
                                                                     : LocalEditOp::Translate;
            const BoneLocalEdit current{mb->localPosition, mb->localRotationEuler,
                                        mb->localScale};
            if (!parentSpace) {
                // World/Local: decompose the manipulated world matrix through
                // the single core implementation (shared with tests).
                const BoneLocalEdit edit = decomposeWorldToLocal(parentG, outW, leOp, current);
                mb->localPosition = edit.position;
                mb->localRotationEuler = edit.rotationEuler;
                mb->localScale = edit.scale;
            } else if (app.gizmoOp == GizmoOp::Translate) {
                // Parent translate: world delta back through the parent frame.
                Mat4 movedW = mb->globalTransform;
                movedW.m[3][0] = outW.m[3][0];
                movedW.m[3][1] = outW.m[3][1];
                movedW.m[3][2] = outW.m[3][2];
                const BoneLocalEdit edit =
                    decomposeWorldToLocal(parentG, movedW, LocalEditOp::Translate, current);
                mb->localPosition = edit.position;
            } else if (app.gizmoOp == GizmoOp::Rotate) {
                // Parent rotate: draw-matrix delta premultiplies the local
                // rotation (rotation about parent axes).
                mb->localRotationEuler =
                    applyParentRotationDelta(parentG, outW, mb->localRotationEuler);
            } else {
                // Parent scale: parent-axis step ratios onto local scale
                // (approximate under non-uniform parent scale, disclosed in
                // the Bone panel tooltip).
                const Vec3 ratios = drawScaleRatios(drawBefore, outW);
                mb->localScale = {mb->localScale.x * ratios.x, mb->localScale.y * ratios.y,
                                  mb->localScale.z * ratios.z};
            }
            if (auto rr = rebuildSkeletonRuntime(ga->skeleton); !rr)
                app.setStatus("Gizmo update failed: " + rr.error().message, "error");
            ga->gpuDirty = true;
            ga->dirty = true;
        }
    }
    if (!gizmoUsing && wasUsing) {
        if (wasBlocked) {
            app.setStatus("Gizmo blocked: bone is locked.", "warning");
        } else {
            app.runValidation();
            app.setStatus("Gizmo edit applied.", "success");
        }
        wasBlocked = false;
    }
    wasUsing = gizmoUsing;
}
#endif

void doImportSmd(App& app) {
    const DialogResult dlg =
        openFileDialog(g_mainWindow, "Import SMD model", "SMD files (*.smd)|*.smd|All files (*.*)|*.*", "smd");
    if (!dlg.confirmed) return;
    if (auto r = app.importSmdFile(dlg.path); !r)
        app.setStatus("SMD import failed: " + r.error().message, "error");
}

void doExportSmd(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export SMD model",
                                            "SMD files (*.smd)|*.smd", "smd", a->id + ".smd");
    if (!dlg.confirmed) return;
    if (auto r = app.exportSmdFile(dlg.path); !r)
        app.setStatus("SMD export failed: " + r.error().message, "error");
}

void doImportBridged(App& app) {
    if (app.bridgeBusy) {
        app.setStatus("A bridge import is already running.", "warning");
        return;
    }
    const DialogResult dlg = openFileDialog(
        g_mainWindow, "Import model (SMD/FBX/GR2 via bridge)",
        "Supported (*.smd;*.fbx;*.gr2)|*.smd;*.fbx;*.gr2|SMD (*.smd)|*.smd|FBX (*.fbx)|*.fbx|GR2 "
        "(*.gr2)|*.gr2|All (*.*)|*.*",
        "");
    if (!dlg.confirmed) return;
    app.startBridgedImport(dlg.path, ImGui::GetTime());
}

// Native glTF import via App::importGltfFile (CLI-chain parity, fail-closed):
// the file dialog mirrors doImportBridged (*.glb + *.gltf filter); statuses
// come from the method itself (installConverted success / honest failure),
// none duplicated here. Disabled while bridgeBusy at both call sites (same
// gate as the Import FBX/GR2 button).
void doImportGltf(App& app) {
    const DialogResult dlg = openFileDialog(
        g_mainWindow, "Import glTF model",
        "glTF (*.glb;*.gltf)|*.glb;*.gltf|Binary glTF (*.glb)|*.glb|glTF JSON (*.gltf)|*.gltf|All "
        "(*.*)|*.*",
        "");
    if (!dlg.confirmed) return;
    app.importGltfFile(dlg.path);
}

void doExportMsm(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export MSM shape",
                                            "MSM files (*.msm)|*.msm", "msm", a->id + ".msm");
    if (!dlg.confirmed) return;
    if (auto r = app.exportMsmFile(dlg.path); !r)
        app.setStatus("MSM export failed: " + r.error().message, "error");
}

void doExportGr2(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export GR2 (external bridge)",
                                            "GR2 files (*.gr2)|*.gr2", "gr2", a->id + ".gr2");
    if (!dlg.confirmed) return;
    if (auto r = app.exportGr2Bridge(dlg.path); !r)
        app.setStatus("GR2 export: " + r.error().message, "warning");
}

void doExportGr2ToFbx(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export GR2 as FBX (Noesis)",
                                            "FBX files (*.fbx)|*.fbx", "fbx", a->id + ".fbx");
    if (!dlg.confirmed) return;
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    if (!std::filesystem::exists(cfg.noesisCliPath)) {
        app.setStatus("Noesis not found — cannot convert GR2 to FBX.", "error");
        return;
    }
    app.setStatus("Converting GR2 to FBX via Noesis (rotate 90 0 0)...", "info");
    if (auto r = convertGr2ToFbxViaNoesis(std::filesystem::path(a->sourcePath), dlg.path, cfg); !r) {
        app.setStatus("GR2->FBX failed: " + r.error().message, "error");
    } else {
        app.setStatus("GR2 converted to FBX: " + dlg.path, "success");
    }
}

void doExportFbx(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export FBX (Noesis)",
                                            "FBX files (*.fbx)|*.fbx", "fbx", a->id + ".fbx");
    if (!dlg.confirmed) return;
    if (auto r = app.exportFbxFile(dlg.path); !r)
        app.setStatus("FBX export failed: " + r.error().message, "error");
}

void doExportAni(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    if (a->clip.keys.empty()) {
        app.setStatus("No animation keys to export. Add keyframes first.", "warning");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export ANI (Metin2 Animation)",
                                            "ANI files (*.ani)|*.ani", "ani", a->id + ".ani");
    if (!dlg.confirmed) return;
    if (auto r = app.exportAniFile(dlg.path); !r)
        app.setStatus("ANI export failed: " + r.error().message, "error");
}

void doExportLod(App& app, float ratio) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export LOD SMD model",
                                            "SMD files (*.smd)|*.smd", "smd", a->id + "_lod.smd");
    if (!dlg.confirmed) return;
    Mesh lodMesh = a->mesh;  // decimate a copy; the live session is untouched
    LodOptions opt;
    opt.targetRatio = ratio;
    auto lod = decimateMesh(lodMesh, opt);
    if (!lod) {
        app.setStatus("LOD failed: " + lod.error().message, "error");
        return;
    }
    RepairStats rs = repairMeshWeights(lodMesh, a->skeleton.bones.size());
    (void)rs;
    auto written = writeSmd(lodMesh, a->skeleton, a->animFrames);
    if (!written) {
        app.setStatus("LOD export failed: " + written.error().message, "error");
        return;
    }
    if (auto w = writeTextFile(dlg.path, written.value().text, a->id); !w) {
        app.setStatus("LOD export failed: " + w.error().message, "error");
        return;
    }
    app.setStatus("Exported LOD " + dlg.path + " (" + lod.value().toDisplayString() + ").",
                  "success");
}

void doSaveWorkspace(App& app) {
    const DialogResult dlg =
        saveFileDialog(g_mainWindow, "Save workspace", "Workspace (*.m2rig)|*.m2rig", "m2rig",
                       (app.current.empty() ? "project" : app.current) + ".m2rig");
    if (!dlg.confirmed) return;
    if (auto r = app.saveWorkspaceFile(dlg.path); !r)
        app.setStatus("Workspace save failed: " + r.error().message, "error");
}

void doLoadWorkspace(App& app) {
    const DialogResult dlg = openFileDialog(g_mainWindow, "Load workspace",
                                            "Workspace (*.m2rig)|*.m2rig|All (*.*)|*.*", "m2rig");
    if (!dlg.confirmed) return;
    if (auto r = app.loadWorkspaceFile(dlg.path); !r)
        app.setStatus("Workspace load failed: " + r.error().message, "error");
}

void doOpenMsm(App& app) {
    const DialogResult dlg = openFileDialog(g_mainWindow, "Inspect MSM shape",
                                            "MSM files (*.msm)|*.msm|All (*.*)|*.*", "msm");
    if (!dlg.confirmed) return;
    if (auto r = app.openMsmInspector(dlg.path); !r)
        app.setStatus("MSM open failed: " + r.error().message, "error");
}

// --- Command palette (Ctrl+K) -----------------------------------------------
// Routes ONLY to existing handlers (no forks): every run row names the reused
// function in its trailing comment. Disabled logic reuses each button's own
// gate (Frame/currentAsset, Undo/canUndo, Redo/canRedo, bridge/bridgeBusy,
// exports/currentAsset, Auto-rig/currentAsset) — never re-derived.
// Fuzzy: case-insensitive ASCII substring over the action name; rank score =
// (earliest-match-index, shortest-name); remaining ties keep table order via
// a stable insertion pass (deterministic, single-threaded, no parallelism).
// Empty query lists the full table in declared order.
struct CmdPaletteAction {
    const char* name;
    const char* hint;
    bool (*enabled)(const App&);
    void (*run)(App&);
    const char* disabledReason;  // shown greyed when !enabled; nullptr when n/a
};

constexpr std::size_t kCmdPalMax = 32;  // fixed rank buffer; see static_assert below

inline bool cmdPalAlways(const App& app) {
    (void)app;
    return true;
}
inline bool cmdPalHasAsset(const App& app) {
    return app.currentAsset() != nullptr;  // same gate as the toolbar Frame button
}
inline bool cmdPalCanUndo(const App& app) {
    return app.canUndo();  // same gate as the toolbar/menu Undo item
}
inline bool cmdPalCanRedo(const App& app) {
    return app.canRedo();  // same gate as the toolbar/menu Redo item
}
inline bool cmdPalBridgeFree(const App& app) {
    return !app.bridgeBusy;  // same gate as the Import FBX/GR2 button
}

inline char cmdPalLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

// Earliest case-insensitive substring position of needle in hay, or -1.
inline int cmdPalMatchPos(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return 0;
    if (needle.size() > hay.size()) return -1;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool hit = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (cmdPalLower(hay[i + j]) != cmdPalLower(needle[j])) {
                hit = false;
                break;
            }
        }
        if (hit) return static_cast<int>(i);
    }
    return -1;
}

const CmdPaletteAction kCmdPalette[] = {
    {"Frame all (F)", "Fit the whole model in view (F)", cmdPalHasAsset,
     [](App& app) { frameWholeModel(app, false); },  // reuses frameWholeModel
     "no asset"},
    {"Run validation", "Run the full compatibility check", cmdPalAlways,
     [](App& app) { app.runValidation(); },  // reuses App::runValidation
     nullptr},
    {"Undo (Ctrl+Z)", "Undo last change (Ctrl+Z)", cmdPalCanUndo,
     [](App& app) { app.undo(); },  // reuses App::undo
     "nothing to undo"},
    {"Redo (Ctrl+Y)", "Redo undone change (Ctrl+Y)", cmdPalCanRedo,
     [](App& app) { app.redo(); },  // reuses App::redo
     "nothing to redo"},
    {"Toggle Deform preview (D)", "Preview deformed mesh (D)", cmdPalAlways,
     [](App& app) {  // reuses the View-menu/toolbar Deform preview assignment
         app.previewDeform = !app.previewDeform;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"Toggle Textured (T)", "Sample first-material DDS texture (T)", cmdPalAlways,
     [](App& app) {  // reuses the View-menu/toolbar Textured assignment
         app.textured = !app.textured;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"Toggle PBR shading", "Cook-Torrance PBR for Solid modes", cmdPalAlways,
     [](App& app) { app.usePbr = !app.usePbr; },  // reuses the View-menu/toolbar PBR assignment
     nullptr},
    {"View: Solid (1)", "Solid (1)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::Solid;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"View: Wireframe (2)", "Wireframe (2)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::Wireframe;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"View: Solid + Wire (3)", "Solid + Wire (3)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::SolidWireframe;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"View: Normals (4)", "Normals (4)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::Normals;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"View: Height (5)", "Height (5)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::Height;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"View: Weights (6)", "Weights heatmap - needs a selected bone (6)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::Weights;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"View: UV (7)", "UV checker (7)", cmdPalAlways,
     [](App& app) {  // reuses the toolbar drawViewModeSegmented radio assignment
         app.viewMode = ViewMode::UV;
         if (LoadedAsset* m = app.currentAsset()) m->gpuDirty = true;
     },
     nullptr},
    {"Camera: Front", "Snap camera to front (keeps framing)", cmdPalAlways,
     [](App& app) {
         app.camera.applyPreset(CameraPreset::Front, ImGui::GetTime());
     },  // reuses the overlay Front applyPreset call
     nullptr},
    {"Camera: Back", "Snap camera to back (keeps framing)", cmdPalAlways,
     [](App& app) {
         app.camera.applyPreset(CameraPreset::Back, ImGui::GetTime());
     },  // reuses the overlay Back applyPreset call
     nullptr},
    {"Camera: Top", "Snap camera to top (keeps framing)", cmdPalAlways,
     [](App& app) {
         app.camera.applyPreset(CameraPreset::Top, ImGui::GetTime());
     },  // reuses the overlay Top applyPreset call
     nullptr},
    {"Camera: Bottom", "Snap camera to bottom (keeps framing)", cmdPalAlways,
     [](App& app) {
         app.camera.applyPreset(CameraPreset::Bottom, ImGui::GetTime());
     },  // reuses the overlay Bottom applyPreset call
     nullptr},
    {"Camera: Left", "Snap camera to left (keeps framing)", cmdPalAlways,
     [](App& app) {
         app.camera.applyPreset(CameraPreset::Left, ImGui::GetTime());
     },  // reuses the overlay Left applyPreset call
     nullptr},
    {"Camera: Right", "Snap camera to right (keeps framing)", cmdPalAlways,
     [](App& app) {
         app.camera.applyPreset(CameraPreset::Right, ImGui::GetTime());
     },  // reuses the overlay Right applyPreset call
     nullptr},
    {"Import SMD...", "Import SMD model", cmdPalAlways,
     [](App& app) { doImportSmd(app); },  // reuses doImportSmd
     nullptr},
    {"Import FBX/GR2 (bridge)...", "Import model via bridge", cmdPalBridgeFree,
     [](App& app) { doImportBridged(app); },  // reuses doImportBridged
     "bridge busy"},
    {"Import glTF...", "Import glTF model (.glb/.gltf)", cmdPalBridgeFree,
     [](App& app) { doImportGltf(app); },  // reuses doImportGltf
     "bridge busy"},
    {"Export SMD...", "Write skeletal mesh, re-validated, gate enforced", cmdPalHasAsset,
     [](App& app) { doExportSmd(app); },  // reuses doExportSmd
     "no asset"},
    {"Export MSM...", "Write Metin2 mesh groups, re-validated, gate enforced", cmdPalHasAsset,
     [](App& app) { doExportMsm(app); },  // reuses doExportMsm
     "no asset"},
    {"Export LOD SMD...", "Decimate a COPY to the keep ratio", cmdPalHasAsset,
     [](App& app) { doExportLod(app, app.prefs.lodRatio); },  // reuses doExportLod
     "no asset"},
    {"Export all loaded (SMD+MSM)...", "Export every loaded asset to SMD+MSM", cmdPalHasAsset,
     [](App& app) {  // reuses the Project-menu Export-all-batch call
         if (auto r = app.exportAllBatch(); !r)
             app.setStatus("Batch failed: " + r.error().message, "error");
     },
     "no asset"},
    {"Auto-rig from skeleton", "Binds every vertex to nearest bones (undoable)", cmdPalHasAsset,
     [](App& app) {  // reuses the toolbar/Weights-panel Auto-rig call
         if (auto r = app.autoRigFromSkeleton(); !r)
             app.setStatus("Auto-rig failed: " + r.error().message, "error");
     },
     "no asset"},
};

static_assert(sizeof(kCmdPalette) / sizeof(kCmdPalette[0]) <= kCmdPalMax,
              "command palette table exceeds the fixed rank buffer");

// Centered Ctrl+K popup (own ID "cmd_palette"): autofocus filter input +
// fuzzy-substring rank over kCmdPalette; Enter/click runs the row's existing
// handler. Disabled rows render greyed with their reason and never run.
// Opens only via the appOwnsKeyboard-gated Ctrl+K hook in drawAllPanels;
// Esc closes (explicit below; ImGui also dismisses popups on Esc).
void drawCommandPalette(App& app) {
    static char s_filter[128] = "";
    static int s_selected = 0;
    static bool s_wasOpen = false;
    const bool isOpen = ImGui::IsPopupOpen("cmd_palette", ImGuiPopupFlags_None);
    const bool justOpened = isOpen && !s_wasOpen;
    if (justOpened) {
        s_filter[0] = '\0';
        s_selected = 0;
    }
    if (!isOpen) {
        s_wasOpen = false;
        return;
    }
    const ImGuiIO& palIo = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(palIo.DisplaySize.x * 0.5f, palIo.DisplaySize.y * 0.30f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 400.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("cmd_palette")) {
        s_wasOpen = true;
        return;
    }
    if (justOpened) ImGui::SetKeyboardFocusHere();
    bool runSelected = false;
    if (ImGui::InputTextWithHint("##cmd_filter", "Type a command... (Esc closes)", s_filter,
                                 sizeof(s_filter), ImGuiInputTextFlags_EnterReturnsTrue))
        runSelected = true;
    // Ranked index list over the static table (fixed buffers, no allocation).
    int order[kCmdPalMax] = {};
    int matchAt[kCmdPalMax] = {};
    int count = 0;
    {
        const std::string_view needle(s_filter);
        constexpr std::size_t kCmdCount = sizeof(kCmdPalette) / sizeof(kCmdPalette[0]);
        for (std::size_t i = 0; i < kCmdCount; ++i) {
            const int at = cmdPalMatchPos(kCmdPalette[i].name, needle);
            if (at < 0) continue;
            if (count >= static_cast<int>(kCmdPalMax)) break;
            order[count] = static_cast<int>(i);
            matchAt[count] = at;
            ++count;
        }
        // Stable insertion sort by (earliest-match-index, shortest-name);
        // full ties keep table order (deterministic).
        for (int i = 1; i < count; ++i) {
            const int oi = order[i];
            const int pi = matchAt[i];
            const std::size_t li =
                std::string_view(kCmdPalette[static_cast<std::size_t>(oi)].name).size();
            int j = i - 1;
            while (j >= 0) {
                const int oj = order[j];
                const int pj = matchAt[j];
                const std::size_t lj =
                    std::string_view(kCmdPalette[static_cast<std::size_t>(oj)].name).size();
                if (pj > pi || (pj == pi && lj > li)) {
                    order[j + 1] = oj;
                    matchAt[j + 1] = pj;
                    --j;
                } else {
                    break;
                }
            }
            order[j + 1] = oi;
            matchAt[j + 1] = pi;
        }
    }
    if (count == 0) {
        s_selected = 0;
    } else {
        if (s_selected >= count) s_selected = count - 1;
        if (s_selected < 0) s_selected = 0;
    }
    if (count > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) --s_selected;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++s_selected;
        if (s_selected >= count) s_selected = count - 1;
        if (s_selected < 0) s_selected = 0;
    }
    if (ImGui::BeginChild("cmd_list", ImVec2(0, 0), true)) {
        if (count == 0) {
            ImGui::TextDisabled("No matches.");
        }
        for (int r = 0; r < count; ++r) {
            const CmdPaletteAction& act = kCmdPalette[static_cast<std::size_t>(order[r])];
            const bool en = act.enabled(app);
            char row[256];
            if (en) {
                std::snprintf(row, sizeof(row), "%s  |  %s", act.name, act.hint);
            } else {
                std::snprintf(row, sizeof(row), "%s  |  %s (%s)", act.name, act.hint,
                              act.disabledReason != nullptr ? act.disabledReason : "unavailable");
            }
            ImGui::BeginDisabled(!en);
            if (ImGui::Selectable(row, r == s_selected)) {
                s_selected = r;
                if (en) {
                    act.run(app);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::EndChild();
    if (runSelected && count > 0) {
        const CmdPaletteAction& act = kCmdPalette[static_cast<std::size_t>(order[s_selected])];
        if (act.enabled(app)) {
            act.run(app);
            ImGui::CloseCurrentPopup();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    s_wasOpen = true;
}

void drawMsmNode(const MsmNode& node) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    char label[256];
    std::snprintf(label, sizeof(label), "%s  [#%u]", node.name.c_str(), node.lineNumber);
    const bool open = ImGui::TreeNodeEx(label, flags);
    for (const auto& [key, val] : node.attributes)
        ImGui::TextDisabled("  %s = %s", key.c_str(), val.c_str());
    if (open) {
        for (const auto& child : node.children) drawMsmNode(child);
        ImGui::TreePop();
    }
}

void drawMsmInspector(App& app) {
    ImGui::Text("MSM Inspector");
    ImGui::Separator();
    if (ImGui::Button("Open .msm...")) doOpenMsm(app);
    if (!app.msmDoc) {
        ImGui::SameLine();
        ImGui::TextDisabled("No MSM loaded (reference view only).");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) app.closeMsmInspector();
    ImGui::TextDisabled("%s", app.msmPath.c_str());
    ImGui::Text("%s", app.msmReport.summaryLine().c_str());
    if (ImGui::BeginChild("msm_tree", ImVec2(0, 0), true)) drawMsmNode(app.msmDoc->root);
    ImGui::EndChild();
}

// --- MSE Effects preview (Wave-30b, panels-local state) ----------------------
// App carries no MSE fields (headers are out of scope for this slice), so the
// effect document + transport live here as TU-local state, mirroring the MSM
// Inspector tab pattern (doOpenMsm/drawMsmInspector above).
// MseRuntime::update had zero callers before this slice (repo-wide grep finds
// only the mse.cpp definition); the per-frame tick in drawSceneContents below
// is its first caller.
MseRuntime g_mse;
std::string g_msePath;
std::size_t g_mseEmitterCount = 0;
bool g_mseTabEnabled = true;  // View > Panels > "MSE Effects tab" toggle
bool g_msePreview = true;     // in-tab "Preview in viewport" checkbox
bool g_msePlaying = false;
float g_mseTime = 0.0f;
float g_mseDuration = 5.0f;

float mseEffectDuration(const MseDocument& doc) {
    float dur = 0.0f;
    for (const auto& att : doc.attachments)
        for (const auto& em : att.emitters)
            if (std::isfinite(em.lifeTime) && em.lifeTime > dur) dur = em.lifeTime;
    // Global loop clock for the time slider; per-emitter loop flags are
    // approximated as loop (documented at the tick). Zero-emitter or
    // degenerate documents still get a scrubable range.
    if (!std::isfinite(dur) || dur <= 0.0f) return 5.0f;
    return dur > 600.0f ? 600.0f : dur;
}

void doOpenMse(App& app) {
    const DialogResult dlg = openFileDialog(g_mainWindow, "Open MSE effect",
                                            "MSE files (*.mse)|*.mse|All (*.*)|*.*", "mse");
    if (!dlg.confirmed) return;
    std::ifstream file(dlg.path, std::ios::binary);
    if (!file.is_open()) {
        app.setStatus("MSE open failed: cannot open file", "error");
        return;
    }
    std::ostringstream textStream;
    textStream << file.rdbuf();
    auto parsed = parseMse(textStream.str(), dlg.path);
    if (!parsed) {
        g_mse = MseRuntime{};
        g_msePath.clear();
        g_mseEmitterCount = 0;
        g_msePlaying = false;
        g_mseTime = 0.0f;
        app.setStatus("MSE open failed: " + parsed.error().message, "error");
        return;
    }
    g_mse = MseRuntime{};
    g_mse.doc = std::move(parsed.value());
    g_msePath = dlg.path;
    g_mseEmitterCount = 0;
    for (const auto& att : g_mse.doc.attachments) g_mseEmitterCount += att.emitters.size();
    g_mseDuration = mseEffectDuration(g_mse.doc);
    g_mseTime = 0.0f;
    g_msePlaying = true;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "MSE loaded: %zu attachment(s), %zu emitter(s) — preview in Tools > MSE Effects.",
                  g_mse.doc.attachments.size(), g_mseEmitterCount);
    app.setStatus(buf, g_mse.doc.attachments.empty() ? "warning" : "success");
}

void drawMseEffects(App& app) {
    ImGui::Text("MSE Effects");
    ImGui::Separator();
    if (ImGui::Button("Open .mse...")) doOpenMse(app);
    if (g_msePath.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("No MSE loaded (effect preview only).");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        g_mse = MseRuntime{};
        g_msePath.clear();
        g_mseEmitterCount = 0;
        g_msePlaying = false;
        g_mseTime = 0.0f;
        app.setStatus("MSE closed.", "info");
    }
    ImGui::TextDisabled("%s", g_msePath.c_str());
    ImGui::Text("%zu attachment(s), %zu emitter(s)", g_mse.doc.attachments.size(),
                g_mseEmitterCount);
    ImGui::Checkbox("Preview in viewport", &g_msePreview);
    ImGui::BeginDisabled(g_mse.doc.attachments.empty());
    if (ImGui::Button(g_msePlaying ? "Pause" : "Play")) g_msePlaying = !g_msePlaying;
    ImGui::SameLine();
    ImGui::SliderFloat("Time", &g_mseTime, 0.0f, g_mseDuration, "%.2f s");
    ImGui::EndDisabled();
    if (g_mseTime < 0.0f) g_mseTime = 0.0f;
    if (g_mseTime > g_mseDuration) g_mseTime = g_mseDuration;
    if (!app.currentAsset())
        ImGui::TextDisabled("Load a model to preview particles in the viewport.");
    else if (!g_msePreview)
        ImGui::TextDisabled("Preview disabled — tick the checkbox to draw particles.");
    else if (g_mse.doc.attachments.empty())
        ImGui::TextDisabled("Document holds no attachments — nothing to preview.");
    if (ImGui::BeginChild("mse_tree", ImVec2(0, 0), true)) {
        for (std::size_t ai = 0; ai < g_mse.doc.attachments.size(); ++ai) {
            const MseAttachment& att = g_mse.doc.attachments[ai];
            ImGui::PushID(static_cast<int>(ai));
            char label[192];
            std::snprintf(label, sizeof(label), "%s  (%zu emitter%s)", att.boneName.c_str(),
                          att.emitters.size(), att.emitters.size() == 1 ? "" : "s");
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (att.emitters.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx(label, flags);
            if (open) {
                for (std::size_t ei = 0; ei < att.emitters.size(); ++ei) {
                    const MseEmitter& em = att.emitters[ei];
                    ImGui::PushID(static_cast<int>(ei));
                    ImGui::Text("%s", em.name.c_str());
                    ImGui::TextDisabled("type=%s rate=%.1f/s life=%.2fs size=%.2f->%.2f",
                                        em.type.c_str(), em.emitRate, em.lifeTime, em.startSize,
                                        em.endSize);
                    if (!em.texturePath.empty())
                        ImGui::TextDisabled("texture=%s", em.texturePath.c_str());
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

// --- Per-submesh textured routing (uploads live in refreshGpu) -------------
// All 12 range variants exist (renderer.hpp:180-210, Slice C + C2). Routing
// stays intentionally static-textured-only (Blinn) plus textured-PBR below:
// skinned-Blinn, flat-debug and untextured modes keep their whole-draw calls
// (per-mode range expansion is mechanical from here), and only the static
// textured + textured-PBR paths go per-submesh.
bool meshHasDistinctVisibleMaterials(const Mesh& mesh, const std::set<std::size_t>& hidden) {
    // Any hiding compacts the uploaded index buffer (filterVisibleIndices),
    // so original startIndex/count ranges no longer address it — hidden state
    // forces the whole-draw path (today's exact behavior, zero risk).
    if (!hidden.empty()) return false;
    if (mesh.subMeshes.size() < 2) return false;
    std::set<std::uint32_t> seen;
    for (const SubMesh& sm : mesh.subMeshes) {
        if (sm.indexCount == 0) continue;  // degenerate: the renderer skips it
        seen.insert(sm.materialIndex);
        if (seen.size() >= 2) return true;
    }
    return false;
}

int drawTexturedSubmeshRanges(Renderer& renderer, LoadedAsset& asset,
                              const std::set<std::size_t>& hidden, const Mat4& viewProj,
                              FillMode fill) {
    int calls = 0;
    // Single-texture fallback binding: reproduces exactly what refreshGpu left
    // behind (materials[0] upload under the asset id, or nothing when no DDS
    // resolved — then each range takes the honest untextured fallback flagged
    // in the status bar, same as the whole-draw path).
    const std::string fallback = renderer.hasTexture(asset.id) ? asset.id : std::string{};
    constexpr std::uint32_t kMaxRange = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t i = 0; i < asset.mesh.subMeshes.size(); ++i) {
        if (hidden.count(i) != 0) continue;  // belt-and-braces: caller pre-filters
        const SubMesh& sm = asset.mesh.subMeshes[i];
        if (sm.indexCount == 0 || sm.startIndex > kMaxRange) continue;  // renderer would skip
        const std::uint32_t count = sm.indexCount > kMaxRange
                                        ? kMaxRange
                                        : static_cast<std::uint32_t>(sm.indexCount);
        // Per-material lookup at draw time ONLY via hasTexture: the uploader
        // (refreshGpu in app_gpu.cpp, NOT this file) owns uploads under
        // <assetId>#mat<i>, so a key that was never uploaded (or was
        // released after its path went stale) simply misses here — no
        // per-frame decode, no fake success.
        const std::string matKey = asset.id + "#mat" + std::to_string(sm.materialIndex);
        renderer.setActiveTexture(renderer.hasTexture(matKey) ? matKey : fallback);
        renderer.drawMeshTexturedRange(asset.id, static_cast<std::uint32_t>(sm.startIndex),
                                       count, viewProj, fill);
        ++calls;
    }
    renderer.setActiveTexture(fallback);  // restore single-texture state for later passes
    return calls;
}

// First non-degenerate submesh material (whole-draw PBR normal binding).
// Falls back to material 0 for meshes without submeshes; indices beyond the
// uploaded keys simply miss the hasTexture probe below.
std::uint32_t primaryMaterialIndex(const Mesh& mesh) {
    for (const SubMesh& sm : mesh.subMeshes)
        if (sm.indexCount != 0) return sm.materialIndex;
    return 0;
}

// Binds the uploaded LINEAR normal map for one material index. Returns true
// when a key was bound (caller must clearPbrNormalMap after the draw);
// missing keys bind nothing, so the textured-PBR draw stays on PsTexPbr
// byte-identically (resolvePbrNormal would ignore them anyway).
bool bindPbrNormalForMaterial(Renderer& renderer, const LoadedAsset& asset,
                              std::uint32_t materialIndex) {
    const std::string key = asset.id + "#nmat" + std::to_string(materialIndex);
    if (!renderer.hasTexture(key)) return false;
    renderer.bindPbrNormalMap(key);
    return true;
}

// Per-submesh textured-PBR routing (static + skinned): mirrors
// drawTexturedSubmeshRanges — per-range albedo binding plus a per-range
// normal-map bind (missing key = unbound = PsTexPbr for that range only).
// Both bindings are restored after the loop.
int drawTexturedPbrSubmeshRanges(Renderer& renderer, LoadedAsset& asset,
                                 const std::set<std::size_t>& hidden, const Mat4& viewProj,
                                 FillMode fill, bool skinned) {
    int calls = 0;
    const std::string fallback = renderer.hasTexture(asset.id) ? asset.id : std::string{};
    for (std::size_t i = 0; i < asset.mesh.subMeshes.size(); ++i) {
        if (hidden.count(i) != 0) continue;  // belt-and-braces: caller pre-filters
        const SubMesh& sm = asset.mesh.subMeshes[i];
        if (sm.indexCount == 0) continue;  // renderer would skip
        const std::string matKey = asset.id + "#mat" + std::to_string(sm.materialIndex);
        renderer.setActiveTexture(renderer.hasTexture(matKey) ? matKey : fallback);
        const std::string nKey = asset.id + "#nmat" + std::to_string(sm.materialIndex);
        renderer.bindPbrNormalMap(renderer.hasTexture(nKey) ? nKey : std::string{});
        if (skinned)
            renderer.drawMeshTexturedSkinnedPbrRange(asset.id, sm.startIndex, sm.indexCount,
                                                     viewProj, fill);
        else
            renderer.drawMeshTexturedPbrRange(asset.id, sm.startIndex, sm.indexCount, viewProj,
                                              fill);
        ++calls;
    }
    renderer.setActiveTexture(fallback);  // restore single-texture state for later passes
    renderer.clearPbrNormalMap();
    return calls;
}

// Closest mesh-surface point to a click ray (ray-triangle Moller). Returns
// false when nothing hit; used as the paint brush center.
bool pickMeshPoint(const App& app, float panelW, float panelH, float clickX, float clickY,
                   Vec3& outPoint) {
    const LoadedAsset* a = app.currentAsset();
    if (!a || panelW <= 0 || panelH <= 0 || a->mesh.indices.size() < 3) return false;
    const float aspect = panelW / panelH;
    const Mat4 invVp = (app.camera.viewMatrix() * app.camera.projMatrix(aspect)).inverseGeneral();
    const float nx = (clickX / panelW) * 2.0f - 1.0f;
    const float ny = 1.0f - (clickY / panelH) * 2.0f;
    const Vec3 ro = invVp.transformPoint({nx, ny, 0.0f});
    const Vec3 rf = invVp.transformPoint({nx, ny, 1.0f});
    const Vec3 rd = normalized(rf - ro);
    bool hit = false;
    float bestT = std::numeric_limits<float>::max();
    const auto& verts = a->mesh.vertices;
    for (std::size_t i = 0; i + 2 < a->mesh.indices.size(); i += 3) {
        const std::uint32_t i0 = a->mesh.indices[i], i1 = a->mesh.indices[i + 1],
                             i2 = a->mesh.indices[i + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;  // MESH_BAD_INDEX
        const Vec3& v0 = verts[i0].position;
        const Vec3& v1 = verts[i1].position;
        const Vec3& v2 = verts[i2].position;
        const Vec3 e1 = v1 - v0, e2 = v2 - v0;
        const Vec3 p = cross(rd, e2);
        const float det = dot(e1, p);
        if (std::fabs(det) < 1e-9f) continue;
        const float inv = 1.0f / det;
        const Vec3 tv = ro - v0;
        const float u = dot(tv, p) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const Vec3 q = cross(tv, e1);
        const float v = dot(rd, q) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = dot(e2, q) * inv;
        if (t > 0.0f && t < bestT) {
            bestT = t;
            hit = true;
        }
    }
    if (hit) outPoint = ro + rd * bestT;
    return hit;
}

// Viewport bone picking: nearest bone segment to the click ray, threshold in
// pixels converted to world units at the camera target depth.
int pickBoneAt(const App& app, float panelW, float panelH, float clickX, float clickY) {
    const LoadedAsset* a = app.currentAsset();
    if (!a || panelW <= 0 || panelH <= 0) return -1;
    const float aspect = panelW / panelH;
    const Mat4 invVp = (app.camera.viewMatrix() * app.camera.projMatrix(aspect)).inverseGeneral();
    const float nx = (clickX / panelW) * 2.0f - 1.0f;
    const float ny = 1.0f - (clickY / panelH) * 2.0f;
    const Vec3 pNear = invVp.transformPoint({nx, ny, 0.0f});
    const Vec3 pFar = invVp.transformPoint({nx, ny, 1.0f});
    const Vec3 dir = normalized(pFar - pNear);
    const float eyeDist = distance(app.camera.eye(), app.camera.target);
    float worldPerPixel = 0.01f;
    if (app.camera.orthographic) {
        worldPerPixel = app.camera.orthoHeight / panelH;
    } else {
        worldPerPixel =
            2.0f * eyeDist * std::tan(app.camera.fovY * 0.5f) / panelH;
    }
    const float threshold = worldPerPixel * 10.0f;

    int best = -1;
    float bestDist = threshold;
    for (const auto& b : a->skeleton.bones) {
        if (!app.boneVisible(b.id)) continue;
        const Vec3 joint{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                         b.globalTransform.m[3][2]};
        Vec3 segA = joint, segB = joint;
        if (b.parentId != kNoParent) {
            if (const Bone* p = a->skeleton.findById(static_cast<std::uint32_t>(b.parentId))) {
                segA = {p->globalTransform.m[3][0], p->globalTransform.m[3][1],
                        p->globalTransform.m[3][2]};
            }
        }
        const Vec3 e = segB - segA;
        const Vec3 w0 = pNear - segA;
        float d;
        if (dot(e, e) <= 1e-12f) {
            // Root/orphan joint: point-to-ray distance (roots are pickable).
            const float s = std::max(0.0f, dot(dir, joint - pNear));
            d = distance(pNear + dir * s, joint);
        } else {
            const float bb = dot(dir, e);
            const float ee = dot(e, e);
            const float c1 = dot(dir, w0);
            const float c2 = dot(e, w0);
            const float denom = ee - bb * bb;  // |dir| == 1
            float s = 0.0f, t = 0.0f;
            if (denom > 1e-12f) {
                t = (c2 - c1 * bb) / denom;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                s = t * bb - c1;
                if (s < 0.0f) s = 0.0f;
                t = (s * bb + c2) / ee;  // re-solve clamped
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                s = t * bb - c1;
                if (s < 0.0f) s = 0.0f;
            }
            const Vec3 closest = pNear + dir * s;
            const Vec3 onBone = segA + e * t;
            d = distance(closest, onBone);
        }
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(b.id);
        }
    }
    return best;
}

bool segmentedButton(const char* label, bool selected, const char* tooltip, bool disabled = false) {
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Header]);
    ImGui::BeginDisabled(disabled);
    const bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    if (selected) ImGui::PopStyleColor();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

void drawViewModeSegmented(App& app) {
    static const char* kLabels[7] = {"Solid", "Wire", "S+W", "Nrml", "Hght", "Wght", "UV"};
    static const char* kTips[7] = {"Solid (1)", "Wireframe (2)", "Solid + Wire (3)", "Normals (4)",
                                   "Height (5)", "Weights heatmap — needs a selected bone (6)",
                                   "UV checker (7)"};
    const bool noAsset = app.currentAsset() == nullptr;
    for (int i = 0; i < 7; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool sel = app.viewMode == static_cast<ViewMode>(i);
        if (segmentedButton(kLabels[i], sel, kTips[i], false)) {
            app.viewMode = static_cast<ViewMode>(i);
            if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            if (i == 5 && app.selectedBone < 0)
                app.setStatus("Weights view needs a selected bone — pick one in Skeleton or viewport.",
                              "warning");
        }
    }
    (void)noAsset;
}

void drawToolbar(App& app) {
    // Grouped toolbar with wrap (layout-perfect slice): five groups —
    // View (modes + Frame + Ortho) | Display (X-ray/Tex/PBR/Paint/Deform/DQS +
    // Grid/Bones/Wire ovl overlays, same App bools) | Rig (Auto-rig) |
    // Status (Undo/Redo/Validate/FPS) | Panels (">>" overflow popup).
    // Before each group (after the first), when CursorX + groupWidth exceeds
    // the toolbar width the group starts on a new line instead of clipping.
    // Group widths are measured constants (default style, default font):
    // View ~640 (7 mode buttons ~340 + Frame ~85 + Ortho ~120 + spacings),
    // Display ~620 (9 checkboxes), Rig ~110, Status ~300, Panels ~60.
    const float toolbarW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
    constexpr float kViewGroupW = 640.0f;
    constexpr float kDisplayGroupW = 620.0f;
    constexpr float kRigGroupW = 110.0f;
    constexpr float kStatusGroupW = 300.0f;
    constexpr float kPanelsGroupW = 60.0f;
    bool firstGroup = true;
    auto beginGroup = [&](float groupW) {
        if (firstGroup) {
            firstGroup = false;
            return;
        }
        if (ImGui::GetCursorPosX() + groupW > toolbarW) {
            ImGui::NewLine();
        } else {
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();
        }
    };
    // --- View: modes + Frame + Ortho --------------------------------------
    beginGroup(kViewGroupW);
    drawViewModeSegmented(app);
    ImGui::SameLine();
    ImGui::BeginDisabled(app.currentAsset() == nullptr);
    if (ImGui::Button("Frame (F)")) {
        frameWholeModel(app, false);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Fit the whole model in view (F)");
    ImGui::SameLine();
    const char* proj = app.camera.orthographic ? "Ortho -> Persp" : "Persp -> Ortho";
    if (ImGui::Button(proj)) app.camera.orthographic = !app.camera.orthographic;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle perspective / orthographic");
    // --- Display: X-ray/Tex/PBR/Paint/Deform/DQS + Grid/Bones/Wire ovl ----
    beginGroup(kDisplayGroupW);
    bool xray = app.xrayBones;
    if (ImGui::Checkbox("X-ray", &xray)) app.xrayBones = xray;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bones through mesh — default ON, needs Bones on (X)");
    ImGui::SameLine();
    if (ImGui::Checkbox("Tex", &app.textured)) {
        if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
        if (app.textured) {
            const LoadedAsset* ta = app.currentAsset();
            const bool hasDds = ta && !ta->mesh.materials.empty() &&
                                !ta->mesh.materials[0].texturePath.empty();
            if (!hasDds)
                app.setStatus("Tex on but no .dds found next to model / Data/Models — untextured.",
                              "warning");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sample first-material DDS texture (T) — needs .dds");
    ImGui::SameLine();
    if (ImGui::Checkbox("PBR", &app.usePbr))
        app.setStatus(app.usePbr ? "PBR shading on (Solid modes; debug ramps stay flat)."
                                 : "PBR shading off.",
                      "info");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cook-Torrance PBR for Solid modes");
    ImGui::SameLine();
    bool paint = app.paintMode;
    if (ImGui::Checkbox("Paint", &paint)) {
        app.paintMode = paint;
        if (paint && app.selectedBone < 0) {
            app.setStatus("Paint mode: select a bone first (click in Skeleton or viewport)", "warning");
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl/Shift+drag paints SELECTED bone (P) — select a bone first");
    ImGui::SameLine();
    if (ImGui::Checkbox("Deform", &app.previewDeform)) {
        if (LoadedAsset* pa = app.currentAsset()) pa->gpuDirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Preview deformed mesh (D)");
    ImGui::SameLine();
    if (ImGui::Checkbox("DQS", &app.useDqs)) {
        if (LoadedAsset* dq = app.currentAsset()) dq->gpuDirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Dual Quaternion Skinning -- avoids candy-wrapper artifacts on twist joints.\nRequires previewDeform=ON for animated deformation preview.");
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &app.showGrid);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ground grid + axes (G)");
    ImGui::SameLine();
    ImGui::Checkbox("Bones", &app.showBones);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Skeleton segments + joints (B)");
    ImGui::SameLine();
    ImGui::Checkbox("Wire ovl", &app.showWireOverlay);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Depth-biased wireframe overlay (W)");
    // --- Rig: Auto-rig ------------------------------------------------------
    beginGroup(kRigGroupW);
    // Quick auto-rig: same undoable op as the Weights-panel button, surfaced
    // where new users look first (single call site in App, no duplicate logic).
    // Red family like Flood/Prune: whole-mesh destructive (undoable).
    ImGui::BeginDisabled(app.currentAsset() == nullptr);
ImGui::PushStyleColor(ImGuiCol_Button, Theme::kDanger);
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kDangerHover);
ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kDangerActive);
    if (ImGui::Button("Auto-rig")) {
        if (auto r = app.autoRigFromSkeleton(); !r)
            app.setStatus("Auto-rig failed: " + r.error().message, "error");
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Auto-rig from skeleton: binds every vertex to nearest bones (undoable)");
    // --- Status: Undo/Redo/Validate/FPS ------------------------------------
    beginGroup(kStatusGroupW);
    ImGui::BeginDisabled(!app.canUndo());
    if (ImGui::Button("Undo")) app.undo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Undo last change (Ctrl+Z)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!app.canRedo());
    if (ImGui::Button("Redo")) app.redo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Redo undone change (Ctrl+Y)");
    ImGui::SameLine();
    if (ImGui::Button("Validate")) app.runValidation();
    tipFor("Run the full compatibility check (<=4 influences, skeleton, sockets, topology)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run compatibility + validation checks");
    ImGui::SameLine();
    ImGui::TextDisabled("FPS %.0f", app.fps);
    if (app.bridgeBusy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Running %s... %.0fs", app.bridgeJob.label.c_str(),
                            ImGui::GetTime() - app.bridgeJob.startTime);
    }
    // --- Panels: ">>" overflow popup (same App bools, no fork) -------------
    beginGroup(kPanelsGroupW);
    if (ImGui::Button(">>")) ImGui::OpenPopup("toolbar_panels_pop");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle panels");
    if (ImGui::BeginPopup("toolbar_panels_pop")) {
        auto panelBtn = [&](const char* label, const char* tooltip, bool* open) {
            if (ImGui::Button(label)) *open = !*open;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        };
        panelBtn("Bone", "Toggle Bone panel", &app.uiSettings.showBonePanel);
        panelBtn("Weights", "Toggle Weights panel", &app.uiSettings.showWeightsPanel);
        panelBtn("Materials", "Toggle Materials panel", &app.uiSettings.showMaterialsPanel);
        panelBtn("Export", "Toggle Export panel", &app.uiSettings.showExportPanel);
        panelBtn("Project", "Toggle Project panel", &app.uiSettings.showProjectPanel);
        panelBtn("BoneDisp", "Toggle Bone Display panel", &app.uiSettings.showBoneDisplayPanel);
        panelBtn("Gizmo", "Toggle Gizmo panel", &app.uiSettings.showGizmoPanel);
        ImGui::EndPopup();
    }
}

// Status bar at the bottom of the main window
void drawStatusBar(const App& app, const Renderer& renderer, const ViewportRect& rect) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 24));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 24));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 0));
    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        // Left: viewport status
        const char* rectStatus = rect.valid ? "VALID" : "INVALID";
        ImGui::Text("Viewport: %dx%d %s", rect.w, rect.h, rectStatus);
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Asset info
        if (const LoadedAsset* a = app.currentAsset()) {
            ImGui::Text("%s | %zu v / %zu t / %zu bones",
                        a->id.c_str(),
                        a->mesh.vertices.size(),
                        a->mesh.triangleCount(),
                        a->skeleton.bones.size());
            if (app.selectedBone >= 0) {
                if (const Bone* b = a->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone))) {
                    ImGui::SameLine();
                    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                    ImGui::SameLine();
                    ImGui::Text("Bone: %s [id %u]", b->name.c_str(), b->id);
                }
            }
        } else {
            ImGui::Text("No model loaded");
        }
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Render stats (priority collapse: dropped second when narrow).
        // Same right-align-guard pattern as the status message below, with
        // CalcTextSize-measured needs so the single-row bar never clips.
        auto stats = renderer.frameStats();
        {
            const float availAfterAsset = ImGui::GetContentRegionAvail().x;
            const float drawNeed =
                ImGui::CalcTextSize("Draw calls: 9999 (untextured fallback)").x + 40.0f;
            const float camNeed =
                ImGui::CalcTextSize("Eye: (0.0, 0.0, 0.0) Dist: 0.0").x + 40.0f;
            const float msgNeed =
                ImGui::CalcTextSize(app.statusMessage.c_str()).x + 20.0f;
            // Camera drops first, draw drops second (viewport + asset + status stay).
            const bool showCamera = availAfterAsset > drawNeed + camNeed + msgNeed + 80.0f;
            const bool showDraw = availAfterAsset > drawNeed + msgNeed + 80.0f;
            if (showDraw) {
                ImGui::Text("Draw calls: %d", stats.drawCalls);
                if (stats.texturedFallback) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "(untextured fallback)");
                }
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();
            }
            if (showCamera) {
                const Vec3 eye = app.camera.eye();
                ImGui::Text("Eye: (%.1f, %.1f, %.1f) Dist: %.1f", eye.x, eye.y, eye.z,
                            app.camera.distance);
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();
            }
            if (!showDraw) {
                (void)stats;
            }
        }

        // Status message (right-aligned)
        float avail = ImGui::GetContentRegionAvail().x;
        float msgWidth = ImGui::CalcTextSize(app.statusMessage.c_str()).x + 20;
        if (avail > msgWidth + 100) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - msgWidth);
        }
        ImVec4 color = Theme::statusColor(app.statusKind);
        ImGui::TextColored(color, "%s", app.statusMessage.empty() ? "Ready" : app.statusMessage.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// Toast notifications (bottom-right overlay)
void drawToasts(App& app) {
    if (app.toasts.empty()) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    double now = ImGui::GetTime();
    // Per-toast Y stacking above the 24px status bar: each toast gets its own
    // pivot (index * 70px) instead of sharing one pivot. Sticky-vs-expiring
    // contract below (until/dismiss/cleanup) is untouched.
    constexpr float kToastStackStep = 70.0f;
    std::size_t toastIndex = 0;
    for (auto it = app.toasts.rbegin(); it != app.toasts.rend(); ++it, ++toastIndex) {
        const float stackY =
            viewport->Pos.y + viewport->Size.y - 34.0f - static_cast<float>(toastIndex) * kToastStackStep;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 10, stackY),
                                ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowViewport(viewport->ID);
        const auto& toast = *it;
        ImGui::PushID(static_cast<int>(toast.id));
        const ImVec4 color = Theme::statusColor(toast.kind);

        if (ImGui::Begin(("##Toast" + std::to_string(toast.id)).c_str(), nullptr, flags)) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", toast.message.c_str());
            ImGui::PopStyleColor();
            if (!toast.sticky && ImGui::Button("Dismiss")) {
                // Mark for removal by setting expiry to past
                const_cast<App::Toast&>(toast).until = now - 1.0;
            }
        }
        ImGui::End();
        ImGui::PopID();
    }

    // Clean up expired toasts
    app.toasts.erase(std::remove_if(app.toasts.begin(), app.toasts.end(),
                                    [now](const App::Toast& t) {
                                        return !t.sticky && now >= t.until;
                                    }),
                     app.toasts.end());
}

void drawAssetsPanel(App& app) {
    {
        for (auto& kv : app.assets) {
            const bool sel = app.current == kv.first;
            if (ImGui::Selectable(kv.first.c_str(), sel)) {
                app.current = kv.first;
                app.selectedBone = -1;
                app.selectedBones.clear();
                app.hiddenBones.clear();
                app.soloBone = -1;
                app.selectedVertex = -1;
                app.hoveredBone = -1;
                app.lockedBones.clear();  // locks are per-asset, resolved by id
                app.hiddenSubmeshes.clear();
                app.clearUndoHistory();  // snapshots reference the previous mesh
                app.noteWeightsChanged();
                if (LoadedAsset* a = app.currentAsset()) {
                    a->gpuDirty = true;
                    app.camera.frameAabb(a->mesh.bounds);
                    char buf[192];
                    std::snprintf(buf, sizeof(buf), "Switched to %s (%zu verts, %zu bones).",
                                  a->id.c_str(), a->mesh.vertices.size(),
                                  a->skeleton.bones.size());
                    app.setStatus(buf, "info");
                }
                app.runValidation();
            }
        }
        // Import row with wrap: same buttons, flow to a new line when the
        // 230px column cannot fit the next label (no horizontal clip).
        const ImGuiStyle& assetStyle = ImGui::GetStyle();
        const float assetSectionW =
            ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        auto assetNeedW = [&](const char* label) -> float {
            return ImGui::CalcTextSize(label).x + assetStyle.FramePadding.x * 2.0f +
                   assetStyle.ItemSpacing.x;
        };
        bool assetFirst = true;
        auto assetFlow = [&](const char* label) {
            if (assetFirst) {
                assetFirst = false;
                return;
            }
            if (ImGui::GetCursorPosX() + assetNeedW(label) <= assetSectionW)
                ImGui::SameLine();
        };
        if (ImGui::Button("Load sample armor")) {
            if (auto r = app.loadSampleArmor(); !r)
                app.setStatus("Sample load failed: " + r.error().message, "error");
        }
        assetFlow("Sample as...");
        if (ImGui::Button("Sample as...")) ImGui::OpenPopup("sample_profile");
        if (ImGui::BeginPopup("sample_profile")) {
            for (const auto& p : allBuiltinProfiles()) {
                const std::string label = p.identity + " (" + p.race + "/" + p.gender + ")";
                if (ImGui::Selectable(label.c_str())) {
                    if (auto r = app.loadSampleArmorForProfile(p.identity); !r)
                        app.setStatus("Sample failed: " + r.error().message, "error");
                }
            }
            ImGui::EndPopup();
        }
        assetFlow("Import SMD...");
        if (ImGui::Button("Import SMD...")) doImportSmd(app);
        ImGui::BeginDisabled(app.bridgeBusy);
        assetFlow("Import FBX/GR2...");
        if (ImGui::Button("Import FBX/GR2...")) doImportBridged(app);
        assetFlow("Import glTF...");
        if (ImGui::Button("Import glTF...")) doImportGltf(app);
        tipFor("Native glTF import (.glb/.gltf, CLI-chain parity, fail-closed)");
        ImGui::EndDisabled();
        if (app.bridgeBusy)
            ImGui::TextDisabled("Running %s... %.0fs", app.bridgeJob.label.c_str(),
                                ImGui::GetTime() - app.bridgeJob.startTime);
        assetFlow("Validate");
        if (ImGui::Button("Validate")) app.runValidation();
        tipFor("Run the full compatibility check (<=4 influences, skeleton, sockets, topology)");
    }
}

void drawScenePanel(App& app) {
    {
        if (const LoadedAsset* a = app.currentAsset()) {
            ImGui::Text("%s", a->mesh.name.c_str());
            ImGui::TextDisabled("%zu verts / %zu tris", a->mesh.vertices.size(),
                                a->mesh.triangleCount());
            for (std::size_t i = 0; i < a->mesh.subMeshes.size(); ++i) {
                const auto& sm = a->mesh.subMeshes[i];
                const char* mat = sm.materialIndex < a->mesh.materials.size()
                                      ? a->mesh.materials[sm.materialIndex].name.c_str()
                                      : "<no material>";
                bool vis = app.hiddenSubmeshes.count(i) == 0;
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Checkbox("##subvis", &vis)) {
                    if (vis)
                        app.hiddenSubmeshes.erase(i);
                    else
                        app.hiddenSubmeshes.insert(i);
                    if (LoadedAsset* wa = app.currentAsset()) wa->gpuDirty = true;
                }
                ImGui::SameLine();
                ImGui::Text("submesh %zu: %s (%zu tris)%s", i, mat, sm.indexCount / 3,
                            vis ? "" : " [hidden]");
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("No asset loaded.");
        }
    }
}

void drawSkeletonPanel(App& app) {
    {
        if (LoadedAsset* a = app.currentAsset()) {
            static char boneFilter[128] = "";
            ImGui::InputTextWithHint("##bonefilter", "Search bones...", boneFilter,
                                     sizeof(boneFilter));
            if (boneFilter[0] != '\0') {
                std::string f = boneFilter;
                for (char& c : f) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (const auto& b : a->skeleton.bones) {
                    std::string n = b.name;
                    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (n.find(f) == std::string::npos) continue;
                    char label[256];
                    std::snprintf(label, sizeof(label), "%s%s  [%zu]",
                                  app.isBoneLocked(b.id) ? "[L] " : "", b.name.c_str(),
                                  app.boneInfluenceCount(b.id));
                    const bool sel = app.isBoneSelected(b.id) ||
                                         app.selectedBone == static_cast<int>(b.id);
                    if (ImGui::Selectable(label, sel)) {
                        const bool additive = ImGui::GetIO().KeyCtrl != 0;
                        app.selectBone(b.id, additive);
                        a->gpuDirty = true;
                    }
                }
            } else if (a->skeleton.rootBone != kNoParent)
                drawSkeletonTree(app, a->skeleton.rootBone);
            else
                ImGui::TextDisabled("No skeleton.");
            ImGui::Separator();
            ImGui::TextDisabled("Selected: %zu%s", app.selectedBones.size(),
                                app.soloBone >= 0 ? " | SOLO" : "");
            if (ImGui::Button("Hierarchy")) {
                if (app.selectedBone >= 0)
                    app.selectBoneHierarchy(static_cast<std::uint32_t>(app.selectedBone),
                                            ImGui::GetIO().KeyCtrl != 0);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Select bone + all descendants (Ctrl adds)");
            ImGui::SameLine();
            if (ImGui::Button("Clear sel")) app.clearBoneSelection();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear bone multi-select");
            ImGui::SameLine();
            if (ImGui::Button(app.soloBone >= 0 ? "Un-solo" : "Solo")) {
                if (app.soloBone >= 0) {
                    app.soloBone = -1;
                } else if (app.selectedBone >= 0) {
                    app.soloBone = app.selectedBone;
                }
                if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Isolate selected bone");
            ImGui::SameLine();
            if (ImGui::Button("Unhide all")) app.clearHiddenBones();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear per-bone hide + solo");
            static char setName[64] = "";
            ImGui::InputTextWithHint("##setname", "Set name...", setName, sizeof(setName));
            ImGui::SameLine();
            if (ImGui::Button("Save set")) {
                if (setName[0] != '\0') {
                    app.saveBoneSelectionSet(setName);
                    app.setStatus(std::string("Saved selection set '") + setName + "'.",
                                  "success");
                    setName[0] = '\0';
                }
            }
            if (!app.boneSelectionSets.empty()) {
                static int setIdx = 0;
                std::vector<const char*> names;
                for (const auto& kv : app.boneSelectionSets) names.push_back(kv.first.c_str());
                if (setIdx >= static_cast<int>(names.size())) setIdx = 0;
                ImGui::Combo("Sets", &setIdx, names.data(), static_cast<int>(names.size()));
                ImGui::SameLine();
                if (ImGui::Button("Load")) {
                    if (!app.loadBoneSelectionSet(names[static_cast<std::size_t>(setIdx)]))
                        app.setStatus("Set resolves to no bones on this asset.", "warning");
                }
                ImGui::SameLine();
                if (ImGui::Button("Del")) {
                    app.deleteBoneSelectionSet(names[static_cast<std::size_t>(setIdx)]);
                    setIdx = 0;
                }
                tipFor("Delete the selected bone selection set (bones stay)");
            }
        } else {
            ImGui::TextDisabled("No asset loaded.");
        }
    }
}

void drawBoneProperties(App& app) {
    ImGui::Text("Bone Properties");
    ImGui::Separator();
    ImGui::Checkbox("Always on top (X-ray)", &app.xrayBones);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draw skeleton after mesh with depth test off (X)");
    LoadedAsset* a = app.currentAsset();
    if (!a || app.selectedBone < 0) {
        ImGui::TextDisabled("Select a bone in the Skeleton tree");
        ImGui::TextDisabled("or click one in the viewport (no Ctrl, no drag).");
        if (a && a->skeleton.rootBone != kNoParent) {
            if (ImGui::Button("Select root")) {
                app.selectedBone = a->skeleton.rootBone;
                a->gpuDirty = true;
            }
        }
        return;
    }
    Bone* mb = a->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone));
    if (!mb) {
        ImGui::TextDisabled("Selected bone no longer exists.");
        return;
    }
    const Bone* b = mb;
    ImGui::Text("Name: %s", b->name.c_str());
    ImGui::Text("ID: %u", b->id);
    const char* parentName = "<root>";
    if (b->parentId != kNoParent) {
        if (const Bone* p = a->skeleton.findById(static_cast<std::uint32_t>(b->parentId)))
            parentName = p->name.c_str();
    }
    ImGui::Text("Parent: %s", parentName);
    ImGui::Text("Children: %zu", b->children.size());
    ImGui::Text("Length: %.4f", b->length);
    ImGui::Text("Influenced verts: %zu", app.boneInfluenceCount(b->id));
    if (const SkeletonProfile* p = findProfile(a->profileId); p) {
        ImGui::Text("Mirror: %s", mirrorBoneName(*p, b->name).c_str());
    }
    ImGui::Separator();
    ImGui::Text("Local Transform");
    const bool boneLocked = app.isBoneLocked(mb->id);
    ImGui::BeginDisabled(boneLocked);
    // --- numeric position -------------------------------------------------
    {
        float pos[3] = {mb->localPosition.x, mb->localPosition.y, mb->localPosition.z};
        ImGui::PushID("bone_pos");
        if (ImGui::DragFloat3("Position", pos, 0.01f, -100000.0f, 100000.0f, "%.3f")) {
            if (!isFiniteF(pos[0]) || !isFiniteF(pos[1]) || !isFiniteF(pos[2])) {
                app.setStatus("Position rejected: non-finite value.", "error");
            } else {
                mb->localPosition = {pos[0], pos[1], pos[2]};
                if (auto rr = rebuildSkeletonRuntime(a->skeleton); !rr)
                    app.setStatus("Bone update failed: " + rr.error().message, "error");
                else {
                    a->gpuDirty = true;
                    a->dirty = true;
                }
            }
        }
        if (ImGui::IsItemActivated())
            app.pushUndoSnapshot("edit position " + mb->name);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            app.runValidation();
            app.setStatus("Position set for " + mb->name + ".", "success");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Local position XYZ (parent space). Ctrl+click to type exact values.");
        ImGui::PopID();
    }
    // --- numeric rotation (degrees in UI, radians in core) ----------------
    {
        float deg[3] = {mb->localRotationEuler.x * kRadToDeg,
                        mb->localRotationEuler.y * kRadToDeg,
                        mb->localRotationEuler.z * kRadToDeg};
        ImGui::PushID("bone_rot");
        if (ImGui::DragFloat3("Rotation (deg)", deg, 0.5f, -720.0f, 720.0f, "%.2f")) {
            if (!isFiniteF(deg[0]) || !isFiniteF(deg[1]) || !isFiniteF(deg[2])) {
                app.setStatus("Rotation rejected: non-finite value.", "error");
            } else {
                mb->localRotationEuler = {deg[0] * kDegToRad, deg[1] * kDegToRad,
                                          deg[2] * kDegToRad};
                if (auto rr = rebuildSkeletonRuntime(a->skeleton); !rr)
                    app.setStatus("Bone update failed: " + rr.error().message, "error");
                else {
                    a->gpuDirty = true;
                    a->dirty = true;
                }
            }
        }
        if (ImGui::IsItemActivated())
            app.pushUndoSnapshot("edit rotation " + mb->name);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            app.runValidation();
            app.setStatus("Rotation set for " + mb->name + ".", "success");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Local XYZ euler in degrees (core stores radians).");
        ImGui::PopID();
    }
    // --- numeric scale ----------------------------------------------------
    {
        float scl[3] = {mb->localScale.x, mb->localScale.y, mb->localScale.z};
        ImGui::PushID("bone_scl");
        if (ImGui::DragFloat3("Scale", scl, 0.01f, -1000.0f, 1000.0f, "%.3f")) {
            if (!isFiniteF(scl[0]) || !isFiniteF(scl[1]) || !isFiniteF(scl[2])) {
                app.setStatus("Scale rejected: non-finite value.", "error");
            } else if (std::fabs(scl[0]) < 1e-6f || std::fabs(scl[1]) < 1e-6f ||
                       std::fabs(scl[2]) < 1e-6f) {
                app.setStatus("Scale rejected: near-zero scale breaks bind inverses.", "error");
            } else {
                mb->localScale = {scl[0], scl[1], scl[2]};
                if (auto rr = rebuildSkeletonRuntime(a->skeleton); !rr)
                    app.setStatus("Bone update failed: " + rr.error().message, "error");
                else {
                    a->gpuDirty = true;
                    a->dirty = true;
                }
            }
        }
        if (ImGui::IsItemActivated())
            app.pushUndoSnapshot("edit scale " + mb->name);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            app.runValidation();
            app.setStatus("Scale set for " + mb->name + ".", "success");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Local scale XYZ (near-zero refused: singular bind inverse).");
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (boneLocked) ImGui::TextDisabled("Unlock the bone to edit its transform.");
    // --- transform ops (all real, all undoable) ---------------------------
    if (ImGui::Button("Frame bone")) {
        const Vec3 j{mb->globalTransform.m[3][0], mb->globalTransform.m[3][1],
                     mb->globalTransform.m[3][2]};
        app.camera.target = j;
        app.camera.updateClip();
        app.setStatus("Framed bone " + mb->name + ".", "info");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the camera target to this joint");
    ImGui::SameLine();
    if (ImGui::Button("Select mirror")) {
        bool found = false;
        if (const SkeletonProfile* p = findProfile(a->profileId); p) {
            const std::string mn = mirrorBoneName(*p, mb->name);
            if (const Bone* mbb = a->skeleton.findByName(mn)) {
                app.selectedBone = static_cast<int>(mbb->id);
                a->gpuDirty = true;
                app.setStatus("Selected mirror bone " + mn + ".", "success");
                found = true;
            }
        }
        if (!found) app.setStatus("No mirror bone for " + mb->name + ".", "warning");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the L/R counterpart from the profile");
    ImGui::SameLine();
    {
        // Copy/paste buffer is session state (names shown so a stale paste is obvious).
        static Vec3 copyPos{0, 0, 0}, copyRot{0, 0, 0}, copyScl{1, 1, 1};
        static std::string copyBone;
        static bool haveCopy = false;
        if (ImGui::Button("Copy")) {
            copyPos = mb->localPosition;
            copyRot = mb->localRotationEuler;
            copyScl = mb->localScale;
            copyBone = mb->name;
            haveCopy = true;
            app.setStatus("Copied transform of " + mb->name + ".", "success");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy this bone's local transform");
        ImGui::SameLine();
        ImGui::BeginDisabled(!haveCopy || boneLocked);
        if (ImGui::Button("Paste")) {
            app.pushUndoSnapshot("paste transform to " + mb->name);
            mb->localPosition = copyPos;
            mb->localRotationEuler = copyRot;
            mb->localScale = copyScl;
            if (auto rr = rebuildSkeletonRuntime(a->skeleton); !rr)
                app.setStatus("Paste failed: " + rr.error().message, "error");
            else {
                a->gpuDirty = true;
                a->dirty = true;
                app.runValidation();
                app.setStatus("Pasted transform from " + copyBone + " to " + mb->name + ".",
                              "success");
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(haveCopy ? ("Paste transform copied from " + copyBone).c_str()
                                       : "Copy a bone transform first");
    }
    ImGui::SameLine();
    {
        // Reset to the imported bind pose (frame 0) when the asset has one.
        const bool hasBind = !a->animFrames.empty() && !a->animFrames[0].poses.empty();
        ImGui::BeginDisabled(!hasBind || boneLocked);
        if (ImGui::Button("Reset to bind")) {
            app.pushUndoSnapshot("reset to bind " + mb->name);
            if (auto r = poseSkeletonFromFrame(a->skeleton, a->animFrames, 0); !r)
                app.setStatus("Reset failed: " + r.error().message, "error");
            else {
                a->gpuDirty = true;
                a->dirty = true;
                app.runValidation();
                app.setStatus("Pose reset to bind (frame 0).", "success");
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(hasBind ? "Restore frame-0 bind pose (whole skeleton, undoable)"
                                       : "Needs an imported animation (frame 0 = bind)");
    }
    ImGui::Separator();
    bool lockedNow = boneLocked;
    if (ImGui::Checkbox("Locked (skip paint/mirror/transfer/gizmo/flood/prune/auto-rig)", &lockedNow))
        app.setBoneLocked(mb->id, lockedNow);
    // Destructive ops share one red family so they read as dangerous at a
    // glance (still undoable — the hint below says so).
ImGui::PushStyleColor(ImGuiCol_Button, Theme::kDanger);
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kDangerHover);
ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kDangerActive);
    ImGui::BeginDisabled(app.selectedBone < 0);
    if (ImGui::Button("Flood")) {
        if (auto r = app.floodSelectedBone(); !r)
            app.setStatus("Flood failed: " + r.error().message, "error");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(app.selectedBone < 0
                               ? "Needs a selected bone — pick one in Skeleton or viewport."
                               : "Bind every vertex rigidly to this bone (destructive, undoable)");
    ImGui::SameLine();
    ImGui::BeginDisabled(app.selectedBone < 0);
    if (ImGui::Button("Prune")) {
        if (auto r = app.pruneSelectedBone(); !r)
            app.setStatus("Prune failed: " + r.error().message, "error");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(app.selectedBone < 0
                               ? "Needs a selected bone — pick one in Skeleton or viewport."
                               : "Remove this bone from every vertex (destructive, undoable)");
    ImGui::PopStyleColor(3);
    ImGui::TextDisabled("Flood/Prune are destructive but undoable.");
    if (ImGui::Button("Lock sockets")) app.lockSocketBones();
    ImGui::SameLine();
    if (ImGui::Button("Unlock all")) app.unlockAllBones();
    if (ImGui::Button(app.boneVisible(mb->id) ? "Hide bone" : "Show bone"))
        app.setBoneHidden(mb->id, app.boneVisible(mb->id));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Isolate/hide this bone in viewport + picking");
    ImGui::SameLine();
    if (ImGui::Button(app.soloBone == app.selectedBone ? "Un-solo" : "Solo"))
        app.soloBone = (app.soloBone == app.selectedBone) ? -1 : app.selectedBone;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo this bone (all others hidden)");
    ImGui::SameLine();
    if (ImGui::Button("Select hierarchy"))
        app.selectBoneHierarchy(mb->id, ImGui::GetIO().KeyCtrl != 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select bone + all descendants");
    ImGui::Separator();
    // Gizmo op/space/snap live in the Gizmo panel only (canonical): the
    // duplicate radios here bound the same App fields (gizmoOp/gizmoSpace/
    // gizmoSnap/snapTranslate/snapRotateDeg/snapScale) and are deleted.
    ImGui::Text("Gizmo");
    ImGui::TextDisabled("Op / space / snap live in the Gizmo panel.");
    if (ImGui::Button("Show Gizmo panel")) app.uiSettings.showGizmoPanel = true;
    // Gizmo display options
    ImGui::Separator();
    ImGui::Text("Gizmo Display");
    ImGui::Checkbox("Show Axis Labels", &app.gizmoShowAxisLabels);
    ImGui::Checkbox("Show Plane Handles", &app.gizmoShowPlaneHandles);
    ImGui::Checkbox("Show Center Handle", &app.gizmoShowCenterHandle);
    ImGui::SliderFloat("Handle Size", &app.gizmoHandleSize, 0.5f, 2.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale factor for gizmo handle size");
#ifdef M2RIG_WITH_GIZMO
    ImGui::TextDisabled("Drag the gizmo in the viewport (undoable).");
#else
    ImGui::TextDisabled("Gizmo disabled at build time (M2RIG_WITH_GIZMO=OFF).");
#endif
}

// --- Weight table v1 (lives in the Weights panel: no new persisted flag) --
// Row collection is a cheap index list over mesh.vertices; only rows the
// ImGuiListClipper actually displays build display strings (see the honest-
// perf note at the table below). All mutations go through the same undoable
// App ops as the single-vertex editor (setVertexInfluenceWeight /
// removeVertexInfluence / normalizeVertexWeights) or a single-snapshot bulk
// loop over the same core repair primitive — never a side channel.
static void collectWeightTableRows(const App& app, bool onlySelected, float minWeight,
                                   const char* nameFilter, std::vector<std::size_t>& out) {
    out.clear();
    const LoadedAsset* a = app.currentAsset();
    if (a == nullptr) return;
    const bool haveNameFilter = nameFilter != nullptr && nameFilter[0] != '\0';
    const bool useSel = onlySelected && app.selectedBone >= 0;
    const auto selBone = static_cast<std::uint32_t>(app.selectedBone);
    for (std::size_t vi = 0; vi < a->mesh.vertices.size(); ++vi) {
        const auto& infs = a->mesh.vertices[vi].influences;
        if (useSel) {
            if (weightOfBone(infs, selBone) < minWeight) continue;
        } else if (minWeight > 0.0f) {
            float wmax = 0.0f;
            for (const auto& inf : infs) wmax = (std::max)(wmax, inf.weight);
            if (wmax < minWeight) continue;
        }
        if (haveNameFilter) {
            bool hit = false;
            for (const auto& inf : infs) {
                const Bone* b = a->skeleton.findById(inf.bone);
                if (b != nullptr && b->name.find(nameFilter) != std::string::npos) {
                    hit = true;
                    break;
                }
            }
            if (!hit) continue;
        }
        out.push_back(vi);
    }
}

// CSV export reuses the Export-panel saveFileDialog pattern (same native
// dialog, same confirmed/cancel contract) — no new dialog plumbing.
static void doExportWeightsCsv(App& app, const std::vector<std::size_t>& rows) {
    LoadedAsset* a = app.currentAsset();
    if (a == nullptr) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export weight table CSV",
                                            "CSV files (*.csv)|*.csv", "csv",
                                            a->id + "_weights.csv");
    if (!dlg.confirmed) return;
    std::ofstream out(dlg.path, std::ios::out | std::ios::binary);
    if (!out.is_open()) {
        app.setStatus("Cannot write CSV: " + dlg.path, "error");
        return;
    }
    out << "vertex,bone_id,bone_name,weight\n";
    for (std::size_t vi : rows) {
        if (vi >= a->mesh.vertices.size()) continue;
        for (const auto& inf : a->mesh.vertices[vi].influences) {
            const Bone* b = a->skeleton.findById(inf.bone);
            out << vi << ',' << inf.bone << ",\"" << (b != nullptr ? b->name : "?") << "\","
                << inf.weight << '\n';
        }
    }
    out.close();
    if (!out) {
        app.setStatus("CSV write failed: " + dlg.path, "error");
        return;
    }
    app.setStatus("Weight table CSV exported (" + std::to_string(rows.size()) + " verts) -> " +
                      dlg.path,
                  "success");
}

void drawWeightPanel(App& app, Renderer& renderer) {
    ImGui::Text("Weight Paint");
    ImGui::Separator();
    const char* brushNames[] = {"Add", "Subtract", "Smooth", "Normalize", "Blur", "Sharpen"};
    int bm = static_cast<int>(app.brushMode);
    if (ImGui::Combo("Brush", &bm, brushNames, 6)) app.brushMode = static_cast<BrushMode>(bm);
    ImGui::SliderFloat("Radius", &app.brushRadius, 0.05f, 2.5f, "%.2f");
    ImGui::SliderFloat("Strength", &app.brushStrength, 0.05f, 1.0f, "%.2f");
    const char* falloffs[] = {"Linear", "Cos2", "Smoothstep"};
    int fo = static_cast<int>(app.paintFalloff);
    if (ImGui::Combo("Falloff", &fo, falloffs, 3)) app.paintFalloff = static_cast<PaintFalloff>(fo);
    ImGui::Checkbox("Symmetry", &app.symmetryEnabled);
    if (app.symmetryEnabled) {
        const char* axes[] = {"X", "Y", "Z"};
        int ax = static_cast<int>(app.symmetryAxis);
        if (ImGui::Combo("Axis", &ax, axes, 3)) app.symmetryAxis = static_cast<SymmetryAxis>(ax);
    }
    ImGui::Separator();
    if (LoadedAsset* a = app.currentAsset()) {
        // Cached on App (recomputed only when weights change, not per frame).
        const WeightQualityMetrics q = app.weightQuality();
        ImGui::Text("Normalized: %.2f%%", q.normalizedPct);
        ImGui::Text("Unweighted: %.2f%%", q.unweightedPct);
        ImGui::Text("Invalid: %.0f", q.invalidCount);
        ImGui::Text("Max influences: %zu", q.maxInfluenceCount);
        ImGui::Text("Avg influences: %.2f", q.avgInfluenceCount);
        if (ImGui::Button("Normalize all weights")) {
            app.pushUndoSnapshot("normalize all weights");
            RepairStats stats = repairMeshWeights(a->mesh, a->skeleton.bones.size());
            app.noteWeightsChanged();
            a->gpuDirty = true;
            a->dirty = true;
            app.runValidation();
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "Repair: %zu verts changed, %zu invalid removed, %zu dups merged, "
                          "removed mass %.4f.",
                          stats.verticesChanged, stats.invalidRemoved, stats.duplicatesMerged,
                          stats.removedMass);
            app.setStatus(buf, "success");
        }
        ImGui::SameLine();
        if (ImGui::Button("Validate")) app.runValidation();
        tipFor("Run the full compatibility check (<=4 influences, skeleton, sockets, topology)");
        ImGui::Separator();
        ImGui::BeginDisabled(!app.canUndo());
        if (ImGui::Button("Undo")) app.undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!app.canRedo());
        if (ImGui::Button("Redo")) app.redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(app.currentAsset() == nullptr);
        if (ImGui::Button("Mirror weights")) {
            if (auto r = app.mirrorWeights(); !r)
                app.setStatus("Mirror failed: " + r.error().message, "error");
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(app.currentAsset() == nullptr
                                   ? "Needs a loaded asset — Project > Load sample armor"
                                   : "Mirror weights across L/R bone pairs (undoable)");
        if (app.viewMode == ViewMode::Weights) {
            ImGui::Separator();
            ImGui::Text("Heatmap (selected bone):");
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x - 60.0f;
            const ImU32 ramp[5] = {IM_COL32(40, 80, 255, 255), IM_COL32(0, 220, 220, 255),
                                   IM_COL32(40, 220, 80, 255), IM_COL32(255, 220, 40, 255),
                                   IM_COL32(255, 50, 40, 255)};
            for (int i = 0; i < 5; ++i)
                dl->AddRectFilled(ImVec2(p.x + w * i / 5.0f, p.y),
                                  ImVec2(p.x + w * (i + 1) / 5.0f, p.y + 12), ramp[i]);
            ImGui::Dummy(ImVec2(w, 14));
            ImGui::SameLine();
            ImGui::TextDisabled("0.0 - 1.0");
        }
        ImGui::BeginDisabled(app.currentAsset() == nullptr || app.assets.size() < 2);
        if (ImGui::Button("Transfer from...")) ImGui::OpenPopup("transfer_src");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip((app.currentAsset() == nullptr || app.assets.size() < 2)
                                   ? "Needs two loaded assets — load a second asset first."
                                   : "Transfer weights from another loaded asset (undoable)");
        if (ImGui::BeginPopup("transfer_src")) {
            if (app.assets.size() < 2) ImGui::TextDisabled("Load a second asset first.");
            for (const auto& kv : app.assets) {
                if (kv.first == app.current) continue;
                if (ImGui::Selectable(("Quick: " + kv.first).c_str())) {
                    if (auto r = app.transferWeightsFrom(kv.first); !r)
                        app.setStatus("Transfer failed: " + r.error().message, "error");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("One-shot kNN transfer with the current bone map (fast)");
                if (ImGui::Selectable(("Self-train: " + kv.first).c_str())) {
                    if (auto r = app.transferWeightsSelfTrained(kv.first); !r)
                        app.setStatus("Self-train failed: " + r.error().message, "error");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Optimizes the bone map first (slower, better fit)");
            }
            ImGui::EndPopup();
        }
        ImGui::BeginDisabled(app.currentAsset() == nullptr);
        if (ImGui::Button("Auto-rig from skeleton")) {
            if (auto r = app.autoRigFromSkeleton(); !r)
                app.setStatus("Auto-rig failed: " + r.error().message, "error");
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Auto-rig from skeleton: binds every vertex to nearest bones (undoable)");
        ImGui::TextDisabled("Auto-rig binds every vertex to the nearest bones (undoable).");
        if (app.selectedBone < 0)
            ImGui::TextDisabled("Select a bone, enable Paint, then drag in the viewport.");
        else if (!app.paintMode)
            ImGui::TextDisabled("Enable Paint in the toolbar to paint. Weights view shows heatmap.");
        ImGui::Separator();
        ImGui::Text("Vertex weights (<=4, Metin2):");
        if (!a->mesh.vertices.empty()) {
            if (app.selectedVertex < 0 ||
                static_cast<std::size_t>(app.selectedVertex) >= a->mesh.vertices.size())
                app.selectedVertex = 0;
            int vtx = app.selectedVertex;
            if (ImGui::SliderInt("Vertex", &vtx, 0,
                                 static_cast<int>(a->mesh.vertices.size()) - 1)) {
                app.selectedVertex = vtx;
                if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
            }
            const Vertex& vv = a->mesh.vertices[static_cast<std::size_t>(app.selectedVertex)];
            ImGui::TextDisabled("pos (%.2f, %.2f, %.2f) | %zu influences", vv.position.x,
                                vv.position.y, vv.position.z, vv.influences.size());
            for (std::size_t s = 0; s < vv.influences.size(); ++s) {
                ImGui::PushID(static_cast<int>(s));
                const BoneInfluence& inf = vv.influences[s];
                const char* bname = "?";
                if (const Bone* bb =
                        a->skeleton.findById(inf.bone)) bname = bb->name.c_str();
                ImGui::Text("%s [%u]%s", bname, inf.bone,
                            app.isBoneLocked(inf.bone) ? " [L]" : "");
                ImGui::SameLine();
                float w = inf.weight;
                ImGui::SetNextItemWidth(90);
                if (ImGui::DragFloat("##w", &w, 0.01f, 0.0f, 1.0f, "%.3f")) {
                    if (auto r = app.setVertexInfluenceWeight(
                            static_cast<std::size_t>(app.selectedVertex), s, w);
                        !r)
                        app.setStatus("Weight edit failed: " + r.error().message, "error");
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) app.runValidation();
                ImGui::SameLine();
                ImGui::BeginDisabled(vv.influences.size() <= 1);
                if (ImGui::SmallButton("x")) {
                    if (auto r = app.removeVertexInfluence(
                            static_cast<std::size_t>(app.selectedVertex), s);
                        !r)
                        app.setStatus("Remove failed: " + r.error().message, "error");
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            if (ImGui::Button("Normalize vertex")) {
                if (auto r = app.normalizeVertexWeights(
                        static_cast<std::size_t>(app.selectedVertex));
                    !r)
                    app.setStatus("Normalize failed: " + r.error().message, "error");
            }
        } else {
            ImGui::TextDisabled("Mesh has no vertices.");
        }
        ImGui::Separator();
        ImGui::Text("Weight table (v1):");
        // View-local filter state (not persisted): selected-bone gate + minimum
        // weight + bone-name substring. Reuses the single-vertex editor above
        // for per-influence edits (the "Sel" button loads a row into it).
        static bool s_wtOnlySel = true;
        static float s_wtMinW = 0.0f;
        static char s_wtNameFilter[64] = "";
        ImGui::Checkbox("Only selected bone", &s_wtOnlySel);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("Min weight", &s_wtMinW, 0.0f, 1.0f, "%.3f");
        if (s_wtMinW < 0.0f) s_wtMinW = 0.0f;
        if (s_wtMinW > 1.0f) s_wtMinW = 1.0f;
        ImGui::SetNextItemWidth(160);
        ImGui::InputText("Bone name contains", s_wtNameFilter, sizeof(s_wtNameFilter));
        std::vector<std::size_t> wtRows;
        collectWeightTableRows(app, s_wtOnlySel, s_wtMinW, s_wtNameFilter, wtRows);
        ImGui::TextDisabled("Shown %zu / %zu verts", wtRows.size(), a->mesh.vertices.size());
        // Bulk ops: ONE undo snapshot each (not one per vertex), same core
        // repair primitive as the single-vertex path, same lock guards.
        if (ImGui::Button("Normalize shown")) {
            app.pushUndoSnapshot("normalize shown weights");
            std::size_t done = 0, skipped = 0;
            double dropped = 0.0;
            for (std::size_t vi : wtRows) {
                auto& infs = a->mesh.vertices[vi].influences;
                bool locked = false;
                for (const auto& inf : infs) {
                    if (app.isBoneLocked(inf.bone)) {
                        locked = true;
                        break;
                    }
                }
                if (locked) {
                    ++skipped;
                    continue;
                }
                RepairStats stats;
                repairVertexInfluences(infs, kMetin2MaxInfluences, &stats);
                dropped += stats.removedMass;
                ++done;
            }
            app.noteWeightsChanged();
            a->dirty = true;
            a->gpuDirty = true;
            app.runValidation();
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Normalize shown: %zu verts, %zu skipped (locked), dropped mass %.4f.",
                          done, skipped, dropped);
            app.setStatus(buf, "success");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(app.selectedBone < 0);
        if (ImGui::Button("Prune selected bone in shown")) {
            const auto bone = static_cast<std::uint32_t>(app.selectedBone);
            if (app.isBoneLocked(bone)) {
                app.setStatus("Bone is locked - unlock to prune.", "error");
            } else {
                app.pushUndoSnapshot("prune bone in shown rows");
                std::size_t done = 0, skipped = 0;
                for (std::size_t vi : wtRows) {
                    auto& infs = a->mesh.vertices[vi].influences;
                    std::size_t slot = infs.size();
                    for (std::size_t s = 0; s < infs.size(); ++s) {
                        if (infs[s].bone == bone) {
                            slot = s;
                            break;
                        }
                    }
                    // Same guards as removeVertexInfluence: keep the last
                    // influence, never touch a locked bone.
                    if (slot >= infs.size() || infs.size() <= 1) {
                        ++skipped;
                        continue;
                    }
                    infs.erase(infs.begin() + static_cast<std::ptrdiff_t>(slot));
                    RepairStats stats;
                    repairVertexInfluences(infs, kMetin2MaxInfluences, &stats);
                    ++done;
                }
                app.noteWeightsChanged();
                a->dirty = true;
                a->gpuDirty = true;
                app.runValidation();
                char buf[192];
                std::snprintf(buf, sizeof(buf), "Prune in shown: %zu verts pruned, %zu skipped.",
                              done, skipped);
                app.setStatus(buf, "success");
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(app.selectedBone < 0 ? "Select a bone first."
                                                   : "Remove the selected bone from every shown row");
        ImGui::SameLine();
        if (ImGui::Button("Export CSV...")) doExportWeightsCsv(app, wtRows);
        // Honest perf: ImGuiListClipper renders only the visible rows — the
        // per-row bone-name/weight strings below are built inside the clipped
        // loop, never for the whole mesh at once.
        const ImGuiTableFlags wtFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("weight_table_v1", 3, wtFlags, ImVec2(0, 260))) {
            ImGui::TableSetupColumn("Vertex", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Bones + weights");
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(wtRows.size()));
            while (clipper.Step()) {
                for (int ri = clipper.DisplayStart; ri < clipper.DisplayEnd; ++ri) {
                    const std::size_t vi = wtRows[static_cast<std::size_t>(ri)];
                    if (vi >= a->mesh.vertices.size()) continue;
                    const auto& infs = a->mesh.vertices[vi].influences;
                    ImGui::PushID(ri);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", vi);
                    ImGui::TableNextColumn();
                    std::string summary;
                    summary.reserve(64);
                    for (std::size_t s = 0; s < infs.size(); ++s) {
                        const Bone* bb = a->skeleton.findById(infs[s].bone);
                        char cell[128];
                        std::snprintf(cell, sizeof(cell), "%s:%.3f%s",
                                      bb != nullptr ? bb->name.c_str() : "?",
                                      static_cast<double>(infs[s].weight),
                                      s + 1 < infs.size() ? "  " : "");
                        summary += cell;
                    }
                    ImGui::TextUnformatted(summary.c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Norm")) {
                        if (auto r = app.normalizeVertexWeights(vi); !r)
                            app.setStatus("Normalize failed: " + r.error().message, "error");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Sel")) {
                        app.selectedVertex = static_cast<int>(vi);
                        if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
                    }
                    ImGui::PopID();
                }
            }
            clipper.End();
            ImGui::EndTable();
        }
        (void)renderer;
    } else {
        ImGui::TextDisabled("No asset loaded.");
    }
    ImGui::TextDisabled("Paint: Ctrl+drag paints, plain drag orbits. Weights view = heatmap.");
}

// TU-local 2 s TTL exists() cache for the Materials panel probes (ONE place:
// this file only — app_gpu.cpp owns the upload-time resolve, not the panel).
// Per-frame probe rows would syscall every frame for paths that barely
// change; the System panel uses the same 2 s TTL shape. Keyed by exact
// literal path. Texture RESOLVE stays uncached by design: plug-in media must
// appear without stale hits, and refreshGpu only runs on gpuDirty anyway.
bool cachedPathExists(const std::string& literalPath, double nowSeconds) {
    static std::map<std::string, std::pair<double, bool>> cache;  // path -> (time, hit)
    const auto it = cache.find(literalPath);
    if (it != cache.end() && nowSeconds - it->second.first <= 2.0) return it->second.second;
    std::error_code ec;
    const bool hit = std::filesystem::exists(literalPath, ec) && !ec;
    cache[literalPath] = {nowSeconds, hit};
    return hit;
}

void drawMaterialPanel(App& app) {
    ImGui::Text("Materials / Textures");
    ImGui::Separator();
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        ImGui::TextDisabled("No asset loaded.");
        return;
    }
    // PBR factors (Wave 25a, per-asset session state): drive setPbrMaterial
    // per draw. Albedo DDS binds as before; per-material normal maps bind on
    // the textured-PBR path (linear data, srgb=false uploads).
    bool pbr = app.usePbr;
    if (ImGui::Checkbox("PBR shading", &pbr)) app.usePbr = pbr;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cook-Torrance PBR for Solid modes (punctual sun + IBL)");
    if (ImGui::SliderFloat("Metallic", &a->pbr.metallic, 0.0f, 1.0f)) a->dirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Factor only — the PBR shader has no metallic texture input; albedo + normal maps "
            "are the only texture slots.");
    if (ImGui::SliderFloat("Roughness", &a->pbr.roughness, 0.05f, 1.0f)) a->dirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Factor only — the PBR shader has no roughness texture input; albedo + normal maps "
            "are the only texture slots.");
    if (ImGui::SliderFloat("AO", &a->pbr.ao, 0.0f, 1.0f)) a->dirty = true;
    ImGui::Separator();
    const double nowS = ImGui::GetTime();
    for (std::size_t i = 0; i < a->mesh.materials.size(); ++i) {
        auto& m = a->mesh.materials[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("Material %zu: %s", i, m.name.c_str());
        char path[260];
        std::snprintf(path, sizeof(path), "%s", m.texturePath.c_str());
        if (ImGui::InputText("Texture", path, sizeof(path))) {
            m.texturePath = path;
            a->dirty = true;
            a->gpuDirty = true;
        }
        if (m.texturePath.empty()) {
            ImGui::TextColored({1, 0.6f, 0.3f, 1}, "Missing texture path.");
        } else {
            // Real existence probe, exe dir first: literal path, shipped
            // Data/Models next to the exe, bare basename — each candidate
            // through the 2 s TTL cache above (resolve itself is uncached).
            const std::string base =
                std::filesystem::path(m.texturePath).filename().string();
            const std::filesystem::path exeDir = executableDir();
            auto existsUnder = [&](const std::filesystem::path& dir) {
                return !dir.empty() && !base.empty() &&
                       cachedPathExists((dir / base).string(), nowS);
            };
            const bool found =
                !base.empty() && (cachedPathExists(m.texturePath, nowS) || existsUnder(exeDir) ||
                cachedPathExists((exeDir / "Data" / "Models" / base).string(), nowS) ||
                cachedPathExists(
                    (std::filesystem::current_path() / "Data" / "Models" / base).string(), nowS) ||
                cachedPathExists((std::filesystem::current_path() / base).string(), nowS));
            if (found)
                ImGui::TextColored(Theme::kOk, "Texture found.");
            else
                ImGui::TextColored({1, 0.6f, 0.3f, 1}, "Texture not found (checked .dds next to model).");
        }
        char npath[260];
        std::snprintf(npath, sizeof(npath), "%s", m.normalTexturePath.c_str());
        if (ImGui::InputText("Normal map", npath, sizeof(npath))) {
            m.normalTexturePath = npath;
            a->dirty = true;
            a->gpuDirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Tangent-space normal map (linear data). Uploaded per material; bound on the "
                "textured-PBR path only.");
        if (m.normalTexturePath.empty()) {
            ImGui::TextDisabled("No normal map (PBR uses geometry normals).");
        } else {
            // Same probe shape + TTL cache as the albedo field above.
            const std::string nbase =
                std::filesystem::path(m.normalTexturePath).filename().string();
            const std::filesystem::path exeDir = executableDir();
            auto nExistsUnder = [&](const std::filesystem::path& dir) {
                return !dir.empty() && !nbase.empty() &&
                       cachedPathExists((dir / nbase).string(), nowS);
            };
            const bool nfound =
                !nbase.empty() && (cachedPathExists(m.normalTexturePath, nowS) ||
                nExistsUnder(exeDir) ||
                cachedPathExists((exeDir / "Data" / "Models" / nbase).string(), nowS) ||
                cachedPathExists(
                    (std::filesystem::current_path() / "Data" / "Models" / nbase).string(),
                    nowS) ||
                cachedPathExists((std::filesystem::current_path() / nbase).string(), nowS));
            if (nfound)
                ImGui::TextColored(Theme::kOk, "Normal map found.");
            else
                ImGui::TextColored({1, 0.6f, 0.3f, 1},
                                   "Normal map not found (checked .dds next to model).");
        }
        ImGui::PopID();
    }
}

void drawProjectPanel(App& app, const std::filesystem::path& projectsDir) {
    ImGui::Text("Project");
    ImGui::Separator();
    if (const LoadedAsset* a = app.currentAsset()) {
        ImGui::Text("Asset: %s", a->id.c_str());
        ImGui::Text("Source: %s", a->sourcePath.empty() ? "<procedural>" : a->sourcePath.c_str());
        ImGui::Text("Dirty: %s", a->dirty ? "yes" : "no");
        std::string profile = a->profileId;
        if (ImGui::BeginCombo("Skeleton profile", profile.c_str())) {
            for (const char* id : {"pc_warrior", "pc_warrior_m", "pc_warrior_f", "pc_ninja",
                                   "pc_assassin_m", "pc_assassin_f", "pc_sura", "pc_sura_m",
                                   "pc_sura_f", "pc_shaman", "pc_shaman_m", "pc_shaman_f",
                                   "pc_wolfman", "pc_mount"}) {
                const bool sel = profile == id;
                if (ImGui::Selectable(id, sel)) {
                    app.currentAsset()->profileId = id;
                    app.currentAsset()->dirty = true;
                    app.runValidation();
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("No asset loaded.");
    }
    ImGui::Separator();
    ImGui::Text("Autosave");
    ImGui::SliderInt("Interval [min, 0=off]", &app.autosaveMinutes, 0, 30);
    if (ImGui::Button("Save now")) {
        if (auto r = app.saveAutosaveNow(projectsDir); !r)
            app.setStatus("Autosave failed: " + r.error().message, "error");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Last backup: %s", app.lastAutosaveInfo.c_str());
}

void drawExportPanel(App& app) {
    ImGui::Text("Export");
    ImGui::Separator();
    if (ImGui::Button("Run compatibility check")) app.runValidation();
    if (!app.report.items().empty()) {
        ImGui::Text("%s", app.report.summaryLine().c_str());
        if (app.report.exportBlocked())
            ImGui::TextColored({1, 0.4f, 0.35f, 1}, "EXPORT BLOCKED");
        else
            ImGui::TextColored(Theme::kOk, "Checks passed");
    }
    // Flow helper: chain buttons on one line while they fit, otherwise let
    // the next button fall to a new line (no horizontal clip at 230px
    // columns). sectionW is captured at the section start; needW measures a
    // button label plus frame padding.
    const ImGuiStyle& expStyle = ImGui::GetStyle();
    auto expNeedW = [&](const char* label) -> float {
        return ImGui::CalcTextSize(label).x + expStyle.FramePadding.x * 2.0f +
               expStyle.ItemSpacing.x;
    };
    // --- Formats ----------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Formats");
    ImGui::BeginDisabled(app.currentAsset() == nullptr);
    {
        const float sectionW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        bool firstInRow = true;
        auto flow = [&](const char* label) {
            if (firstInRow) {
                firstInRow = false;
                return;
            }
            if (ImGui::GetCursorPosX() + expNeedW(label) <= sectionW) ImGui::SameLine();
        };
        if (ImGui::Button("Export SMD...")) doExportSmd(app);
        tipFor("Write skeletal mesh (ASCII version/nodes/skeleton/triangles), re-validated, gate enforced");
        flow("Export MSM...");
        if (ImGui::Button("Export MSM...")) doExportMsm(app);
        tipFor("Write Metin2 mesh groups (ShapeData/Index/Model/SourceSkin), re-validated, gate enforced");
        flow("Export FBX (Noesis)...");
        if (ImGui::Button("Export FBX (Noesis)...")) doExportFbx(app);
        tipFor("Convert via Noesis bridge (needs noesis/Noesis.exe next to the app)");
        flow("Export ANI...");
        if (ImGui::Button("Export ANI...")) doExportAni(app);
        tipFor("Export clip as .ani (export-only format, empty clip refused)");
        flow("Export GR2 (bridge)...");
        if (ImGui::Button("Export GR2 (bridge)...")) doExportGr2(app);
        tipFor("Granny3D export via bridge (honest NOT_SUPPORTED_DIRECTLY status when unavailable)");
        flow("GR2 -> FBX (Noesis)...");
        if (ImGui::Button("GR2 -> FBX (Noesis)...")) doExportGr2ToFbx(app);
        tipFor("Convert GR2 to FBX via Noesis bridge (rotate-hardened path)");
    }
    ImGui::EndDisabled();
    {
        const float sectionW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        bool firstInRow = true;
        auto flow = [&](const char* label) {
            if (firstInRow) {
                firstInRow = false;
                return;
            }
            if (ImGui::GetCursorPosX() + expNeedW(label) <= sectionW) ImGui::SameLine();
        };
        if (ImGui::Button("Save workspace...")) doSaveWorkspace(app);
        flow("Load workspace...");
        if (ImGui::Button("Load workspace...")) doLoadWorkspace(app);
    }
    ImGui::Separator();
    ImGui::Text("Pre-export checklist:");
    if (app.report.items().empty()) {
        ImGui::TextDisabled("Run compatibility check first.");
    } else {
        std::size_t shown = 0;
        for (const auto& item : app.report.items()) {
            if (shown >= 8) {
                ImGui::TextDisabled("... and %zu more (see Validation).",
                                    app.report.items().size() - shown);
                break;
            }
            const bool pass = item.severity != Severity::Error && item.severity != Severity::Fatal;
            ImVec4 c = pass ? Theme::kOk : Theme::kErr;
            if (item.severity == Severity::Warning) c = Theme::kWarn;
            ImGui::TextColored(c, "%s %s", pass ? "[OK]" : "[FAIL]", item.id.c_str());
            ++shown;
        }
    }
    // The gate itself is enforced inside exportSmdFile/exportMsmFile
    // (fresh re-validate + fail); this text only explains the checklist.
    ImGui::TextDisabled("SMD/MSM re-validate on export and respect the gate above.");
    ImGui::TextDisabled("GR2 bridge always reports its real status.");
    ImGui::Separator();
    ImGui::Text("LOD");
    ImGui::TextDisabled("Decimation export, live mesh untouched.");
    // Binds the persisted preference (verified: UserPreferences::lodRatio in
    // app.hpp); the old function-static is deleted so the menu Export LOD
    // path and this slider share one value.
    ImGui::SliderFloat("Keep ratio", &app.prefs.lodRatio, 0.05f, 0.95f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fraction of triangles to keep (shortest-edge collapse, weights kept)");
    {
        const float sectionW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        if (ImGui::GetCursorPosX() + expNeedW("Export LOD SMD...") <= sectionW)
            ImGui::SameLine();
        ImGui::BeginDisabled(app.currentAsset() == nullptr);
        if (ImGui::Button("Export LOD SMD...")) doExportLod(app, app.prefs.lodRatio);
        tipFor("Decimate a COPY to the keep ratio (live mesh, undo and isolation untouched)");
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    ImGui::Text("Self-learning");
    {
        const float sectionW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        bool firstInRow = true;
        auto flow = [&](const char* label) {
            if (firstInRow) {
                firstInRow = false;
                return;
            }
            if (ImGui::GetCursorPosX() + expNeedW(label) <= sectionW) ImGui::SameLine();
        };
        if (ImGui::Button("Learn from current asset")) {
            if (auto r = app.learnFromAsset(app.current); !r)
                app.setStatus("Learn failed: " + r.error().message, "error");
            else
                app.setStatus("Learned from current asset.", "success");
        }
        tipFor("Feed the current asset into the transfer learning database");
        flow("Learn from GR2 directory...");
        if (ImGui::Button("Learn from GR2 directory...")) {
            const DialogResult dlg = selectDirectoryDialog(g_mainWindow, "Select GR2 directory for analysis");
            if (dlg.confirmed) {
                auto r = app.learnFromDirectory(dlg.path);
                if (!r)
                    app.setStatus("Analyze failed: " + r.error().message, "error");
                else
                    app.setStatus("GR2 directory analyzed.", "success");
            }
        }
        tipFor("Batch-analyze a GR2 folder into the learning database (needs grnreader98)");
        flow("Save learning DB...");
        if (ImGui::Button("Save learning DB...")) {
            const DialogResult dlg = saveFileDialog(g_mainWindow, "Save learning database",
                                                    "Learning DB (*.m2learn)|*.m2learn", "m2learn", "learning.m2learn");
            if (dlg.confirmed) {
                auto r = app.saveLearningDatabase(dlg.path);
                if (!r)
                    app.setStatus("Save failed: " + r.error().message, "error");
            }
        }
        tipFor("Persist the learning database to a .m2learn file");
        flow("Load learning DB...");
        if (ImGui::Button("Load learning DB...")) {
            const DialogResult dlg = openFileDialog(g_mainWindow, "Load learning database",
                                                    "Learning DB (*.m2learn)|*.m2learn", "m2learn");
            if (dlg.confirmed) {
                auto r = app.loadLearningDatabase(dlg.path);
                if (!r)
                    app.setStatus("Load failed: " + r.error().message, "error");
            }
        }
        tipFor("Load a previously saved .m2learn database");
        flow("Self-learning transfer from source...");
        if (ImGui::Button("Self-learning transfer from source...")) {
            if (ImGui::BeginPopup("self_learn_transfer_source")) {
                for (const auto& [id, a] : app.assets) {
                    if (id != app.current) {
                        if (ImGui::Selectable(id.c_str())) {
                            auto r = app.selfLearningTransfer(id);
                            if (!r)
                                app.setStatus("Transfer failed: " + r.error().message, "error");
                            else
                                app.setStatus("Self-learning transfer complete.", "success");
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndPopup();
            } else {
                ImGui::OpenPopup("self_learn_transfer_source");
            }
        }
        flow("Self-learning auto-rig");
        if (ImGui::Button("Self-learning auto-rig")) {
            auto r = app.selfLearningAutoRig();
            if (!r)
                app.setStatus("Auto-rig failed: " + r.error().message, "error");
            else
                app.setStatus("Self-learning auto-rig complete.", "success");
        }
    }
    ImGui::Separator();
    ImGui::Text("Batch");
    static std::vector<App::BatchRow> lastBatch;
    ImGui::BeginDisabled(app.currentAsset() == nullptr);
    if (ImGui::Button("Export all loaded (SMD+MSM)...")) {
        if (auto r = app.exportAllBatch(); r)
            lastBatch = r.value();
        else {
            lastBatch.clear();
            app.setStatus("Batch failed: " + r.error().message, "error");
        }
    }
    tipFor("Export every loaded asset to SMD+MSM next to the first source file");
    ImGui::EndDisabled();
    if (!lastBatch.empty()) {
        if (ImGui::BeginTable("batch_table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollX)) {
            ImGui::TableSetupColumn("Asset");
            ImGui::TableSetupColumn("SMD");
            ImGui::TableSetupColumn("MSM");
            ImGui::TableSetupColumn("Message");
            ImGui::TableHeadersRow();
            for (const auto& row : lastBatch) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", row.id.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(row.smdOk ? Theme::kOk : Theme::kErr,
                                     "%s", row.smdOk ? "OK" : "FAIL");
                ImGui::TableNextColumn();
                ImGui::TextColored(row.msmOk ? Theme::kOk : Theme::kErr,
                                     "%s", row.msmOk ? "OK" : "FAIL");
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row.message.c_str());
            }
            ImGui::EndTable();
        }
    }
}

void drawValidationPanel(App& app) {
    ImGui::Text("Validation  (%s)", app.report.summaryLine().c_str());
    ImGui::Separator();
    if (app.report.items().empty()) {
        ImGui::TextDisabled("No validation results yet. Press Validate.");
        return;
    }
    if (ImGui::BeginChild("validation_scroll", ImVec2(0, 0), true)) {
        for (const auto& item : app.report.items()) {
            ImVec4 c{0.7f, 0.7f, 0.7f, 1};
            if (item.severity == Severity::Warning) c = Theme::kWarn;
            if (item.severity == Severity::Error || item.severity == Severity::Fatal)
                c = Theme::kErr;
            ImGui::TextColored(c, "[%s/%s] %s: %s", validationCategoryName(item.category),
                               severityName(item.severity), item.id.c_str(), item.message.c_str());
            if (!item.location.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled(" @ %s", item.location.c_str());
            }
        }
    }
    ImGui::EndChild();
}

void drawSystemPanel(App& app) {
    ImGui::Text("System Status");
    ImGui::Separator();
    auto toolRow = [](const char* name, const std::filesystem::path& p) {
        const bool ok = !p.empty() && std::filesystem::exists(p);
        ImGui::TextColored(ok ? Theme::kOk : Theme::kErr, "%s",
                            ok ? "[OK]" : "[--]");
        ImGui::SameLine();
        ImGui::Text("%s: %s", name, ok ? p.string().c_str() : "not found");
    };
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    toolRow("Noesis bridge", cfg.noesisCliPath);
    toolRow("grnreader98", findGrnReader());
    std::filesystem::path models = executableDir() / "Data" / "Models";
    if (!std::filesystem::exists(models)) models = std::filesystem::current_path() / "Data" / "Models";
    // Directory scan cached on a 2 s TTL: per-frame iteration + syscalls
    // stalled the UI thread for zero benefit (counts barely change).
    static double lastScanT = -1e9;
    static std::size_t gr2 = 0, fbx = 0, dds = 0;
    const double nowT = ImGui::GetTime();
    if (std::filesystem::exists(models)) {
        if (nowT - lastScanT > 2.0) {
            lastScanT = nowT;
            gr2 = fbx = dds = 0;
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(models, ec)) {
                if (!e.is_regular_file()) continue;
                const std::string ext = e.path().extension().string();
                if (ext == ".gr2") ++gr2;
                if (ext == ".fbx") ++fbx;
                if (ext == ".dds") ++dds;
            }
        }
        ImGui::Text("Data/Models: %zu GR2 / %zu FBX / %zu DDS", gr2, fbx, dds);
    } else {
        ImGui::TextDisabled("Data/Models: not present next to the executable.");
    }
    std::size_t errors = 0, warnings = 0;
    for (const auto& item : app.report.items()) {
        if (item.severity == Severity::Error || item.severity == Severity::Fatal)
            ++errors;
        else if (item.severity == Severity::Warning)
            ++warnings;
    }
    ImGui::Text("Assets loaded: %zu | Validation: %zu errors, %zu warnings", app.assets.size(),
                errors, warnings);
    ImGui::TextDisabled("GR2 native emit: NOT supported directly (bridge only).");
}

void drawConsolePanel() {    ImGui::Text("Console");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) Logger::instance().clearRecent();
    ImGui::Separator();
    if (ImGui::BeginChild("console_scroll", ImVec2(0, 0), true,
                           ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& e : Logger::instance().recent(200)) {
            ImVec4 c{0.75f, 0.75f, 0.75f, 1};
            if (e.level == LogLevel::Warning) c = Theme::kWarn;
            if (e.level == LogLevel::Error || e.level == LogLevel::Fatal) c = Theme::kErr;
            ImGui::TextColored(c, "[%s] %s", e.timestamp.c_str(), e.message.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void drawKeyframeRow(App& app, LoadedAsset* a) {
    // Pose keyframes (in-session authoring; scrub samples the clip).
    ImGui::Separator();
    ImGui::Text("Keys: %zu", a->clip.keys.size());
    ImGui::SameLine();
    if (ImGui::Button("Add key")) {
        if (auto r = app.addKeyframeHere(); !r)
            app.setStatus("Add key failed: " + r.error().message, "error");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capture the current pose at this frame");
    ImGui::SameLine();
    if (ImGui::Button("Del key")) {
        if (auto r = app.deleteKeyframeHere(); !r)
            app.setStatus("Delete key failed: " + r.error().message, "error");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the key at this frame");
    ImGui::SameLine();
    if (ImGui::Button("Bake clip")) {
        if (auto r = app.bakeClipToFrames(); !r)
            app.setStatus("Bake failed: " + r.error().message, "error");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Write keys to SMD frames (replaces imported frames)");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        if (auto r = app.clearClip(); !r)
            app.setStatus("Clear failed: " + r.error().message, "error");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drop all keys (timeline uses frames again)");
    if (!a->clip.keys.empty()) {
        std::string keyList = "keys @";
        for (const auto& k : a->clip.keys) keyList += " " + std::to_string(k.frame);
        ImGui::TextDisabled("%s", keyList.c_str());
        ImGui::TextDisabled("Scrub re-applies keys; re-add a key after posing.");
    }
}

void drawTimelinePanel(App& app) {
    // Wall-clock anchor shared with the playback branch below; reset here so
    // switching to an animation-less asset stops playback instead of jumping
    // on resume with a stale multi-second accumulator.
    static double lastT = 0.0;
    LoadedAsset* a = app.currentAsset();
    const std::size_t frameCount = app.timelineFrameCount();
    if (!a || frameCount <= 1) {
        app.timelinePlaying = false;
        lastT = 0.0;
        ImGui::Text("Timeline");
        ImGui::SameLine();
        ImGui::TextDisabled(
            "No animation loaded. Import an SMD with multiple skeleton frames to preview poses.");
        // Keyframe authoring stays reachable on static assets: the first key
        // is what grows the timeline. The steppers move the key cursor
        // without posing (there are no frames yet); Add key captures the
        // live pose at that index.
        if (a) {
            drawKeyframeRow(app, a);
            const int cf = static_cast<int>(a->currentFrame);
            ImGui::Text("Frame");
            ImGui::SameLine();
            if (ImGui::Button("<")) {
                if (cf > 0) a->currentFrame = static_cast<std::size_t>(cf - 1);
            }
            ImGui::SameLine();
            if (ImGui::Button(">")) {
                if (cf < 100000) a->currentFrame = static_cast<std::size_t>(cf + 1);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("frame %d (add keys to grow the timeline)", cf);
        }
        return;
    }
    if (ImGui::Button(app.timelinePlaying ? "Pause" : "Play")) app.timelinePlaying = !app.timelinePlaying;
    ImGui::SameLine();
    if (ImGui::Button("|<")) {
        if (auto r = app.setCurrentFrame(0); !r)
            app.setStatus("Frame preview failed: " + r.error().message, "error");
    }
    ImGui::SameLine();
    if (ImGui::Button("<")) {
        const std::size_t prev = a->currentFrame == 0 ? frameCount - 1 : a->currentFrame - 1;
        if (auto r = app.setCurrentFrame(prev); !r)
            app.setStatus("Frame preview failed: " + r.error().message, "error");
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        const std::size_t next = (a->currentFrame + 1) % frameCount;
        if (auto r = app.setCurrentFrame(next); !r)
            app.setStatus("Frame preview failed: " + r.error().message, "error");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::SliderFloat("FPS", &app.timelineFps, 1.0f, 60.0f, "%.0f");
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &app.timelineLoop);
    ImGui::SameLine();
    if (ImGui::Checkbox("Deform preview", &app.previewDeform)) {
        if (LoadedAsset* pa = app.currentAsset()) pa->gpuDirty = true;
    }
    // Advance playback on wall clock (render-only; bind data untouched).
    if (app.timelinePlaying) {
        const double now = ImGui::GetTime();
        if (lastT <= 0.0) lastT = now;
        const double acc = now - lastT;
        const double step = app.timelineFps > 0.0f ? 1.0 / app.timelineFps : 1.0 / 24.0;
        if (acc >= step) {
            lastT = now;
            std::size_t next = a->currentFrame + 1;
            if (next >= frameCount) next = app.timelineLoop ? 0 : frameCount - 1;
            if (next != a->currentFrame) {
                if (auto r = app.setCurrentFrame(next); !r)
                    app.setStatus("Frame preview failed: " + r.error().message, "error");
            } else if (!app.timelineLoop) {
                app.timelinePlaying = false;
            }
        }
    } else {
        lastT = 0.0;  // reset the clock so resume doesn't jump
    }
    int frame = static_cast<int>(a->currentFrame);
    ImGui::Text("Frame");
    ImGui::SameLine();
    if (ImGui::SliderInt("##frame", &frame, 0, static_cast<int>(frameCount) - 1)) {
        if (auto r = app.setCurrentFrame(static_cast<std::size_t>(frame)); !r)
            app.setStatus("Frame preview failed: " + r.error().message, "error");
    }
    // Keyboard transport: same stricter appOwnsKeyboard gate as the global
    // shortcuts in drawAllPanels (condition duplicated on purpose — this
    // slice keeps all changes inside panels.cpp, no header extraction).
    const ImGuiIO& timelineIo = ImGui::GetIO();
    const bool timelineOwnsKeyboard = !timelineIo.WantTextInput && !timelineIo.WantCaptureKeyboard;
    if (timelineOwnsKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) app.timelinePlaying = !app.timelinePlaying;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            const std::size_t prev = a->currentFrame == 0 ? frameCount - 1 : a->currentFrame - 1;
            if (auto r = app.setCurrentFrame(prev); !r)
                app.setStatus("Frame preview failed: " + r.error().message, "error");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            const std::size_t next = (a->currentFrame + 1) % frameCount;
            if (auto r = app.setCurrentFrame(next); !r)
                app.setStatus("Frame preview failed: " + r.error().message, "error");
        }
    }
    ImGui::SameLine();
    const int shownTime =
        a->currentFrame < a->animFrames.size() ? a->animFrames[a->currentFrame].time
                                               : static_cast<int>(a->currentFrame);
    ImGui::TextDisabled("t=%d / %zu frames%s", shownTime, frameCount,
                        app.previewDeform ? " (deformed preview)" : " (skeleton pose)");
    drawKeyframeRow(app, a);
}

}  // namespace

// Draw the 3D scene contents (mesh, grid, bones, gizmo) to the currently bound
// render target. This is shared between the offscreen viewport path and the
// headless smoke test. Returns the number of draw calls issued.
int drawSceneContents(App& app, Renderer& renderer, const Mat4& viewProj, const ViewportRect& rect,
                      float modelRadius) {
    int drawCalls = 0;
    const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};

    if (app.showGrid) {
        float half = modelRadius * 1.5f;
        if (half < 5.0f) half = 5.0f;
        if (half > 500.0f) half = 500.0f;
        renderer.drawLines(buildGridLines(half, half / 10.0f), viewProj);
        drawCalls++;
    }
    if (LoadedAsset* a = app.currentAsset()) {
        if (auto r = app.refreshGpu(renderer); !r) {
            app.setStatus("GPU upload failed: " + r.error().message, "error");
        } else {
            // G4 routing (pure, pinned by tests/test_app.cpp): resolved from
            // the RAW toggles — Solid/SolidWireframe gating lives inside
            // resolveDrawPath, so fill/pbr below match the old
            // inline conditions exactly (same draws, same call counts).
            // GPU skinning (Wave 24): when a skin stream is paired, the mesh
            // deforms on the GPU from the per-frame palette (bindInverse *
            // currentGlobal) — no CPU re-upload during playback. At bind pose
            // the palette is identity, so static views take the same path.
            const bool skinned = renderer.hasSkinning(a->id);
            const DrawPath path = resolveDrawPath(app.viewMode, app.usePbr, app.textured,
                                                  renderer.hasTexture(a->id), skinned);
            const FillMode fill =
                (path == DrawPath::WireBlinn) ? FillMode::Wireframe : FillMode::Solid;
            // Textured shading is solid-modes-only via the path itself
            // (SolidTextured/PbrTextured): debug ramps (Normals / Height /
            // Weights / UV) stay untextured, never washed out.
            // MSE particle preview shares the per-frame palette; it is built
            // once when either the skinned draws or the MSE tick need it.
            const bool mseWanted =
                g_mseTabEnabled && g_msePreview && !g_mse.doc.attachments.empty();
            const bool mseTickNow = mseWanted && g_msePlaying;
            std::vector<Mat4> framePalette;
            if (skinned || mseTickNow)
                framePalette = buildSkinningPalette(a->skeleton, a->bindInverse);
            if (skinned) renderer.setSkinningPalette(framePalette);
            // PBR (Wave 25a): Solid/SolidWireframe only — debug ramps stay
            // flat (display-referred) and Wireframe stays Blinn (topology
            // view, not a material view). Derived from the resolved path so
            // the condition cannot drift from the routing table above.
            const bool pbr =
                (path == DrawPath::PbrSolid || path == DrawPath::PbrTextured);
            if (pbr) renderer.setPbrMaterial(a->pbr);
            // Per-material keys <assetId>#mat<i> / <assetId>#nmat<i> are
            // uploaded by refreshGpu. Static textured multi-material meshes
            // route per visible submesh (hasTexture probe + ranged draw,
            // single-texture fallback per range); textured-PBR multi-material
            // meshes do the same with a per-range normal bind (skinned-PBR
            // included via the skinned range variant — the palette is already
            // set above). Every other mode keeps its whole-draw call by
            // design (static ranges on a skinned-Blinn mesh would drop the
            // GPU deform). Single-material meshes always whole-draw
            // (byte-identical behavior and draw-call count).
            // SolidTextured implies useTextured && !pbr on a solid mode, so
            // this matches the old (useTextured && !skinned && !pbr) gate.
            const bool multiMat = (path == DrawPath::SolidTextured) && !skinned &&
                                  meshHasDistinctVisibleMaterials(a->mesh, app.hiddenSubmeshes);
            const bool multiMatPbr =
                (path == DrawPath::PbrTextured) &&
                meshHasDistinctVisibleMaterials(a->mesh, app.hiddenSubmeshes);
            if (multiMat) {
                drawCalls +=
                    drawTexturedSubmeshRanges(renderer, *a, app.hiddenSubmeshes, viewProj, fill);
            } else if (multiMatPbr) {
                drawCalls += drawTexturedPbrSubmeshRanges(renderer, *a, app.hiddenSubmeshes,
                                                          viewProj, fill, skinned);
            } else {
                // Same draws as before the G4 extraction, dispatched on the
                // resolved path (skinned picks the Skinned suffix of the same
                // path; multiMat/multiMatPbr above stay SolidTextured/
                // PbrTextured sub-variants).
                switch (path) {
                    case DrawPath::SolidTextured:
                    case DrawPath::PbrTextured:
                        if (skinned) {
                            if (pbr) {
                                // Whole-draw normal bind for the draw's
                                // material; missing key binds nothing, so the
                                // no-normal-map frame is call-identical.
                                const bool nBound = bindPbrNormalForMaterial(
                                    renderer, *a, primaryMaterialIndex(a->mesh));
                                renderer.drawMeshTexturedSkinnedPbr(a->id, viewProj, fill);
                                if (nBound) renderer.clearPbrNormalMap();
                            } else
                                renderer.drawMeshTexturedSkinned(a->id, viewProj, fill);
                        } else {
                            if (pbr) {
                                const bool nBound = bindPbrNormalForMaterial(
                                    renderer, *a, primaryMaterialIndex(a->mesh));
                                renderer.drawMeshTexturedPbr(a->id, viewProj, fill);
                                if (nBound) renderer.clearPbrNormalMap();
                            } else
                                renderer.drawMeshTextured(a->id, viewProj, fill);
                        }
                        break;
                    case DrawPath::FlatDebug:
                        // Debug ramps are display-referred: the unlit flat draw keeps
                        // them exact (and legend-consistent) under the linear lit
                        // pipeline instead of washing them through Blinn-Phong.
                        if (skinned)
                            renderer.drawMeshFlatSkinned(a->id, viewProj, fill);
                        else
                            renderer.drawMeshFlat(a->id, viewProj, fill);
                        break;
                    case DrawPath::SolidUntextured:
                    case DrawPath::WireBlinn:
                    case DrawPath::PbrSolid:
                        if (skinned) {
                            if (pbr)
                                renderer.drawMeshSkinnedPbr(a->id, viewProj, fill);
                            else
                                renderer.drawMeshSkinned(a->id, viewProj, fill);
                        } else {
                            if (pbr)
                                renderer.drawMeshPbr(a->id, viewProj, fill);
                            else
                                renderer.drawMesh(a->id, viewProj, fill);
                        }
                        break;
                }
            }
            if (!multiMat && !multiMatPbr) drawCalls++;
            // SolidWireframe mode implies the overlay; the checkbox adds it
            // to every other solid-based mode (drawn once, never stacked).
            if (app.viewMode == ViewMode::SolidWireframe ||
                (app.showWireOverlay && app.viewMode != ViewMode::SolidWireframe &&
                 app.viewMode != ViewMode::Wireframe)) {
                if (skinned)
                    renderer.drawMeshWireOverlaySkinned(a->id, viewProj);
                else
                    renderer.drawMeshWireOverlay(a->id, viewProj);
                drawCalls++;
            }
            if (app.showBones) {
                float worldPerPixel = 0.01f;
                if (rect.h > 0) {
                    if (app.camera.orthographic)
                        worldPerPixel = app.camera.orthoHeight / static_cast<float>(rect.h);
                    else
                        worldPerPixel = 2.0f * app.camera.distance *
                                        std::tan(app.camera.fovY * 0.5f) /
                                        static_cast<float>(rect.h);
                }
                float joint = worldPerPixel * 6.0f;
                const float lo = modelRadius * 0.002f, hi = modelRadius * 0.05f;
                if (joint < lo) joint = lo;
                if (joint > hi) joint = hi;
                // Use app.boneJointSize as base, scale by worldPerPixel
                float jointSize = app.boneJointSize * (joint / 0.03f);
                auto segs = boneSegments(a->skeleton, app, jointSize);
                if (app.xrayBones)
                    renderer.drawLinesXRay(segs, viewProj);
                else
                    renderer.drawLines(segs, viewProj);
                drawCalls++;
            }
            // MSE particle preview (Wave-30b): emission tick + cross draw.
            // Gated by the tab toggle, the in-tab preview checkbox, and a
            // loaded document — the headless smoke path never loads one, so no
            // ImGui clock is touched there. MseRuntime::update is
            // emission-stateless (it rebuilds instances from dt each call), so
            // Pause freezes the last instances and scrubbing the time slider
            // only moves the loop clock below, never history.
            if (mseWanted) {
                if (mseTickNow) {
                    float mseDt = ImGui::GetIO().DeltaTime;
                    if (mseDt < 0.0f) mseDt = 0.0f;
                    if (mseDt > 0.1f) mseDt = 0.1f;  // hitch-proof: no emission burst on resume
                    g_mseTime += mseDt;
                    // Global loop wrap (per-emitter loop flags approximated as
                    // loop; documented, not hidden).
                    if (g_mseTime < 0.0f) g_mseTime = 0.0f;
                    if (g_mseTime >= g_mseDuration) g_mseTime = 0.0f;
                    g_mse.update(mseDt, a->skeleton, framePalette);
                }
                const std::vector<ParticleOverlay> mseParts = g_mse.getActiveParticles();
                // Honest cap: at most 200 particles (600 segments); emitters
                // beyond the cap are dropped tail-first, never sampled.
                constexpr std::size_t kMseParticleCap = 200;
                const std::size_t mseCount =
                    mseParts.size() < kMseParticleCap ? mseParts.size() : kMseParticleCap;
                if (mseCount > 0) {
                    std::vector<GpuVertex> mseSegs;
                    mseSegs.reserve(mseCount * 6);
                    for (std::size_t pi = 0; pi < mseCount; ++pi) {
                        const ParticleOverlay& part = mseParts[pi];
                        float half = (std::isfinite(part.size) && part.size > 0.0f)
                                         ? part.size * 0.5f
                                         : modelRadius * 0.005f;
                        const float halfMax = modelRadius > 0.0f ? modelRadius * 0.05f : 1.0f;
                        if (half > halfMax) half = halfMax;
                        const Vec3 ticks[3] = {{half, 0.0f, 0.0f},
                                               {0.0f, half, 0.0f},
                                               {0.0f, 0.0f, half}};
                        for (int t = 0; t < 3; ++t) {
                            GpuVertex v0{}, v1{};
                            v0.position = part.worldPos - ticks[t];
                            v1.position = part.worldPos + ticks[t];
                            v0.normal = v1.normal = {0.0f, 1.0f, 0.0f};
                            v0.color[0] = v1.color[0] = part.color.x;
                            v0.color[1] = v1.color[1] = part.color.y;
                            v0.color[2] = v1.color[2] = part.color.z;
                            v0.color[3] = v1.color[3] = 1.0f;
                            mseSegs.push_back(v0);
                            mseSegs.push_back(v1);
                        }
                    }
                    // Depth-tested lines: particles hide honestly behind the mesh.
                    renderer.drawLines(mseSegs, viewProj);
                    drawCalls++;
                }
            }
        }
    }
    return drawCalls;
}

// Viewport: dock-proof offscreen rendering (Wave 21). The 3D scene renders
// into an offscreen D3D11 texture sized to the panel rect, composited via
// ImGui::Image(). This survives the occupied-central-node opaque dock
// background that hid the old backbuffer-direct scene (imgui.cpp:19577-19580
// draws full WindowBg when the central node is occupied, so NoBackground on
// the window alone was necessary but not sufficient). Camera input handled here.
ViewportRect drawViewportPanel(App& app, Renderer& renderer) {
    ViewportRect rect;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 240), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        // Ensure offscreen render target matches the panel size (physical pixels)
        const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
        const float sx = ImGui::GetIO().DisplayFramebufferScale.x;
        const float sy = ImGui::GetIO().DisplayFramebufferScale.y;
        
        // Update camera DPI scale for correct coordinate mapping
        app.camera.setDpiScale(sx, sy);
        
        rect.x = static_cast<int>((cursor.x - vpPos.x) * sx);
        rect.y = static_cast<int>((cursor.y - vpPos.y) * sy);
        rect.w = static_cast<int>(avail.x * sx);
        rect.h = static_cast<int>(avail.y * sy);
        rect.valid = rect.w > 8 && rect.h > 8;

        // Input handling (must happen before rendering so camera is up to date)
        // Note: We do NOT use SetNextItemAllowOverlap() because it lets overlay buttons
        // steal the drag start. Instead we track drag state globally via mouse state
        // and viewport rect hover check.
        ImGui::InvisibleButton("viewport_input", avail,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();
        bool gizmoUsing = false;
        bool gizmoOver = false;
        #ifdef M2RIG_WITH_GIZMO
        // Pass physical pixel rect for gizmo (matches offscreen render target)
        ImVec2 physCursor = ImVec2(cursor.x * sx, cursor.y * sy);
        ImVec2 physAvail = ImVec2(avail.x * sx, avail.y * sy);
        updateBoneGizmo(app, physCursor, physAvail, gizmoUsing, gizmoOver);
        #endif
        static bool btnDown = false;
        static ImVec2 downAt{0, 0};
        // Shading popover open: viewport gestures defer to the popup (same
        // arbitration spirit as the Wave-23 overlay fix — a press starting on
        // the Shading button or inside the popup must not orbit, pan, paint,
        // zoom, or click-select; reset/completion paths below are untouched).
        // cmd_palette defers exactly like shadingOpen: a press starting in the
        // palette must not orbit, pan, paint, zoom, or click-select (one
        // extended guard expression; all !shadingOpen uses below are untouched).
        const bool cmdPaletteOpen = ImGui::IsPopupOpen("cmd_palette", ImGuiPopupFlags_None);
        const bool shadingOpen = ImGui::IsPopupOpen("viewport_shading_pop", ImGuiPopupFlags_None) ||
                                 cmdPaletteOpen;
        // Track if mouse is over viewport rect (including overlay area) for drag continuity
        const bool mouseOverViewport = hovered || (io.MouseDown[0] && btnDown && !gizmoUsing);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            btnDown = true;
            downAt = io.MousePos;
            // Start box select on Shift+Click (without Ctrl) when not painting
            if (io.KeyShift && !io.KeyCtrl && !app.paintMode && !gizmoUsing && !gizmoOver) {
                app.boxSelecting = true;
                app.boxSelectStart = {io.MousePos.x, io.MousePos.y};
                app.boxSelectEnd = {io.MousePos.x, io.MousePos.y};
            }
        }
        if (mouseOverViewport) {
            // Update box select
            if (app.boxSelecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                app.boxSelectEnd = {io.MousePos.x, io.MousePos.y};
            }
            const bool paintDrag =
                app.paintMode && app.selectedBone >= 0 && !gizmoUsing && !shadingOpen &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left) && (io.KeyCtrl || io.KeyShift);
            const bool orbitDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !paintDrag &&
                                   !gizmoUsing && !io.KeyCtrl && !io.KeyShift && !app.boxSelecting &&
                                   !shadingOpen;
            if (paintDrag) {
                Vec3 hit;
                if (pickMeshPoint(app, avail.x, avail.y, io.MousePos.x - cursor.x,
                                  io.MousePos.y - cursor.y, hit))
                    app.paintStroke(hit);
            } else if (orbitDrag)
                app.camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
            else if ((ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                      ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) &&
                     !gizmoUsing && !shadingOpen)
                app.camera.pan(io.MouseDelta.x, io.MouseDelta.y);
            if (io.MouseWheel != 0.0f && !gizmoUsing && !shadingOpen) app.camera.zoom(io.MouseWheel);
            if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput && !gizmoUsing) {
                frameWholeModel(app, false);
            }
            const bool anyDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
            // Check if mouse is within viewport logical rect for hover highlight
            const bool mouseInViewport = io.MousePos.x >= cursor.x && io.MousePos.x <= cursor.x + avail.x &&
                                         io.MousePos.y >= cursor.y && io.MousePos.y <= cursor.y + avail.y;
            if (!anyDrag && !btnDown && !gizmoOver && !gizmoUsing && !app.boxSelecting && mouseInViewport) {
                app.hoveredBone = pickBoneAt(app, avail.x, avail.y, io.MousePos.x - cursor.x,
                                             io.MousePos.y - cursor.y);
                if (app.hoveredBone >= 0) {
                    if (const LoadedAsset* ha = app.currentAsset()) {
                        if (const Bone* hb = ha->skeleton.findById(
                                static_cast<std::uint32_t>(app.hoveredBone))) {
                            char tip[256];
                            std::snprintf(tip, sizeof(tip), "%s  [id %u]%s", hb->name.c_str(),
                                          hb->id,
                                          app.isBoneLocked(hb->id) ? "  [LOCKED]" : "");
                            ImGui::SetTooltip("%s", tip);
                        }
                    }
                }
            } else {
                app.hoveredBone = -1;
            }
        }
        // Viewport click-select (was documented but unhandled; btnDown/downAt
        // were dead state). Release with <6px drag, no gizmo, no Ctrl/Shift
        // promotes the hovered bone to selection (real core state, gpuDirty).
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (btnDown) {
                const float rdx = io.MousePos.x - downAt.x;
                const float rdy = io.MousePos.y - downAt.y;
                const bool click = (rdx * rdx + rdy * rdy) < 36.0f;
                if (click && mouseOverViewport && !gizmoUsing && !gizmoOver && !io.KeyShift && !app.boxSelecting && !shadingOpen) {
                    const int hit = pickBoneAt(app, avail.x, avail.y,
                                               io.MousePos.x - cursor.x, io.MousePos.y - cursor.y);
                    if (hit >= 0) {
                        // Ctrl+click toggles multi-select, plain click replaces.
                        const bool additive = io.KeyCtrl != 0;
                        if (additive && app.isBoneSelected(static_cast<std::uint32_t>(hit)) &&
                            app.selectedBones.size() > 1) {
                            app.selectedBones.erase(static_cast<std::uint32_t>(hit));
                            app.selectedBone = static_cast<int>(*app.selectedBones.rbegin());
                        } else {
                            app.selectBone(static_cast<std::uint32_t>(hit), additive);
                        }
                        if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
                    } else if (!io.KeyCtrl) {
                        app.clearBoneSelection();
                    }
                }
            }
            // Handle box select completion
            if (app.boxSelecting) {
                app.boxSelecting = false;
                if (LoadedAsset* a = app.currentAsset()) {
                    // Compute screen-space box
                    const float minX = std::min(app.boxSelectStart.x, app.boxSelectEnd.x);
                    const float maxX = std::max(app.boxSelectStart.x, app.boxSelectEnd.x);
                    const float minY = std::min(app.boxSelectStart.y, app.boxSelectEnd.y);
                    const float maxY = std::max(app.boxSelectStart.y, app.boxSelectEnd.y);
                    
                    // Use logical coordinates for aspect ratio to match viewport
                    const float aspect = avail.x / avail.y;
                    const Mat4 vp = app.camera.viewMatrix() * app.camera.projMatrix(aspect);
                    
// Find all bones within the box
                    std::vector<std::uint32_t> selectedInBox;
                    for (const auto& b : a->skeleton.bones) {
                        if (!app.boneVisible(b.id)) continue;
                        
                        const Vec3 joint{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                                         b.globalTransform.m[3][2]};
                        
                        // Project to screen space
                        const Vec4 clip = {
                            joint.x * vp.m[0][0] + joint.y * vp.m[1][0] + joint.z * vp.m[2][0] + vp.m[3][0],
                            joint.x * vp.m[0][1] + joint.y * vp.m[1][1] + joint.z * vp.m[2][1] + vp.m[3][1],
                            joint.x * vp.m[0][2] + joint.y * vp.m[1][2] + joint.z * vp.m[2][2] + vp.m[3][2],
                            joint.x * vp.m[0][3] + joint.y * vp.m[1][3] + joint.z * vp.m[2][3] + vp.m[3][3]
                        };
                        
                        if (clip.w <= 0.0f) continue;
                        
                        const float ndcX = clip.x / clip.w;
                        const float ndcY = clip.y / clip.w;
                        const float ndcZ = clip.z / clip.w;
                        
                        if (ndcZ < 0.0f || ndcZ > 1.0f) continue;
                        
                        const float screenX = cursor.x + (ndcX * 0.5f + 0.5f) * avail.x;
                        const float screenY = cursor.y + (1.0f - (ndcY * 0.5f + 0.5f)) * avail.y;
                        
                        if (screenX >= minX && screenX <= maxX && screenY >= minY && screenY <= maxY) {
                            selectedInBox.push_back(b.id);
                        }
                    }
                    
                    if (!selectedInBox.empty()) {
                        const bool additive = io.KeyCtrl != 0;
                        if (!additive) {
                            app.clearBoneSelection();
                        }
                        for (std::uint32_t boneId : selectedInBox) {
                            app.selectBone(boneId, true);
                        }
                        if (!selectedInBox.empty()) {
                            app.selectedBone = static_cast<int>(selectedInBox.back());
                        }
                        a->gpuDirty = true;
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "Box selected %zu bones.", selectedInBox.size());
                        app.setStatus(buf, "success");
                    }
                }
            }
            btnDown = false;
        }
        if (!mouseOverViewport && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) btnDown = false;

        // Update camera smooth interpolation
        app.camera.updateSmooth(ImGui::GetTime());
        
        // Render 3D scene to offscreen texture if viewport is valid
        static const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
        if (rect.valid) {
            std::string err;
            if (renderer.ensureViewportTarget(rect.w, rect.h, err)) {
                if (renderer.beginViewportPass(clear)) {
                    // Use logical coordinates for aspect ratio to avoid stretch on multi-DPI
                    const float aspect = avail.x / avail.y;
                    const Mat4 view = app.camera.viewMatrix();
                    renderer.setSceneView(view);
                    const Mat4 vp = view * app.camera.projMatrix(aspect);

                    // Compute model radius for adaptive grid/joint sizing
                    float modelRadius = 2.8f;
                    if (const LoadedAsset* ga = app.currentAsset()) {
                        if (!ga->mesh.bounds.empty) {
                            const float r = ga->mesh.bounds.radius();
                            if (r > 0.01f) modelRadius = r;
                        }
                    }

                    drawSceneContents(app, renderer, vp, rect, modelRadius);
                    renderer.endViewportPass();
                }
            } else {
                app.setStatus("Viewport target failed: " + err, "error");
            }
        }

        // Display the offscreen render target via ImGui::Image()
        // D3D11 texture origin is top-left (same as ImGui), so UV (0,0)->(1,1) is correct.
        // No V-flip needed (that was an OpenGL convention).
        if (rect.valid && renderer.viewportSrv()) {
            ImGui::GetWindowDrawList()->AddImage(
                renderer.viewportSrv(),
                cursor,
                ImVec2(cursor.x + avail.x, cursor.y + avail.y),
                ImVec2(0, 0), ImVec2(1, 1)
            );
        }

        // Render bone labels as ImGui overlay (world to screen projection)
        LoadedAsset* asset = app.currentAsset();
        if (app.showBones && app.boneShowLabels && rect.valid && asset) {
            // Use logical coordinates for aspect ratio to match viewport
            const float aspect = avail.x / avail.y;
            const Mat4 vp = app.camera.viewMatrix() * app.camera.projMatrix(aspect);
            
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            for (const auto& b : asset->skeleton.bones) {
                if (!app.boneVisible(b.id)) continue;
                
                const Vec3 joint{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                                 b.globalTransform.m[3][2]};
                
                // Project to screen space
                const Vec4 clip = {
                    joint.x * vp.m[0][0] + joint.y * vp.m[1][0] + joint.z * vp.m[2][0] + vp.m[3][0],
                    joint.x * vp.m[0][1] + joint.y * vp.m[1][1] + joint.z * vp.m[2][1] + vp.m[3][1],
                    joint.x * vp.m[0][2] + joint.y * vp.m[1][2] + joint.z * vp.m[2][2] + vp.m[3][2],
                    joint.x * vp.m[0][3] + joint.y * vp.m[1][3] + joint.z * vp.m[2][3] + vp.m[3][3]
                };
                
                if (clip.w <= 0.0f) continue; // Behind camera
                
                const float ndcX = clip.x / clip.w;
                const float ndcY = clip.y / clip.w;
                const float ndcZ = clip.z / clip.w;
                
                if (ndcZ < 0.0f || ndcZ > 1.0f) continue; // Outside near/far
                
                const float screenX = cursor.x + (ndcX * 0.5f + 0.5f) * avail.x;
                const float screenY = cursor.y + (1.0f - (ndcY * 0.5f + 0.5f)) * avail.y;
                
                // Check if on screen
                if (screenX < cursor.x || screenX > cursor.x + avail.x ||
                    screenY < cursor.y || screenY > cursor.y + avail.y) continue;
                
                // Determine label color based on bone state
                ImU32 labelCol = IM_COL32(230, 230, 240, 255);
                if (app.isBoneSelected(b.id) || static_cast<int>(b.id) == app.selectedBone) {
                    labelCol = IM_COL32(255, 217, 51, 255);
                } else if (static_cast<int>(b.id) == app.hoveredBone) {
                    labelCol = IM_COL32(255, 128, 25, 255);
                } else if (app.isBoneLocked(b.id)) {
                    labelCol = IM_COL32(128, 128, 128, 255);
                }
                
                // Draw label with background for readability
                const char* label = b.name.c_str();
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                const float padding = 2.0f;
                const ImVec2 bgMin(screenX - textSize.x * 0.5f - padding, screenY - textSize.y - padding);
                const ImVec2 bgMax(screenX + textSize.x * 0.5f + padding, screenY + padding);
                drawList->AddRectFilled(bgMin, bgMax, IM_COL32(10, 10, 15, 200), 3.0f);
                drawList->AddText(ImVec2(screenX - textSize.x * 0.5f, screenY - textSize.y), labelCol, label);
            }
        }

        // Draw box selection rectangle
        if (app.boxSelecting) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 min(app.boxSelectStart.x, app.boxSelectStart.y);
            const ImVec2 max(app.boxSelectEnd.x, app.boxSelectEnd.y);
            const ImVec2 rectMin(std::min(min.x, max.x), std::min(min.y, max.y));
            const ImVec2 rectMax(std::max(min.x, max.x), std::max(min.y, max.y));
            drawList->AddRect(rectMin, rectMax, IM_COL32(80, 170, 255, 255), 0.0f, 0, 2.0f);
            drawList->AddRectFilled(rectMin, rectMax, IM_COL32(80, 170, 255, 50));
        }

        // Overlay controls.
        ImGui::SetCursorScreenPos(cursor + ImVec2(8, 8));
        ImGui::BeginDisabled(app.currentAsset() == nullptr);
        if (ImGui::Button("Frame (F)")) {
            frameWholeModel(app, true);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fit the whole model in view (F)");
        ImGui::SameLine();
        const char* proj = app.camera.orthographic ? "Ortho" : "Persp";
        if (ImGui::Button(proj)) app.camera.setOrthographic(!app.camera.orthographic, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle perspective / orthographic");
        ImGui::SameLine();
        if (ImGui::Button("Front")) app.camera.applyPreset(m2rig::CameraPreset::Front, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to front (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Back")) app.camera.applyPreset(m2rig::CameraPreset::Back, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to back (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Top")) app.camera.applyPreset(m2rig::CameraPreset::Top, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to top (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Bottom")) app.camera.applyPreset(m2rig::CameraPreset::Bottom, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to bottom (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Left")) app.camera.applyPreset(m2rig::CameraPreset::Left, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to left (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Right")) app.camera.applyPreset(m2rig::CameraPreset::Right, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to right (keeps framing)");
        // Two-row wrap when narrow: presets stay on row 1, Shading drops to
        // row 2 so the 9-button row never clips at 720p widths.
        if (avail.x < 720.0f) {
            ImGui::SetCursorScreenPos(cursor + ImVec2(8, 8 + ImGui::GetFrameHeight() + 4.0f));
        } else {
            ImGui::SameLine();
        }
        // Shading popover: the same view-mode + overlay switches as the
        // toolbar, where viewport-focused users look (single bools, no
        // duplicate state — Blender-style header pattern).
        if (ImGui::Button("Shading")) ImGui::OpenPopup("viewport_shading_pop");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shading mode + overlays");
        if (ImGui::BeginPopup("viewport_shading_pop")) {
            // Scrollable body (max ~300px) so the popover never runs off
            // screen at 720p. Popup ID + shadingOpen guard above unchanged.
            if (ImGui::BeginChild("viewport_shading_scroll", ImVec2(0, 300.0f), true)) {
                const char* modes[] = {"Solid",     "Wireframe", "Solid + Wire", "Normals",
                                       "Height",    "Weights",   "UV"};
                for (int i = 0; i < 7; ++i) {
                    if (ImGui::RadioButton(modes[i], app.viewMode == static_cast<ViewMode>(i))) {
                        app.viewMode = static_cast<ViewMode>(i);
                        if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                        if (i == 5 && app.selectedBone < 0)
                            app.setStatus("Weights view needs a selected bone.", "warning");
                    }
                }
                ImGui::Separator();
                ImGui::Checkbox("X-ray bones", &app.xrayBones);
                if (ImGui::Checkbox("Textured", &app.textured)) {
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
                ImGui::Checkbox("PBR shading", &app.usePbr);
            ImGui::Checkbox("Wire overlay", &app.showWireOverlay);
            ImGui::Checkbox("Grid", &app.showGrid);
            ImGui::Checkbox("Bones", &app.showBones);
            ImGui::Separator();
            if (ImGui::Checkbox("Paint mode", &app.paintMode)) {
                if (app.paintMode && app.selectedBone < 0)
                    app.setStatus("Paint mode: select a bone first (click in Skeleton or viewport)",
                                  "warning");
            }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        if (const LoadedAsset* a = app.currentAsset()) {
            ImVec2 overlay = cursor + ImVec2(8, avail.y - 44);
            ImGui::SetCursorScreenPos(overlay);
            ImGui::TextDisabled("%s | %zu v / %zu t | bone %d%s", a->id.c_str(),
                                a->mesh.vertices.size(), a->mesh.triangleCount(),
                                app.selectedBone, app.paintMode ? " | PAINT (Ctrl+drag)" : "");
            ImVec2 diag = cursor + ImVec2(8, avail.y - 24);
            ImGui::SetCursorScreenPos(diag);
            const Vec3 eye = app.camera.eye();
            ImGui::TextDisabled("view %dx%d%s | eye (%.1f,%.1f,%.1f) d=%.1f | grid %d bones %d",
                                rect.w, rect.h, rect.valid ? "" : " INVALID",
                                eye.x, eye.y, eye.z, app.camera.distance,
                                app.showGrid ? 1 : 0, app.showBones ? 1 : 0);
        } else {
            // Measured centering (no fixed -150px offsets): each line is
            // centered via CalcTextSize, with PushTextWrapPos so long lines
            // wrap inside the viewport instead of clipping. Same strings.
            const ImVec2 center = cursor + ImVec2(avail.x * 0.5f, avail.y * 0.45f);
            const float wrapX = cursor.x + avail.x - 16.0f;
            auto centeredLine = [&](const char* text, float yOff, bool disabled) {
                const ImVec2 sz = ImGui::CalcTextSize(text);
                float x = center.x - sz.x * 0.5f;
                if (x < cursor.x + 8.0f) x = cursor.x + 8.0f;
                ImGui::SetCursorScreenPos(ImVec2(x, center.y + yOff));
                ImGui::PushTextWrapPos(wrapX);
                if (disabled)
                    ImGui::TextDisabled("%s", text);
                else
                    ImGui::Text("%s", text);
                ImGui::PopTextWrapPos();
            };
            centeredLine("No model loaded", -40.0f, false);
            centeredLine("1. Project > Import FBX/GR2  or  Project > Load sample armor", -20.0f,
                         true);
            centeredLine("2. Press F to frame the model in view", -4.0f, true);
            centeredLine("Drag = orbit | Right-drag = pan | Wheel = zoom", 12.0f, true);
            ImVec2 overlay = cursor + ImVec2(8, avail.y - 44);
            ImGui::SetCursorScreenPos(overlay);
            ImGui::TextDisabled("No asset loaded — Project > Load sample armor");
            ImVec2 diag = cursor + ImVec2(8, avail.y - 24);
            ImGui::SetCursorScreenPos(diag);
            ImGui::TextDisabled("view %dx%d%s | grid %d", rect.w, rect.h,
                                rect.valid ? "" : " INVALID", app.showGrid ? 1 : 0);
        }
        if (!rect.valid) {
            ImVec2 warn = cursor + ImVec2(8, 32);
            ImGui::SetCursorScreenPos(warn);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "Viewport too small — enlarge the Viewport panel");
        }
        // Paint brush circle overlay.
        if (app.paintMode && hovered && avail.y > 0) {
            const float eyeDist = distance(app.camera.eye(), app.camera.target);
            float worldPerPixel = 0.01f;
            if (app.camera.orthographic)
                worldPerPixel = app.camera.orthoHeight / avail.y;
            else
                worldPerPixel = 2.0f * eyeDist * std::tan(app.camera.fovY * 0.5f) / avail.y;
            if (worldPerPixel > 1e-9f) {
                const float r = app.brushRadius / worldPerPixel;
                const ImU32 ringCol = app.selectedBone >= 0 ? IM_COL32(80, 170, 255, 255)
                                                            : IM_COL32(255, 90, 70, 255);
                ImGui::GetWindowDrawList()->AddCircle(ImGui::GetIO().MousePos, r, ringCol, 40,
                                                      2.0f);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return rect;
}

void drawSettingsPanel(App& app) {
    ImGui::Text("Application Settings");
    ImGui::Separator();

    // NOTE: Export / Tools & Paths / Autosave sections were removed here as
    // duplicates — the single sources of truth are the Export panel
    // (validation gate + checklist), the Tools > System tab (bridge paths +
    // Data/Models scan) and the Project panel (autosave interval + Save now).
    // Profiles below stays read-only.
    if (ImGui::CollapsingHeader("Metin2 Profiles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Skeleton profiles define bone hierarchy, mirrors, sockets.");
        if (const LoadedAsset* a = app.currentAsset()) {
            ImGui::Text("Current profile: %s", a->profileId.c_str());
            if (const SkeletonProfile* p = findProfile(a->profileId); p) {
                ImGui::Text("Identity: %s | Race: %s | Gender: %s",
                            p->identity.c_str(), p->race.c_str(), p->gender.c_str());
                ImGui::Text("Expected bones: %zu | Socket bones: %zu",
                            p->expectedBones.size(), p->socketBones.size());
            }
        }
        ImGui::Separator();
        ImGui::Text("Available profiles:");
        for (const auto& p : allBuiltinProfiles()) {
            bool isCurrent = false;
            if (const LoadedAsset* a = app.currentAsset()) isCurrent = (a->profileId == p.identity);
            ImGui::Text("%s %s (%s/%s)%s",
                        isCurrent ? ">>" : "  ",
                        p.identity.c_str(), p.race.c_str(), p.gender.c_str(),
                        isCurrent ? " [ACTIVE]" : "");
        }
    }

    ImGui::Separator();
    ImGui::Text("Layout presets (apply instantly — the 14 panel flags are live-read every frame):");
    // 4x14 table; columns follow App::UISettings declaration order: Bone,
    // Weights, Materials, MSMInspector, Project, Export, Validation, Console,
    // System/Tools, Timeline, Settings, BoneDisplay, Gizmo, ViewportSettings.
    static const bool kLayoutPresets[4][14] = {
        // Rig: bone + weights + gizmo + bone display + validation.
        {true, true, false, false, true, false, true, false, false, false, false, true,
         true, false},
        // Paint: weights + materials + viewport settings (brush context).
        {true, true, true, false, false, false, false, false, false, false, false, false,
         false, true},
        // Anim: bone + timeline + gizmo.
        {true, false, false, false, false, false, false, false, false, true, false, false,
         true, false},
        // Review: materials + export + validation + console + tools + timeline.
        {false, false, true, false, false, true, true, true, true, true, false, false,
         false, false},
    };
    static const char* kLayoutPresetNames[4] = {"Rig", "Paint", "Anim", "Review"};
    static const char* kLayoutPresetTips[4] = {
        "Rigging: Bone + Weights + Gizmo + Bone Display + Validation",
        "Paint: Bone + Weights + Materials + Viewport Settings",
        "Animation: Bone + Timeline + Gizmo",
        "Review: Materials + Export + Validation + Console + Tools + Timeline",
    };
    for (int pi = 0; pi < 4; ++pi) {
        if (pi > 0) ImGui::SameLine();
        if (ImGui::Button(kLayoutPresetNames[pi])) {
            bool* flags[14] = {
                &app.uiSettings.showBonePanel,         &app.uiSettings.showWeightsPanel,
                &app.uiSettings.showMaterialsPanel,    &app.uiSettings.showMSMInspectorPanel,
                &app.uiSettings.showProjectPanel,      &app.uiSettings.showExportPanel,
                &app.uiSettings.showValidationPanel,   &app.uiSettings.showConsolePanel,
                &app.uiSettings.showSystemPanel,       &app.uiSettings.showTimelinePanel,
                &app.uiSettings.showSettingsPanel,     &app.uiSettings.showBoneDisplayPanel,
                &app.uiSettings.showGizmoPanel,        &app.uiSettings.showViewportSettingsPanel,
            };
            for (int f = 0; f < 14; ++f) *flags[f] = kLayoutPresets[pi][f];
            app.setStatus(std::string("Layout preset applied: ") + kLayoutPresetNames[pi],
                          "success");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kLayoutPresetTips[pi]);
    }

    ImGui::Separator();
    if (ImGui::Button("Reset Viewport Layout")) {
        const ImGuiID resetId = ImGui::GetID("M2RigDockSpace");
        std::error_code ec;
        const char* ini = ImGui::GetIO().IniFilename;
        if (ini) std::filesystem::remove(ini, ec);  // keep the imgui.ini delete
        requestDockRebuild();
        buildDefaultDockLayout(resetId);  // rebuild NOW — no restart (the next
                                          // DockSpace in this/next frame picks it up)
        markDockBuilt();
        app.setStatus("Layout reset — rebuilt live", "success");
    }
}
void drawBoneDisplayPanel(App& app) {
    ImGui::Text("Bone Visualization");
    ImGui::Separator();
    ImGui::SliderFloat("Bone Thickness", &app.boneThickness, 0.5f, 3.0f, "%.2f");
    ImGui::SliderFloat("Joint Size", &app.boneJointSize, 0.01f, 0.1f, "%.3f");
    ImGui::Checkbox("Show Labels", &app.boneShowLabels);
    ImGui::Checkbox("Show Joint Crosses", &app.boneShowJointCrosses);
    ImGui::Separator();
    ImGui::Text("Colors");
    ImGui::ColorEdit4("Selected", app.boneSelectedColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Selected bone");
    ImGui::ColorEdit4("Hovered", app.boneHoveredColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Hovered bone");
    ImGui::ColorEdit4("Locked", app.boneLockedColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Locked bone");
    ImGui::ColorEdit4("Parent", app.boneParentColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Parent of selected");
    ImGui::ColorEdit4("Child", app.boneChildColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Child of selected");
    ImGui::ColorEdit4("Default", app.boneDefaultColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Default bone");
    ImGui::ColorEdit4("Hidden", app.boneHiddenColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Hidden bone");
    ImGui::ColorEdit4("Socket", app.boneSocketColor, ImGuiColorEditFlags_NoInputs);
    ImGui::SameLine(); ImGui::Text("Socket bones (equip_*/stip)");
    if (ImGui::Button("Reset Colors")) {
        app.boneSelectedColor[0] = 1.0f; app.boneSelectedColor[1] = 0.85f; app.boneSelectedColor[2] = 0.2f; app.boneSelectedColor[3] = 1.0f;
        app.boneHoveredColor[0] = 1.0f; app.boneHoveredColor[1] = 0.5f; app.boneHoveredColor[2] = 0.1f; app.boneHoveredColor[3] = 1.0f;
        app.boneLockedColor[0] = 0.5f; app.boneLockedColor[1] = 0.5f; app.boneLockedColor[2] = 0.5f; app.boneLockedColor[3] = 1.0f;
        app.boneParentColor[0] = 0.3f; app.boneParentColor[1] = 0.9f; app.boneParentColor[2] = 0.3f; app.boneParentColor[3] = 1.0f;
        app.boneChildColor[0] = 0.3f; app.boneChildColor[1] = 0.3f; app.boneChildColor[2] = 0.9f; app.boneChildColor[3] = 1.0f;
        app.boneDefaultColor[0] = 0.9f; app.boneDefaultColor[1] = 0.9f; app.boneDefaultColor[2] = 0.95f; app.boneDefaultColor[3] = 1.0f;
        app.boneHiddenColor[0] = 0.35f; app.boneHiddenColor[1] = 0.38f; app.boneHiddenColor[2] = 0.44f; app.boneHiddenColor[3] = 1.0f;
        app.boneSocketColor[0] = 0.3f; app.boneSocketColor[1] = 0.9f; app.boneSocketColor[2] = 0.9f; app.boneSocketColor[3] = 1.0f;
    }
}

void drawGizmoPanel(App& app) {
    ImGui::Text("Gizmo Settings");
    ImGui::Separator();
#ifdef M2RIG_WITH_GIZMO
    ImGui::Text("Operation");
    int gop = app.gizmoOp == GizmoOp::Translate ? 0 : (app.gizmoOp == GizmoOp::Rotate ? 1 : 2);
    if (ImGui::RadioButton("Translate", &gop, 0)) app.gizmoOp = GizmoOp::Translate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", &gop, 1)) app.gizmoOp = GizmoOp::Rotate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", &gop, 2)) app.gizmoOp = GizmoOp::Scale;
    ImGui::Separator();
    ImGui::Text("Space");
    int gsp = app.gizmoSpace == GizmoSpace::Local    ? 1
                : app.gizmoSpace == GizmoSpace::Parent ? 2
                                                        : 0;
    if (ImGui::RadioButton("World", &gsp, 0)) app.gizmoSpace = GizmoSpace::World;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gizmo handles aligned to world axes");
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", &gsp, 1)) app.gizmoSpace = GizmoSpace::Local;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Gizmo handles aligned to the bone's own orientation");
    ImGui::SameLine();
    if (ImGui::RadioButton("Parent", &gsp, 2)) app.gizmoSpace = GizmoSpace::Parent;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Gizmo handles aligned to the parent bone (root degrades to world).\n"
            "Scale applies parent-axis ratios (approximate under non-uniform "
            "parent scale).");
    ImGui::Separator();
    ImGui::Text("Snap");
    if (ImGui::Checkbox("Enable", &app.gizmoSnap)) {
        if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step snapping for gizmo drags");
    if (app.gizmoSnap) {
        ImGui::Indent();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Move step", &app.snapTranslate, 0.01f, 0.001f, 100.0f, "%.3f");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Rotate step (deg)", &app.snapRotateDeg, 0.5f, 0.5f, 90.0f, "%.1f");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Scale step", &app.snapScale, 0.01f, 0.001f, 1.0f, "%.3f");
        if (app.snapTranslate < 1e-6f) app.snapTranslate = 1e-6f;
        if (app.snapRotateDeg < 1e-3f) app.snapRotateDeg = 1e-3f;
        if (app.snapScale < 1e-6f) app.snapScale = 1e-6f;
        ImGui::Unindent();
    }
    ImGui::Separator();
    ImGui::Text("Display");
    ImGui::Checkbox("Show Axis Labels", &app.gizmoShowAxisLabels);
    ImGui::Checkbox("Show Plane Handles", &app.gizmoShowPlaneHandles);
    ImGui::Checkbox("Show Center Handle", &app.gizmoShowCenterHandle);
    ImGui::SliderFloat("Handle Size", &app.gizmoHandleSize, 0.5f, 2.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale factor for gizmo handle size");
#else
    ImGui::TextDisabled("Gizmo disabled at build time (M2RIG_WITH_GIZMO=OFF).");
#endif
}

void drawViewportSettingsPanel(App& app) {
    ImGui::Text("Viewport Settings");
    ImGui::Separator();

    // Camera settings - use local degree variable for FOV slider
    static float fovDegrees = 50.0f;
    static bool fovSynced = false;
    if (!fovSynced) {
        fovDegrees = app.camera.fovY * kRadToDeg;
        fovSynced = true;
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("FOV (degrees)", &fovDegrees, 10.0f, 120.0f, "%.1f")) {
            app.camera.fovY = fovDegrees * kDegToRad;
            app.camera.updateClip();
        }
        ImGui::SliderFloat("Near Plane", &app.camera.nearZ, 0.001f, 10.0f, "%.3f");
        ImGui::SliderFloat("Far Plane", &app.camera.farZ, 10.0f, 10000.0f, "%.0f");
        ImGui::Checkbox("Orthographic", &app.camera.orthographic);
        if (app.camera.orthographic) {
            ImGui::Indent();
            ImGui::SliderFloat("Ortho Height", &app.camera.orthoHeight, 0.01f, 500.0f, "%.2f");
            ImGui::Unindent();
        }
        if (ImGui::Button("Reset Camera")) {
            app.camera = ArcballCamera();
            fovDegrees = app.camera.fovY * kRadToDeg;
            if (const LoadedAsset* a = app.currentAsset()) app.camera.frameAabb(a->mesh.bounds);
        }
    }

    if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("VSync", &app.vsync);
        ImGui::Checkbox("Show Grid", &app.showGrid);
        ImGui::Checkbox("Show Bones", &app.showBones);
        ImGui::Checkbox("X-Ray Bones", &app.xrayBones);
        ImGui::Checkbox("Wire Overlay", &app.showWireOverlay);
        ImGui::Checkbox("Textured View", &app.textured);
        ImGui::Separator();
        ImGui::Text("Skinning:");
        ImGui::Checkbox("DQS (Dual Quaternion)", &app.useDqs);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dual Quaternion Skinning -- avoids candy-wrapper artifacts on twist joints.\nRequires previewDeform=ON for animated deformation preview.");
    }
}

namespace {

// Dock structure shared by first-run setup and both live Reset paths.
// (Kept separate from the panel-visibility flags: those are live-read every
// frame, this rebuilds the node tree. Viewport compositing — NoBackground +
// offscreen texture — is not touched here.)
void buildDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    // NOTE: no PassthruCentralNode here on purpose: DockSpace lives in
    // the internal enum (imgui_internal.h) while PassthruCentralNode is
    // public (imgui.h) — OR-ing them is a C5054 type mismatch. The
    // documented pattern is the flag on DockSpaceOverViewport below;
    // the docked Viewport window itself stays transparent via
    // ImGuiWindowFlags_NoBackground (load-bearing, see drawViewportPanel).
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
    ImGuiID dockMain = dockspaceId;
    ImGuiID dockLeft =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.17f, nullptr, &dockMain);
    ImGuiID dockRight =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.35f, nullptr, &dockMain);
    ImGuiID dockBottom =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.24f, nullptr, &dockMain);
    ImGuiID dockTop =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.075f, nullptr, &dockMain);
    // Split right into Properties (60%) + Workflow (40%)
    ImGuiID dockRightProps = dockRight;
    ImGuiID dockRightWorkflow =
        ImGui::DockBuilderSplitNode(dockRightProps, ImGuiDir_Right, 0.4f, nullptr, &dockRightProps);
    // Split bottom into Output (60%) + Tools (40%)
    ImGuiID dockBottomOut = dockBottom;
    ImGuiID dockBottomTools =
        ImGui::DockBuilderSplitNode(dockBottomOut, ImGuiDir_Right, 0.4f, nullptr, &dockBottomOut);

    ImGui::DockBuilderDockWindow("Toolbar", dockTop);
    ImGui::DockBuilderDockWindow("Assets", dockLeft);
    ImGui::DockBuilderDockWindow("Scene", dockLeft);
    ImGui::DockBuilderDockWindow("Skeleton", dockLeft);
    ImGui::DockBuilderDockWindow("Viewport", dockMain);
    // Right Properties column
    ImGui::DockBuilderDockWindow("Bone", dockRightProps);
    ImGui::DockBuilderDockWindow("Weights", dockRightProps);
    ImGui::DockBuilderDockWindow("Materials", dockRightProps);
    ImGui::DockBuilderDockWindow("Bone Display", dockRightProps);
    ImGui::DockBuilderDockWindow("Gizmo", dockRightProps);
    ImGui::DockBuilderDockWindow("Viewport Settings", dockRightProps);
    // Right Workflow column
    ImGui::DockBuilderDockWindow("Export", dockRightWorkflow);
    ImGui::DockBuilderDockWindow("Project", dockRightWorkflow);
    ImGui::DockBuilderDockWindow("Settings", dockRightWorkflow);
    // Bottom Output
    ImGui::DockBuilderDockWindow("Validation", dockBottomOut);
    ImGui::DockBuilderDockWindow("Console", dockBottomOut);
    // Bottom Tools
    ImGui::DockBuilderDockWindow("Timeline", dockBottomTools);
    ImGui::DockBuilderDockWindow("Tools", dockBottomTools);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace

void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport);

// Renders the 3D scene into the viewport rect (called between ImGui frame
// setup and ImGui::Render, after drawAllPanels determined the rect).
void renderScene(App& app, Renderer& renderer, const ViewportRect& rect) {
    const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
    if (!rect.valid) {
        static bool loggedInvalid = false;
        if (!loggedInvalid) {
            loggedInvalid = true;
            Logger::instance().warning("renderScene: viewport rect invalid, skipping D3D pass.", "app");
        }
        // Keep the swap-chain RTV bound for the subsequent ImGui pass even
        // while docking/resize produces a transiently invalid viewport.
        renderer.bindBackbuffer();
        renderer.clearBackbuffer(clear);
        return;
    }
    if (!renderer.beginScenePass(rect.x, rect.y, rect.w, rect.h, clear)) {
        renderer.endScenePass();
        return;
    }
    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 view = app.camera.viewMatrix();
    renderer.setSceneView(view);
    const Mat4 vp = view * app.camera.projMatrix(aspect);
    // Single shared scene path (no duplicated grid/mesh/bone logic): the
    // offscreen viewport and this backbuffer fallback render identically.
    float modelRadius = 2.8f;
    if (const LoadedAsset* ga = app.currentAsset()) {
        if (!ga->mesh.bounds.empty) {
            const float r = ga->mesh.bounds.radius();
            if (r > 0.01f) modelRadius = r;
        }
    }
    drawSceneContents(app, renderer, vp, rect, modelRadius);
    renderer.endScenePass();
}

void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport,
                   const std::filesystem::path& projectsDir, bool& showRecovery) {
    // Autosave tick (wall clock; only when something is dirty).
    app.tickAutosave(projectsDir, ImGui::GetTime());
    // Async bridge import completion (UI thread applies the result).
    app.pollBridgeImport();
    if (showRecovery) {
        ImGui::OpenPopup("Crash recovery");
        showRecovery = false;
    }
    if (ImGui::BeginPopupModal("Crash recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The previous session did not exit cleanly.");
        ImGui::TextDisabled("Restore the autosaved workspace?");
        if (ImGui::Button("Restore autosave")) {
            if (auto r = app.loadWorkspaceFile((projectsDir / "autosave.m2rig").string()); !r)
                app.setStatus("Recovery failed: " + r.error().message, "error");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            std::error_code ec;
            std::filesystem::remove(projectsDir / "autosave.m2rig", ec);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Load sample armor")) {
                if (auto r = app.loadSampleArmor(); !r)
                    app.setStatus("Sample load failed: " + r.error().message, "error");
            }
            if (ImGui::MenuItem("Import SMD...")) doImportSmd(app);
            ImGui::BeginDisabled(app.bridgeBusy);
            if (ImGui::MenuItem("Import FBX/GR2 (bridge)...")) doImportBridged(app);
            if (ImGui::MenuItem("Import glTF...")) doImportGltf(app);
            ImGui::EndDisabled();
            if (ImGui::MenuItem("Export SMD...")) doExportSmd(app);
            if (ImGui::MenuItem("Export MSM...")) doExportMsm(app);
            if (ImGui::MenuItem("Export GR2 (bridge)...")) doExportGr2(app);
            if (ImGui::MenuItem("Export FBX (Noesis)...")) doExportFbx(app);
            if (ImGui::MenuItem("Export ANI...")) doExportAni(app);
            if (ImGui::MenuItem("GR2 -> FBX (Noesis)...")) doExportGr2ToFbx(app);
            if (ImGui::MenuItem("Export LOD SMD...")) doExportLod(app, app.prefs.lodRatio);
            if (ImGui::MenuItem("Export all loaded (SMD+MSM)...")) {
                if (auto r = app.exportAllBatch(); !r)
                    app.setStatus("Batch failed: " + r.error().message, "error");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save workspace...")) doSaveWorkspace(app);
            if (ImGui::MenuItem("Load workspace...")) doLoadWorkspace(app);
            ImGui::Separator();
            ImGui::BeginDisabled(!app.canUndo());
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) app.undo();
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!app.canRedo());
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) app.redo();
            ImGui::EndDisabled();
            if (ImGui::MenuItem("Run validation")) app.runValidation();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            auto flag = [&](const char* label, const char* shortcut, bool* v) {
                if (ImGui::MenuItem(label, shortcut, v)) {
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
            };
            flag("Grid", "G", &app.showGrid);
            flag("Bones", "B", &app.showBones);
            flag("X-ray bones", "X", &app.xrayBones);
            flag("Wire overlay", "W", &app.showWireOverlay);
            if (ImGui::MenuItem("Textured", "T", &app.textured)) {
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            ImGui::MenuItem("PBR shading", nullptr, &app.usePbr);
            ImGui::MenuItem("Paint mode", "P", &app.paintMode);
            if (ImGui::MenuItem("Deform preview", "D", &app.previewDeform)) {
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            ImGui::Separator();
            const char* modes[] = {"Solid",     "Wireframe", "Solid + Wire", "Normals",
                                   "Height",    "Weights",   "UV"};
            for (int i = 0; i < 7; ++i) {
                char shortcut[2] = {static_cast<char>('1' + i), '\0'};
                if (ImGui::MenuItem(modes[i], shortcut, app.viewMode == static_cast<ViewMode>(i))) {
                    app.viewMode = static_cast<ViewMode>(i);
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("Orthographic", nullptr, &app.camera.orthographic);
            ImGui::BeginDisabled(app.currentAsset() == nullptr);
            if (ImGui::MenuItem("Frame all", "F")) {
                frameWholeModel(app, false);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Fit the whole model in view (F)");
            ImGui::MenuItem("VSync", nullptr, &app.vsync);
            ImGui::Separator();
            if (ImGui::BeginMenu("Panels")) {
                ImGui::MenuItem("Bone", nullptr, &app.uiSettings.showBonePanel);
                ImGui::MenuItem("Weights", nullptr, &app.uiSettings.showWeightsPanel);
                ImGui::MenuItem("Materials", nullptr, &app.uiSettings.showMaterialsPanel);
                ImGui::MenuItem("Bone Display", nullptr, &app.uiSettings.showBoneDisplayPanel);
                ImGui::MenuItem("Gizmo", nullptr, &app.uiSettings.showGizmoPanel);
                ImGui::MenuItem("Viewport Settings", nullptr,
                                &app.uiSettings.showViewportSettingsPanel);
                ImGui::MenuItem("Export", nullptr, &app.uiSettings.showExportPanel);
                ImGui::MenuItem("Project", nullptr, &app.uiSettings.showProjectPanel);
                ImGui::MenuItem("Settings", nullptr, &app.uiSettings.showSettingsPanel);
                ImGui::MenuItem("Validation", nullptr, &app.uiSettings.showValidationPanel);
                ImGui::MenuItem("Console", nullptr, &app.uiSettings.showConsolePanel);
                ImGui::MenuItem("Timeline", nullptr, &app.uiSettings.showTimelinePanel);
                ImGui::MenuItem("Tools", nullptr, &app.uiSettings.showSystemPanel);
                ImGui::MenuItem("MSM Inspector tab", nullptr,
                                &app.uiSettings.showMSMInspectorPanel);
                ImGui::MenuItem("MSE Effects tab", nullptr, &g_mseTabEnabled);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) {
                const ImGuiID resetId = ImGui::GetID("M2RigDockSpace");
                std::error_code ec;
                std::filesystem::remove(ImGui::GetIO().IniFilename ? ImGui::GetIO().IniFilename : "", ec);
                // Live rebuild via the file-scope flag (this menu runs before
                // the DockSpace below, so the rebuild lands in the same frame).
                requestDockRebuild();
                buildDefaultDockLayout(resetId);
                markDockBuilt();
                app.setStatus("Layout reset — rebuilt live", "success");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Shortcuts...")) ImGui::OpenPopup("Shortcuts");
            if (ImGui::MenuItem("About...")) ImGui::OpenPopup("About");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    // Global shortcuts must not steal keys from active ImGui controls.
    const ImGuiIO& shortcutIo = ImGui::GetIO();
    const bool appOwnsKeyboard = !shortcutIo.WantTextInput && !shortcutIo.WantCaptureKeyboard;
    if (appOwnsKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            frameWholeModel(app, false);
        }
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) app.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) app.redo();
        // Command palette: explicit user intent under the same appOwnsKeyboard
        // arbiter as every other global (never steals keys from text inputs).
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_K)) ImGui::OpenPopup("cmd_palette");
        if (!ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_G)) app.showGrid = !app.showGrid;
            if (ImGui::IsKeyPressed(ImGuiKey_B)) app.showBones = !app.showBones;
            if (ImGui::IsKeyPressed(ImGuiKey_X)) app.xrayBones = !app.xrayBones;
            if (ImGui::IsKeyPressed(ImGuiKey_W)) app.showWireOverlay = !app.showWireOverlay;
            if (ImGui::IsKeyPressed(ImGuiKey_P)) app.paintMode = !app.paintMode;
            if (ImGui::IsKeyPressed(ImGuiKey_T)) {
                app.textured = !app.textured;
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D)) {
                app.previewDeform = !app.previewDeform;
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            for (int i = 0; i < 7; ++i) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i))) {
                    app.viewMode = static_cast<ViewMode>(i);
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
            }
        }
    }
    drawCommandPalette(app);
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Metin2 Rigging Studio v%s (native C++20, D3D11 + ImGui)", appVersion());
        ImGui::Separator();
        Gr2BridgeConfig bcfg = defaultGr2BridgeConfig();
        ImGui::Text("Noesis bridge: %s",
                    std::filesystem::exists(bcfg.noesisCliPath) ? bcfg.noesisCliPath.string().c_str()
                                                                : "not found");
        ImGui::Text("grnreader98: %s",
                    findGrnReader().empty() ? "not found" : findGrnReader().string().c_str());
        ImGui::TextDisabled("GR2 native emit: NOT supported directly (bridge only).");
        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("shortcuts", 2)) {
            const char* rows[][2] = {
                {"F", "Frame all"},         {"Space", "Play / pause timeline"},
                {"Left / Right", "Step frame"}, {"Ctrl+Z / Ctrl+Y", "Undo / redo"},
                {"Ctrl/Shift+drag", "Paint weights (needs bone)"}, {"Drag", "Orbit camera"},
                {"Right/Middle drag", "Pan"}, {"Wheel", "Zoom"},
                {"Click (no Ctrl)", "Select bone"},   {"G / B / X / W / P / D", "View toggles"},
                {"1-7", "View modes"},      {"T", "Textured"},
                {"Shift+drag", "Box-select bones"}, {"Ctrl+click", "Additive bone select"},
                {"Paint needs", "Paint mode + selected bone + Ctrl/Shift+drag"}};
            for (const auto& row : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", row[0]);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row[1]);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const ImGuiID dockspaceId = ImGui::GetID("M2RigDockSpace");
    constexpr ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    const char* imguiIni = ImGui::GetIO().IniFilename;
    const bool hasSavedLayout = imguiIni != nullptr && std::filesystem::exists(imguiIni);
    // File-scope g_dockBuilt (not function-static) so both Reset paths can
    // force this same rebuild live. First run with no imgui.ini builds once.
    if (!g_dockBuilt && !hasSavedLayout) {
        buildDefaultDockLayout(dockspaceId);
        markDockBuilt();
    }
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), dockspaceFlags);

    if (ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoCollapse)) drawToolbar(app);
    ImGui::End();

    // Left column - Assets, Scene, Skeleton as separate tabbed windows
    if (ImGui::Begin("Assets")) drawAssetsPanel(app);
    ImGui::End();

    if (ImGui::Begin("Scene")) drawScenePanel(app);
    ImGui::End();

    if (ImGui::Begin("Skeleton")) drawSkeletonPanel(app);
    ImGui::End();

    outViewport = drawViewportPanel(app, renderer);

    // Right Properties column
    if (app.uiSettings.showBonePanel) {
        if (ImGui::Begin("Bone")) drawBoneProperties(app);
        ImGui::End();
    }
    if (app.uiSettings.showWeightsPanel) {
        if (ImGui::Begin("Weights")) drawWeightPanel(app, renderer);
        ImGui::End();
    }
    if (app.uiSettings.showMaterialsPanel) {
        if (ImGui::Begin("Materials")) drawMaterialPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showBoneDisplayPanel) {
        if (ImGui::Begin("Bone Display")) drawBoneDisplayPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showGizmoPanel) {
        if (ImGui::Begin("Gizmo")) drawGizmoPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showViewportSettingsPanel) {
        if (ImGui::Begin("Viewport Settings")) drawViewportSettingsPanel(app);
        ImGui::End();
    }

    // Right Workflow column
    if (app.uiSettings.showExportPanel) {
        if (ImGui::Begin("Export")) drawExportPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showProjectPanel) {
        if (ImGui::Begin("Project")) drawProjectPanel(app, projectsDir);
        ImGui::End();
    }
    if (app.uiSettings.showSettingsPanel) {
        if (ImGui::Begin("Settings")) drawSettingsPanel(app);
        ImGui::End();
    }

    // Bottom Output
    if (app.uiSettings.showValidationPanel) {
        if (ImGui::Begin("Validation")) drawValidationPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showConsolePanel) {
        if (ImGui::Begin("Console")) drawConsolePanel();
        ImGui::End();
    }

    // Bottom Tools
    if (app.uiSettings.showTimelinePanel) {
        if (ImGui::Begin("Timeline")) drawTimelinePanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showSystemPanel) {
        if (ImGui::Begin("Tools")) {
            if (ImGui::BeginTabBar("ToolsTabs")) {
                if (ImGui::BeginTabItem("System")) {
                    drawSystemPanel(app);
                    ImGui::EndTabItem();
                }
                if (app.uiSettings.showMSMInspectorPanel && ImGui::BeginTabItem("MSM Inspector")) {
                    drawMsmInspector(app);
                    ImGui::EndTabItem();
                }
                if (g_mseTabEnabled && ImGui::BeginTabItem("MSE Effects")) {
                    drawMseEffects(app);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }
    }

    // Status bar and toasts (rendered after all panels, on top)
    drawStatusBar(app, renderer, outViewport);
    drawToasts(app);
}

}  // namespace m2rig
