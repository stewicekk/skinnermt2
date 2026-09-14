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
#include <filesystem>
#include <functional>
#include <limits>

#include "m2rig/app.hpp"
#include "m2rig/file_dialog.hpp"
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

void setMainWindowHandle(void* hwnd) { g_mainWindow = hwnd; }

namespace {

// --- helpers -------------------------------------------------------------

void statusBar(const App& app) {
    ImVec4 color{0.6f, 0.65f, 0.7f, 1.0f};
    if (app.statusKind == "success") color = {0.35f, 0.9f, 0.5f, 1.0f};
    if (app.statusKind == "warning") color = {0.95f, 0.75f, 0.3f, 1.0f};
    if (app.statusKind == "error") color = {1.0f, 0.4f, 0.35f, 1.0f};
    ImGui::TextColored(color, "%s", app.statusMessage.empty() ? "Ready." : app.statusMessage.c_str());
}

std::vector<GpuVertex> boneSegments(const Skeleton& skel, int selectedBone, int hoveredBone = -1) {
    std::vector<GpuVertex> segs;
    auto colorFor = [&](const Bone& b) -> Vec3 {
        if (static_cast<int>(b.id) == selectedBone) return {1.0f, 0.85f, 0.2f};
        if (static_cast<int>(b.id) == hoveredBone) return {1.0f, 0.5f, 0.1f};
        if (b.name == "equip_left" || b.name == "equip_right" || b.name == "stip")
            return {0.3f, 0.9f, 0.9f};
        return {0.9f, 0.9f, 0.95f};
    };
    for (const auto& b : skel.bones) {
        if (b.parentId == kNoParent) continue;
        const Bone* p = skel.findById(static_cast<std::uint32_t>(b.parentId));
        if (!p) continue;
        const Vec3 a{p->globalTransform.m[3][0], p->globalTransform.m[3][1],
                     p->globalTransform.m[3][2]};
        const Vec3 c{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                     b.globalTransform.m[3][2]};
        const Vec3 col = colorFor(b);
        GpuVertex v0, v1;
        v0.position = a;
        v1.position = c;
        v0.normal = v1.normal = {0, 1, 0};
        v0.color[0] = v1.color[0] = col.x;
        v0.color[1] = v1.color[1] = col.y;
        v0.color[2] = v1.color[2] = col.z;
        v0.color[3] = v1.color[3] = 1.0f;
        segs.push_back(v0);
        segs.push_back(v1);
        // Joint cross (X/Y/Z ticks) so joints read at any angle.
        const float t = 0.03f;
        const Vec3 axes[3] = {{t, 0, 0}, {0, t, 0}, {0, 0, t}};
        for (const Vec3& ax : axes) {
            GpuVertex j0 = v0, j1 = v0;
            j0.position = c + ax * -1.0f;
            j1.position = c + ax;
            segs.push_back(j0);
            segs.push_back(j1);
        }
    }
    return segs;
}

void drawSkeletonTree(App& app, std::int32_t boneId) {    LoadedAsset* asset = app.currentAsset();
    if (!asset) return;
    const Bone* b = asset->skeleton.findById(static_cast<std::uint32_t>(boneId));
    if (!b) return;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (b->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (app.selectedBone == boneId) flags |= ImGuiTreeNodeFlags_Selected;
    char label[256];
    std::snprintf(label, sizeof(label), "%s%s  [%zu]", app.isBoneLocked(b->id) ? "[L] " : "",
                  b->name.c_str(), app.boneInfluenceCount(b->id));
    const bool open = ImGui::TreeNodeEx(label, flags);
    if (ImGui::IsItemClicked()) app.selectedBone = boneId;
    if (open) {
        for (std::uint32_t child : b->children) drawSkeletonTree(app, static_cast<std::int32_t>(child));
        ImGui::TreePop();
    }
}

// --- panels --------------------------------------------------------------

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
    const DialogResult dlg = openFileDialog(
        g_mainWindow, "Import model (SMD/FBX/GR2 via bridge)",
        "Supported (*.smd;*.fbx;*.gr2)|*.smd;*.fbx;*.gr2|SMD (*.smd)|*.smd|FBX (*.fbx)|*.fbx|GR2 "
        "(*.gr2)|*.gr2|All (*.*)|*.*",
        "");
    if (!dlg.confirmed) return;
    if (auto r = app.importBridgedFile(dlg.path); !r)
        app.setStatus("Import failed: " + r.error().message, "error");
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
        const Vec3& v0 = verts[a->mesh.indices[i]].position;
        const Vec3& v1 = verts[a->mesh.indices[i + 1]].position;
        const Vec3& v2 = verts[a->mesh.indices[i + 2]].position;
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
        if (b.parentId == kNoParent) continue;
        const Bone* p = a->skeleton.findById(static_cast<std::uint32_t>(b.parentId));
        if (!p) continue;
        const Vec3 segA{p->globalTransform.m[3][0], p->globalTransform.m[3][1],
                        p->globalTransform.m[3][2]};
        const Vec3 segB{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                        b.globalTransform.m[3][2]};
        const Vec3 e = segB - segA;
        const Vec3 w0 = pNear - segA;
        const float bb = dot(dir, e);
        const float ee = dot(e, e);
        const float c1 = dot(dir, w0);
        const float c2 = dot(e, w0);
        const float denom = ee - bb * bb;  // |dir| == 1
        float s = 0.0f, t = 0.0f;
        if (denom > 1e-12f && ee > 1e-12f) {
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
        const float d = distance(closest, onBone);
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(b.id);
        }
    }
    return best;
}

void drawToolbar(App& app) {
    const char* modes[] = {"Solid",     "Wireframe", "Solid + Wire", "Normals",
                           "Height",    "Weights",   "UV"};
    int mode = static_cast<int>(app.viewMode);
    ImGui::SetNextItemWidth(140);
    if (ImGui::Combo("View", &mode, modes, 7)) {
        app.viewMode = static_cast<ViewMode>(mode);
        if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &app.showGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Bones", &app.showBones);
    ImGui::SameLine();
    ImGui::Checkbox("X-ray", &app.xrayBones);
    ImGui::SameLine();
    ImGui::Checkbox("Wire ovl", &app.showWireOverlay);
    ImGui::SameLine();
    ImGui::Checkbox("Paint", &app.paintMode);
    ImGui::SameLine();
    ImGui::TextDisabled("FPS %.0f", app.fps);
    ImGui::SameLine();
    statusBar(app);
}

void drawLeft(App& app) {
    if (ImGui::CollapsingHeader("Asset Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& kv : app.assets) {
            const bool sel = app.current == kv.first;
            if (ImGui::Selectable(kv.first.c_str(), sel)) {
                app.current = kv.first;
                app.selectedBone = -1;
                app.lockedBones.clear();  // locks are per-asset, resolved by id
                app.hiddenSubmeshes.clear();
                if (const LoadedAsset* a = app.currentAsset()) app.camera.frameAabb(a->mesh.bounds);
            }
        }
        if (ImGui::Button("Load sample armor")) {
            if (auto r = app.loadSampleArmor(); !r)
                app.setStatus("Sample load failed: " + r.error().message, "error");
        }
        ImGui::SameLine();
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
        ImGui::SameLine();
        if (ImGui::Button("Import SMD...")) doImportSmd(app);
        if (ImGui::Button("Import FBX/GR2...")) doImportBridged(app);
        ImGui::SameLine();
        if (ImGui::Button("Validate")) app.runValidation();
    }
    if (ImGui::CollapsingHeader("Scene Hierarchy", ImGuiTreeNodeFlags_DefaultOpen)) {
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
    if (ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen)) {
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
                    const bool sel = app.selectedBone == static_cast<int>(b.id);
                    if (ImGui::Selectable(label, sel)) {
                        app.selectedBone = static_cast<int>(b.id);
                        a->gpuDirty = true;
                    }
                }
            } else if (a->skeleton.rootBone != kNoParent)
                drawSkeletonTree(app, a->skeleton.rootBone);
            else
                ImGui::TextDisabled("No skeleton.");
        } else {
            ImGui::TextDisabled("No asset loaded.");
        }
    }
}

void drawBoneProperties(App& app) {
    ImGui::Text("Bone Properties");
    ImGui::Separator();
    LoadedAsset* a = app.currentAsset();
    if (!a || app.selectedBone < 0) {
        ImGui::TextDisabled("Select a bone in the Skeleton tree.");
        return;
    }
    const Bone* b = a->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone));
    if (!b) {
        ImGui::TextDisabled("Selected bone no longer exists.");
        return;
    }
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
    ImGui::Text("Local pos: %.3f %.3f %.3f", b->localPosition.x, b->localPosition.y,
                b->localPosition.z);
    ImGui::Text("Local rot (rad): %.3f %.3f %.3f", b->localRotationEuler.x,
                b->localRotationEuler.y, b->localRotationEuler.z);
    ImGui::Text("Influenced verts: %zu", app.boneInfluenceCount(b->id));
    if (const SkeletonProfile* p = findProfile(a->profileId); p) {
        ImGui::Text("Mirror: %s", mirrorBoneName(*p, b->name).c_str());
    }
    bool locked = app.isBoneLocked(b->id);
    if (ImGui::Checkbox("Locked (skip paint/mirror/transfer/gizmo)", &locked))
        app.setBoneLocked(b->id, locked);
    if (ImGui::Button("Lock sockets")) app.lockSocketBones();
    ImGui::SameLine();
    if (ImGui::Button("Unlock all")) app.unlockAllBones();
    ImGui::Separator();
    ImGui::Text("Gizmo");
    int gop = app.gizmoOp == GizmoOp::Translate ? 0 : 1;
    if (ImGui::RadioButton("Translate", &gop, 0)) app.gizmoOp = GizmoOp::Translate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", &gop, 1)) app.gizmoOp = GizmoOp::Rotate;
#ifdef M2RIG_WITH_GIZMO
    ImGui::TextDisabled("Drag the gizmo in the viewport (undoable).");
#else
    ImGui::TextDisabled("Gizmo disabled at build time (M2RIG_WITH_GIZMO=OFF).");
#endif
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
        const WeightQualityMetrics q = computeWeightQuality(a->mesh);
        ImGui::Text("Normalized: %.2f%%", q.normalizedPct);
        ImGui::Text("Unweighted: %.2f%%", q.unweightedPct);
        ImGui::Text("Invalid: %.0f", q.invalidCount);
        ImGui::Text("Max influences: %zu", q.maxInfluenceCount);
        ImGui::Text("Avg influences: %.2f", q.avgInfluenceCount);
        if (ImGui::Button("Normalize all weights")) {
            RepairStats stats = repairMeshWeights(a->mesh, a->skeleton.bones.size());
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
        ImGui::Separator();
        ImGui::BeginDisabled(!app.canUndo());
        if (ImGui::Button("Undo")) app.undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!app.canRedo());
        if (ImGui::Button("Redo")) app.redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Mirror weights")) {
            if (auto r = app.mirrorWeights(); !r)
                app.setStatus("Mirror failed: " + r.error().message, "error");
        }
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
        if (ImGui::Button("Transfer from...")) ImGui::OpenPopup("transfer_src");
        if (ImGui::BeginPopup("transfer_src")) {
            if (app.assets.size() < 2) ImGui::TextDisabled("Load a second asset first.");
            for (const auto& kv : app.assets) {
                if (kv.first == app.current) continue;
                if (ImGui::Selectable(("Quick: " + kv.first).c_str())) {
                    if (auto r = app.transferWeightsFrom(kv.first); !r)
                        app.setStatus("Transfer failed: " + r.error().message, "error");
                }
                if (ImGui::Selectable(("Self-train: " + kv.first).c_str())) {
                    if (auto r = app.transferWeightsSelfTrained(kv.first); !r)
                        app.setStatus("Self-train failed: " + r.error().message, "error");
                }
            }
            ImGui::EndPopup();
        }
        if (app.selectedBone < 0)
            ImGui::TextDisabled("Select a bone, enable Paint, then drag in the viewport.");
        else if (!app.paintMode)
            ImGui::TextDisabled("Enable Paint in the toolbar to paint. Weights view shows heatmap.");
        (void)renderer;
    } else {
        ImGui::TextDisabled("No asset loaded.");
    }
    ImGui::TextDisabled("Paint: Ctrl+drag paints, plain drag orbits. Weights view = heatmap.");
}

void drawMaterialPanel(App& app) {
    ImGui::Text("Materials / Textures");
    ImGui::Separator();
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        ImGui::TextDisabled("No asset loaded.");
        return;
    }
    for (std::size_t i = 0; i < a->mesh.materials.size(); ++i) {
        auto& m = a->mesh.materials[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("Material %zu: %s", i, m.name.c_str());
        char path[260];
        std::snprintf(path, sizeof(path), "%s", m.texturePath.c_str());
        if (ImGui::InputText("Texture", path, sizeof(path))) {
            m.texturePath = path;
            a->dirty = true;
        }
        if (m.texturePath.empty()) {
            ImGui::TextColored({1, 0.6f, 0.3f, 1}, "Missing texture path.");
        } else {
            // Real existence probe: literal path, exe-dir Data/Models, basename.
            const std::string base =
                std::filesystem::path(m.texturePath).filename().string();
            const std::filesystem::path exeDir = std::filesystem::current_path();
            const bool found =
                std::filesystem::exists(m.texturePath) ||
                std::filesystem::exists(exeDir / "Data" / "Models" / base) ||
                std::filesystem::exists(exeDir / base);
            if (found)
                ImGui::TextColored({0.35f, 0.9f, 0.5f, 1}, "Texture found.");
            else
                ImGui::TextColored({1, 0.6f, 0.3f, 1}, "Texture not found (checked .dds next to model).");
        }
        ImGui::PopID();
    }
}

void drawProjectPanel(App& app) {
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
    ImGui::TextDisabled("Snapshots / autosave (.m2rig): wave 8.");
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
            ImGui::TextColored({0.35f, 0.9f, 0.5f, 1}, "Checks passed");
    }
    if (ImGui::Button("Export SMD...")) doExportSmd(app);
    ImGui::SameLine();
    if (ImGui::Button("Export MSM...")) doExportMsm(app);
    if (ImGui::Button("Export GR2 (bridge)...")) doExportGr2(app);
    ImGui::SameLine();
    if (ImGui::Button("Save workspace...")) doSaveWorkspace(app);
    ImGui::SameLine();
    if (ImGui::Button("Load workspace...")) doLoadWorkspace(app);
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
            ImVec4 c = pass ? ImVec4(0.35f, 0.9f, 0.5f, 1) : ImVec4(1.0f, 0.4f, 0.35f, 1);
            if (item.severity == Severity::Warning) c = ImVec4(0.95f, 0.75f, 0.3f, 1);
            ImGui::TextColored(c, "%s %s", pass ? "[OK]" : "[FAIL]", item.id.c_str());
            ++shown;
        }
    }
    ImGui::BeginDisabled(app.report.exportBlocked());
    ImGui::TextDisabled("SMD/MSM export above respect this gate; GR2 bridge reports honestly.");
    ImGui::EndDisabled();
    ImGui::Separator();
    static std::vector<App::BatchRow> lastBatch;
    if (ImGui::Button("Export all loaded (SMD+MSM)...")) {
        if (auto r = app.exportAllBatch(); r)
            lastBatch = r.value();
        else {
            lastBatch.clear();
            app.setStatus("Batch failed: " + r.error().message, "error");
        }
    }
    if (!lastBatch.empty()) {
        if (ImGui::BeginTable("batch_table", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
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
                ImGui::TextColored(row.smdOk ? ImVec4(0.35f, 0.9f, 0.5f, 1)
                                             : ImVec4(1, 0.4f, 0.35f, 1),
                                    "%s", row.smdOk ? "OK" : "FAIL");
                ImGui::TableNextColumn();
                ImGui::TextColored(row.msmOk ? ImVec4(0.35f, 0.9f, 0.5f, 1)
                                             : ImVec4(1, 0.4f, 0.35f, 1),
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
            if (item.severity == Severity::Warning) c = {0.95f, 0.75f, 0.3f, 1};
            if (item.severity == Severity::Error || item.severity == Severity::Fatal)
                c = {1.0f, 0.4f, 0.35f, 1};
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
        ImGui::TextColored(ok ? ImVec4(0.35f, 0.9f, 0.5f, 1) : ImVec4(1, 0.4f, 0.35f, 1), "%s",
                            ok ? "[OK]" : "[--]");
        ImGui::SameLine();
        ImGui::Text("%s: %s", name, ok ? p.string().c_str() : "not found");
    };
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    toolRow("Noesis bridge", cfg.noesisCliPath);
    toolRow("grnreader98", findGrnReader());
    const std::filesystem::path models = std::filesystem::current_path() / "Data" / "Models";
    if (std::filesystem::exists(models)) {
        std::size_t gr2 = 0, fbx = 0, dds = 0;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(models, ec)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext == ".gr2") ++gr2;
            if (ext == ".fbx") ++fbx;
            if (ext == ".dds") ++dds;
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
            if (e.level == LogLevel::Warning) c = {0.95f, 0.75f, 0.3f, 1};
            if (e.level == LogLevel::Error || e.level == LogLevel::Fatal) c = {1, 0.4f, 0.35f, 1};
            ImGui::TextColored(c, "[%s] %s", e.timestamp.c_str(), e.message.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void drawTimelinePanel(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a || a->animFrames.size() <= 1) {
        ImGui::Text("Timeline");
        ImGui::SameLine();
        ImGui::TextDisabled(
            "No animation loaded. Import an SMD with multiple skeleton frames to preview poses.");
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
        const std::size_t prev =
            a->currentFrame == 0 ? a->animFrames.size() - 1 : a->currentFrame - 1;
        if (auto r = app.setCurrentFrame(prev); !r)
            app.setStatus("Frame preview failed: " + r.error().message, "error");
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        const std::size_t next =
            (a->currentFrame + 1) % (a->animFrames.empty() ? 1 : a->animFrames.size());
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
    static double lastT = 0.0;
    if (app.timelinePlaying) {
        const double now = ImGui::GetTime();
        if (lastT <= 0.0) lastT = now;
        const double acc = now - lastT;
        const double step = app.timelineFps > 0.0f ? 1.0 / app.timelineFps : 1.0 / 24.0;
        if (acc >= step) {
            lastT = now;
            std::size_t next = a->currentFrame + 1;
            if (next >= a->animFrames.size()) next = app.timelineLoop ? 0 : a->animFrames.size() - 1;
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
    if (ImGui::SliderInt("##frame", &frame, 0, static_cast<int>(a->animFrames.size()) - 1)) {
        if (auto r = app.setCurrentFrame(static_cast<std::size_t>(frame)); !r)
            app.setStatus("Frame preview failed: " + r.error().message, "error");
    }
    // Keyboard transport (only when not typing).
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) app.timelinePlaying = !app.timelinePlaying;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            const std::size_t prev =
                a->currentFrame == 0 ? a->animFrames.size() - 1 : a->currentFrame - 1;
            if (auto r = app.setCurrentFrame(prev); !r)
                app.setStatus("Frame preview failed: " + r.error().message, "error");
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            const std::size_t next =
                (a->currentFrame + 1) % (a->animFrames.empty() ? 1 : a->animFrames.size());
            if (auto r = app.setCurrentFrame(next); !r)
                app.setStatus("Frame preview failed: " + r.error().message, "error");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("t=%d / %zu frames%s", a->animFrames[a->currentFrame].time,
                        a->animFrames.size(),
                        app.previewDeform ? " (deformed preview)" : " (skeleton pose)");
}

}  // namespace

// Viewport: called with the renderer each frame; returns the scene rect in
// framebuffer pixels for the D3D pass. Camera input handled here.
ViewportRect drawViewportPanel(App& app) {
    ViewportRect rect;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("viewport_input", avail,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImGuiIO& io = ImGui::GetIO();
            bool gizmoUsing = false;
            bool gizmoOver = false;
#ifdef M2RIG_WITH_GIZMO
            // Bone manipulator (translate/rotate the selected bone in world space).
            if (LoadedAsset* ga = app.currentAsset()) {
                Bone* mb = app.selectedBone >= 0
                               ? ga->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone))
                               : nullptr;
                if (mb) {
                    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                    ImGuizmo::BeginFrame();
                    ImGuizmo::SetRect(cursor.x, cursor.y, avail.x, avail.y);
                    ImGuizmo::SetOrthographic(app.camera.orthographic);
                    const Mat4 vv = app.camera.viewMatrix();
                    const Mat4 pp =
                        app.camera.projMatrix(avail.x / (avail.y > 0.0f ? avail.y : 1.0f));
                    float view[16], proj[16], mtx[16];
                    for (int r = 0; r < 4; ++r) {
                        for (int c = 0; c < 4; ++c) {
                            view[c * 4 + r] = vv.m[r][c];
                            proj[c * 4 + r] = pp.m[r][c];
                            mtx[c * 4 + r] = mb->globalTransform.m[r][c];
                        }
                    }
                    ImGuizmo::Manipulate(view, proj,
                                         app.gizmoOp == GizmoOp::Translate ? ImGuizmo::TRANSLATE
                                                                           : ImGuizmo::ROTATE,
                                         ImGuizmo::WORLD, mtx);
                    gizmoUsing = ImGuizmo::IsUsing();
                    gizmoOver = ImGuizmo::IsOver();
                    static bool wasUsing = false;
                    if (gizmoUsing && !wasUsing) app.pushUndoSnapshot("gizmo " + mb->name);
                    if (gizmoUsing) {
                        if (app.isBoneLocked(mb->id)) {
                            app.setStatus("Gizmo blocked: bone is locked.", "warning");
                        } else {
                        Mat4 nW;
                        for (int r = 0; r < 4; ++r)
                            for (int c = 0; c < 4; ++c) nW.m[r][c] = mtx[c * 4 + r];
                        Mat4 pG = Mat4::identity();
                        if (mb->parentId != kNoParent) {
                            if (const Bone* pb = ga->skeleton.findById(
                                    static_cast<std::uint32_t>(mb->parentId)))
                                pG = pb->globalTransform;
                        }
                        // Row-vector: world = parent * local.
                        const Mat4 nL = pG.inverseRigid() * nW;
                        mb->localPosition = {nL.m[3][0], nL.m[3][1], nL.m[3][2]};
                        if (app.gizmoOp == GizmoOp::Rotate)
                            mb->localRotationEuler = nL.eulerXyzFromRotation();
                        if (auto rr = rebuildSkeletonRuntime(ga->skeleton); !rr)
                            app.setStatus("Gizmo update failed: " + rr.error().message, "error");
                        ga->gpuDirty = true;
                        ga->dirty = true;
                        }
                    }
                    if (!gizmoUsing && wasUsing) {
                        app.runValidation();
                        app.setStatus("Gizmo edit applied.", "success");
                    }
                    wasUsing = gizmoUsing;
                }
            }
#endif
            const bool paintDrag =
                app.paintMode && app.selectedBone >= 0 && !gizmoUsing && !gizmoOver &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left) && (io.KeyCtrl || io.KeyShift);
            const bool orbitDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !paintDrag &&
                                   !gizmoUsing && !gizmoOver && !io.KeyCtrl && !io.KeyShift;
            if (paintDrag) {
                // Paint dab at the mesh surface under the cursor.
                Vec3 hit;
                if (pickMeshPoint(app, avail.x, avail.y, io.MousePos.x - cursor.x,
                                  io.MousePos.y - cursor.y, hit))
                    app.paintStroke(hit);
            } else if (orbitDrag)
                app.camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                     ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                app.camera.pan(io.MouseDelta.x, io.MouseDelta.y);
            if (io.MouseWheel != 0.0f) app.camera.zoom(io.MouseWheel);
            if (ImGui::IsKeyPressed(ImGuiKey_F)) {
                if (const LoadedAsset* a = app.currentAsset()) app.camera.frameAabb(a->mesh.bounds);
            }
            // Click (no drag) selects a bone in the 3D view.
            static bool btnDown = false;
            static ImVec2 downAt{0, 0};
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                btnDown = true;
                downAt = io.MousePos;
            }
            if (btnDown && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                btnDown = false;
                const float movedX = io.MousePos.x - downAt.x;
                const float movedY = io.MousePos.y - downAt.y;
                if (movedX * movedX + movedY * movedY < 25.0f && !io.KeyCtrl && !io.KeyShift &&
                    !gizmoOver && !gizmoUsing) {
                    const int picked = pickBoneAt(app, avail.x, avail.y, downAt.x - cursor.x,
                                                  downAt.y - cursor.y);
                    if (picked >= 0) {
                        app.selectedBone = picked;
                        if (LoadedAsset* pa = app.currentAsset()) pa->gpuDirty = true;
                    }
                }
            }
            // Hover highlight (no drag in progress): cheap bone pick + tooltip.
            const bool anyDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
            if (!anyDrag && !btnDown && !gizmoOver && !gizmoUsing) {
                app.hoveredBone = pickBoneAt(app, avail.x, avail.y, io.MousePos.x - cursor.x,
                                             io.MousePos.y - cursor.y);
                if (app.hoveredBone >= 0) {
                    if (const LoadedAsset* ha = app.currentAsset()) {
                        if (const Bone* hb = ha->skeleton.findById(
                                static_cast<std::uint32_t>(app.hoveredBone)))
                            ImGui::SetTooltip("%s", hb->name.c_str());
                    }
                }
            } else {
                app.hoveredBone = -1;
            }
        }
        // Overlay controls.
        ImGui::SetCursorScreenPos(cursor + ImVec2(8, 8));
        if (ImGui::Button("Frame (F)")) {
            if (const LoadedAsset* a = app.currentAsset()) app.camera.frameAabb(a->mesh.bounds);
        }
        ImGui::SameLine();
        const char* proj = app.camera.orthographic ? "Ortho" : "Persp";
        if (ImGui::Button(proj)) app.camera.orthographic = !app.camera.orthographic;
        ImGui::SameLine();
        if (ImGui::Button("Front")) app.camera.preset({0, 0.1f, 1});
        ImGui::SameLine();
        if (ImGui::Button("Top")) app.camera.preset({0, 1, 0.001f});
        ImGui::SameLine();
        if (ImGui::Button("Left")) app.camera.preset({-1, 0.1f, 0});

        const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
        rect.x = static_cast<int>(cursor.x * scale);
        rect.y = static_cast<int>(cursor.y * scale);
        rect.w = static_cast<int>(avail.x * scale);
        rect.h = static_cast<int>(avail.y * scale);
        rect.valid = rect.w > 8 && rect.h > 8;
        if (const LoadedAsset* a = app.currentAsset()) {
            ImVec2 overlay = cursor + ImVec2(8, avail.y - 24);
            ImGui::SetCursorScreenPos(overlay);
            ImGui::TextDisabled("%s | %zu v / %zu t | bone %d%s", a->id.c_str(),
                                a->mesh.vertices.size(), a->mesh.triangleCount(),
                                app.selectedBone, app.paintMode ? " | PAINT (Ctrl+drag)" : "");
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
                ImGui::GetWindowDrawList()->AddCircle(ImGui::GetIO().MousePos, r,
                                                      IM_COL32(80, 170, 255, 255), 40, 2.0f);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return rect;
}

void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport);

// Renders the 3D scene into the viewport rect (called between ImGui frame
// setup and ImGui::Render, after drawAllPanels determined the rect).
void renderScene(App& app, Renderer& renderer, const ViewportRect& rect) {
    const float clear[4] = {0.09f, 0.10f, 0.13f, 1.0f};
    if (!rect.valid) return;
    renderer.beginScenePass(rect.x, rect.y, rect.w, rect.h, clear);
    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 vp = app.camera.viewMatrix() * app.camera.projMatrix(aspect);
    if (app.showGrid) renderer.drawLines(buildGridLines(), vp);
    if (LoadedAsset* a = app.currentAsset()) {
        if (auto r = app.refreshGpu(renderer); !r)
            app.setStatus("GPU upload failed: " + r.error().message, "error");
        else {
            const FillMode fill =
                app.viewMode == ViewMode::Wireframe ? FillMode::Wireframe : FillMode::Solid;
            renderer.drawMesh(a->id, vp, fill);
            if (app.viewMode == ViewMode::SolidWireframe || app.showWireOverlay)
                renderer.drawMeshWireOverlay(a->id, vp);
            if (app.showBones) {
                auto segs = boneSegments(a->skeleton, app.selectedBone, app.hoveredBone);
                if (app.xrayBones)
                    renderer.drawLinesXRay(segs, vp);
                else
                    renderer.drawLines(segs, vp);
            }
        }
    }
    renderer.endScenePass();
}

void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Load sample armor")) {
                if (auto r = app.loadSampleArmor(); !r)
                    app.setStatus("Sample load failed: " + r.error().message, "error");
            }
            if (ImGui::MenuItem("Import SMD...")) doImportSmd(app);
            if (ImGui::MenuItem("Import FBX/GR2 (bridge)...")) doImportBridged(app);
            if (ImGui::MenuItem("Export SMD...")) doExportSmd(app);
            if (ImGui::MenuItem("Export MSM...")) doExportMsm(app);
            if (ImGui::MenuItem("Export GR2 (bridge)...")) doExportGr2(app);
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
            ImGui::MenuItem("Paint mode", "P", &app.paintMode);
            ImGui::MenuItem("Deform preview", "D", &app.previewDeform);
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
            if (ImGui::MenuItem("Frame all", "F")) {
                if (const LoadedAsset* a = app.currentAsset()) app.camera.frameAabb(a->mesh.bounds);
            }
            ImGui::MenuItem("VSync", nullptr, &app.vsync);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Shortcuts...")) ImGui::OpenPopup("Shortcuts");
            if (ImGui::MenuItem("About...")) ImGui::OpenPopup("About");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    // Global shortcuts (skipped while typing into text inputs).
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            if (const LoadedAsset* a = app.currentAsset()) app.camera.frameAabb(a->mesh.bounds);
        }
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) app.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) app.redo();
        if (!ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_G)) app.showGrid = !app.showGrid;
            if (ImGui::IsKeyPressed(ImGuiKey_B)) app.showBones = !app.showBones;
            if (ImGui::IsKeyPressed(ImGuiKey_X)) app.xrayBones = !app.xrayBones;
            if (ImGui::IsKeyPressed(ImGuiKey_W)) app.showWireOverlay = !app.showWireOverlay;
            if (ImGui::IsKeyPressed(ImGuiKey_P)) app.paintMode = !app.paintMode;
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
                {"Ctrl+drag", "Paint weights"}, {"Drag", "Orbit camera"},
                {"Right/Middle drag", "Pan"}, {"Wheel", "Zoom"},
                {"Click", "Select bone"},   {"G / B / X / W / P / D", "View toggles"},
                {"1-7", "View modes"}};
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
    static bool dockBuilt = false;
    if (!dockBuilt) {
        dockBuilt = true;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
        ImGuiID dockMain = dockspaceId;
        const ImGuiID dockLeft =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.17f, nullptr, &dockMain);
        const ImGuiID dockRight =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.21f, nullptr, &dockMain);
        const ImGuiID dockBottom =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.24f, nullptr, &dockMain);
        const ImGuiID dockTop =
            ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.075f, nullptr, &dockMain);
        ImGui::DockBuilderDockWindow("Toolbar", dockTop);
        ImGui::DockBuilderDockWindow("Asset / Scene / Skeleton", dockLeft);
        ImGui::DockBuilderDockWindow("Viewport", dockMain);
        ImGui::DockBuilderDockWindow("Bone", dockRight);
        ImGui::DockBuilderDockWindow("Weights", dockRight);
        ImGui::DockBuilderDockWindow("Material / Texture", dockRight);
        ImGui::DockBuilderDockWindow("Project / Snapshots", dockRight);
        ImGui::DockBuilderDockWindow("Export", dockRight);
        ImGui::DockBuilderDockWindow("Validation", dockBottom);
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("System", dockBottom);
        ImGui::DockBuilderDockWindow("Timeline / Animation", dockBottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

    if (ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoCollapse)) drawToolbar(app);
    ImGui::End();

    if (ImGui::Begin("Asset / Scene / Skeleton")) drawLeft(app);
    ImGui::End();

    outViewport = drawViewportPanel(app);

    if (ImGui::Begin("Bone")) drawBoneProperties(app);
    ImGui::End();

    if (ImGui::Begin("Weights")) drawWeightPanel(app, renderer);
    ImGui::End();

    if (ImGui::Begin("Material / Texture")) drawMaterialPanel(app);
    ImGui::End();

    if (ImGui::Begin("Project / Snapshots")) drawProjectPanel(app);
    ImGui::End();

    if (ImGui::Begin("Export")) drawExportPanel(app);
    ImGui::End();

    if (ImGui::Begin("Validation")) drawValidationPanel(app);
    ImGui::End();

    if (ImGui::Begin("Console")) drawConsolePanel();
    ImGui::End();

    if (ImGui::Begin("System")) drawSystemPanel(app);
    ImGui::End();

    if (ImGui::Begin("Timeline / Animation")) drawTimelinePanel(app);
    ImGui::End();
}

}  // namespace m2rig
