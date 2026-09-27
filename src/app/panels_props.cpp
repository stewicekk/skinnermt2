// Properties column: Bone panel + weight table / weight paint panel.
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"

namespace m2rig {
void drawBoneProperties(App& app) {
    ImGui::Text("Bone Properties");
    ImGui::Separator();
    ImGui::Checkbox(labelXray(), &app.xrayBones);
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
    if (boneLocked) {
        ImGui::SameLine();
        ImGui::TextDisabled("[locked]");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bone is locked — unlock to edit transform.");
    }
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
    dangerButtonPush();
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
    dangerButtonPop();
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
    // Falloff curve preview (read-only PlotLines of the selected curve).
    {
        float samples[33];
        for (int i = 0; i < 33; ++i) samples[i] = evalFalloff(app.paintFalloff, i / 32.0f);
        ImGui::PlotLines("##falloff", samples, 33, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
    }
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
        if (ImGui::Button(labelValidate())) app.runValidation();
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
            const ImU32* const ramp = theme::heatmapU32();  // single source (ui_model token)
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
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Cannot remove the last influence.");
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

}  // namespace m2rig
