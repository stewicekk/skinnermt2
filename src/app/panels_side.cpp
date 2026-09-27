// Left navigator: Assets / Scene / Skeleton panels.
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"

namespace m2rig {
void drawAssetsPanel(App& app) {
    {
        ImGui::BeginChild("##assetlist", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f),
                          true);
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
        ImGui::EndChild();
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
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
                    assetNeedW(label) <=
                assetSectionW)
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
        assetFlow("Load GR2 from Data/Models...");
        if (ImGui::Button("Load GR2 from Data/Models...")) doImportGr2FromModels(app);
        tipFor("Import a Metin2 GR2 model from the Data/Models folder via bridge");
        assetFlow("Import glTF...");
        if (ImGui::Button("Import glTF...")) doImportGltf(app);
        tipFor("Native glTF import (.glb/.gltf, CLI-chain parity, fail-closed)");
        ImGui::EndDisabled();
        assetFlow("Import mesh...");
        if (ImGui::Button("Import mesh...")) doImportMeshOntoSkeleton(app);
        tipFor("Replace geometry onto the loaded skeleton (keeps rig, profiles, locks); "
               ".smd/.fbx/.gltf/.obj");
        if (app.bridgeBusy)
            ImGui::TextDisabled("Running %s... %.0fs", app.bridgeJob.label.c_str(),
                                ImGui::GetTime() - app.bridgeJob.startTime);
        assetFlow(labelValidate());
        if (ImGui::Button(labelValidate())) app.runValidation();
        tipFor("Run the full compatibility check (<=4 influences, skeleton, sockets, topology)");
    }
}

void drawScenePanel(App& app) {
    {
        if (const LoadedAsset* a = app.currentAsset()) {
            ImGui::Text("%s", a->mesh.name.c_str());
            ImGui::TextDisabled("%zu verts / %zu tris", a->mesh.vertices.size(),
                                a->mesh.triangleCount());
            ImGui::BeginChild("##submeshlist", ImVec2(0, 0), true);
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
                ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x +
                                       ImGui::GetCursorPosX());
                ImGui::Text("submesh %zu: %s (%zu tris)%s", i, mat, sm.indexCount / 3,
                            vis ? "" : " [hidden]");
                ImGui::PopTextWrapPos();
                ImGui::PopID();
            }
            ImGui::EndChild();
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
            // BUG 6c: scrollable bone list so action buttons stay visible
            const float boneListH = ImGui::GetContentRegionAvail().y * 0.4f;
            ImGui::BeginChild("##bonelist", ImVec2(0, boneListH), true);
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
            ImGui::EndChild();
            ImGui::Separator();
            ImGui::TextDisabled("Selected: %zu%s", app.selectedBones.size(),
                                app.soloBone >= 0 ? " | SOLO" : "");
            // BUG 2: flow helper wraps buttons instead of overflowing
            const ImGuiStyle& skelStyle = ImGui::GetStyle();
            const float skelSectionW =
                ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
            auto skelNeedW = [&](const char* label) -> float {
                return ImGui::CalcTextSize(label).x + skelStyle.FramePadding.x * 2.0f +
                       skelStyle.ItemSpacing.x;
            };
            bool skelFirst = true;
            auto skelFlow = [&](const char* label) {
                if (skelFirst) {
                    skelFirst = false;
                    return;
                }
                if (ImGui::GetItemRectMax().x + skelStyle.ItemSpacing.x +
                        skelNeedW(label) <=
                    skelSectionW)
                    ImGui::SameLine();
            };
            skelFlow("Hierarchy");
            if (ImGui::Button("Hierarchy")) {
                if (app.selectedBone >= 0)
                    app.selectBoneHierarchy(static_cast<std::uint32_t>(app.selectedBone),
                                            ImGui::GetIO().KeyCtrl != 0);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Select bone + all descendants (Ctrl adds)");
            skelFlow("Clear sel");
            if (ImGui::Button("Clear sel")) app.clearBoneSelection();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear bone multi-select");
            const char* soloLabel = app.soloBone >= 0 ? "Un-solo" : "Solo";
            skelFlow(soloLabel);
            if (ImGui::Button(soloLabel)) {
                if (app.soloBone >= 0) {
                    app.soloBone = -1;
                } else if (app.selectedBone >= 0) {
                    app.soloBone = app.selectedBone;
                }
                if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Isolate selected bone");
            skelFlow("Unhide all");
            if (ImGui::Button("Unhide all")) app.clearHiddenBones();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear per-bone hide + solo");
            // BUG 3: explicit width leaves room for "Save set" button
            static char setName[64] = "";
            ImGui::PushItemWidth(skelSectionW - 70);
            ImGui::InputTextWithHint("##setname", "Set name...", setName, sizeof(setName));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Save set")) {
                if (setName[0] != '\0') {
                    app.saveBoneSelectionSet(setName);
                    app.setStatus(std::string("Saved selection set '") + setName + "'.",
                                  "success");
                    setName[0] = '\0';
                }
            }
            // BUG 4: explicit width leaves room for "Load" + "Del" buttons
            if (!app.boneSelectionSets.empty()) {
                static int setIdx = 0;
                std::vector<const char*> names;
                for (const auto& kv : app.boneSelectionSets) names.push_back(kv.first.c_str());
                if (setIdx >= static_cast<int>(names.size())) setIdx = 0;
                ImGui::PushItemWidth(skelSectionW - 100);
                ImGui::Combo("Sets", &setIdx, names.data(), static_cast<int>(names.size()));
                ImGui::PopItemWidth();
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


}  // namespace m2rig
