// Toolbar groups, view-mode segmented control, status bar, toast stack.
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"

namespace m2rig {
namespace {
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

}  // namespace
void drawToolbar(App& app) {
    // Grouped toolbar with wrap (layout-perfect slice): six groups in render
    // order (Wave 36b) — Stage (workflow-stage layout presets Rig/Paint/Anim/
    // Review, active row highlighted) | Rig (Auto-rig) | View (modes + Frame +
    // Ortho) | Display (X-ray/Tex/PBR/Paint/Deform/DQS + Grid/Bones/Wire ovl,
    // same App bools) | Status (Undo/Redo/Validate/FPS) | Panels (">>"
    // overflow popup). Before each group (after the first), when CursorX +
    // groupWidth exceeds the toolbar width the group starts on a new line
    // instead of clipping. Widths: ui_model toolbarGroupWidth() — the single
    // tested source (labels + frame padding + spacing, default style).
    const float toolbarW = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
    bool firstGroup = true;
    auto beginGroup = [&](int group) {
        const float groupW = toolbarGroupWidth(group);
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
    // --- File: New/Open/Save ------------------------------------------------
    beginGroup(0);
    if (ImGui::Button("New")) {
        app.newWorkspace();
        app.setStatus("New workspace created.", "success");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New workspace (clears all assets)");
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
        const DialogResult dlg = openFileDialog(g_mainWindow, "Open workspace",
                                                "M2RIG (*.m2rig)|*.m2rig|All (*.*)|*.*", "");
        if (dlg.confirmed) {
            if (auto r = app.loadWorkspaceFile(dlg.path); !r)
                app.setStatus("Open failed: " + r.error().message, "error");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open .m2rig workspace");
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        const DialogResult dlg = saveFileDialog(g_mainWindow, "Save workspace",
                                                "M2RIG (*.m2rig)|*.m2rig", "m2rig");
        if (dlg.confirmed) {
            if (auto r = app.saveWorkspaceFile(dlg.path); !r)
                app.setStatus("Save failed: " + r.error().message, "error");
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save .m2rig workspace");
    // --- Stage: workflow-stage preset switch (same ui_model matrix) --------
    beginGroup(1);
    for (int p = 0; p < kLayoutPresetCount; ++p) {
        if (p > 0) ImGui::SameLine();
        if (segmentedButton(layoutPresetName(p), layoutPresetMatches(app.uiSettings, p),
                            layoutPresetTip(p))) {
            applyLayoutPreset(app.uiSettings, p);
            app.setStatus(std::string("Layout preset applied: ") + layoutPresetName(p),
                          "success");
        }
    }
    // --- Rig: Auto-rig ------------------------------------------------------
    beginGroup(2);
    // Quick auto-rig: same undoable op as the Weights-panel button, surfaced
    // where new users look first (single call site in App, no duplicate logic).
    // Red family like Flood/Prune: whole-mesh destructive (undoable).
    ImGui::BeginDisabled(app.currentAsset() == nullptr);
    dangerButtonPush();
    if (ImGui::Button("Auto-rig")) {
        if (auto r = app.autoRigFromSkeleton(); !r)
            app.setStatus("Auto-rig failed: " + r.error().message, "error");
    }
    dangerButtonPop();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Auto-rig from skeleton: binds every vertex to nearest bones (undoable)");
    // --- View: modes + Frame + Ortho + Play/Pause ---------------------------
    beginGroup(3);
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
    ImGui::SameLine();
    if (ImGui::Button(app.timelinePlaying ? "Pause" : "Play")) {
        app.timelinePlaying = !app.timelinePlaying;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play/Pause animation (Space)");
    // --- Display: X-ray/Tex/PBR/Paint/Deform/DQS + Grid/Bones/Wire ovl ----
    beginGroup(4);
    bool xray = app.xrayBones;
    if (ImGui::Checkbox(labelXray(), &xray)) app.xrayBones = xray;
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
        app.setStatus(app.usePbr ? "PBR shading on." : "PBR shading off.", "info");
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
    // --- Status: Undo/Redo/Validate/FPS ------------------------------------
    beginGroup(5);
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
    if (ImGui::Button(labelValidate())) app.runValidation();
    tipFor("Run the full compatibility check (<=4 influences, skeleton, sockets, topology)");
    ImGui::SameLine();
    ImGui::TextDisabled("FPS %.0f", app.fps);
    if (app.bridgeBusy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Running %s... %.0fs", app.bridgeJob.label.c_str(),
                            ImGui::GetTime() - app.bridgeJob.startTime);
    }
    // --- Panels: ">>" overflow popup (same App bools, no fork) -------------
    beginGroup(6);
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
        panelBtn("BoneDisp", "Toggle Bone Display panel", &app.uiSettings.showBoneDisplayPanel);
        panelBtn("Gizmo", "Toggle Gizmo panel", &app.uiSettings.showGizmoPanel);
        panelBtn("Viewport Settings", "Toggle Viewport Settings panel",
                 &app.uiSettings.showViewportSettingsPanel);
        panelBtn("Export", "Toggle Export panel", &app.uiSettings.showExportPanel);
        panelBtn("Project", "Toggle Project panel", &app.uiSettings.showProjectPanel);
        panelBtn("Settings", "Toggle Settings panel", &app.uiSettings.showSettingsPanel);
        panelBtn("Validation", "Toggle Validation panel", &app.uiSettings.showValidationPanel);
        panelBtn("Console", "Toggle Console panel", &app.uiSettings.showConsolePanel);
        panelBtn("Timeline", "Toggle Timeline panel", &app.uiSettings.showTimelinePanel);
        panelBtn("Tools", "Toggle Tools (System) panel", &app.uiSettings.showSystemPanel);
        panelBtn("MSM Inspector tab", "Toggle MSM Inspector tab",
                 &app.uiSettings.showMSMInspectorPanel);
        panelBtn("MSE Effects tab", "Toggle MSE Effects tab", &app.uiSettings.mseTabEnabled);
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
        const char* rectStatus = rect.valid ? "ok" : "too small";
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
                ImGui::CalcTextSize("Draw calls: 9999 (no texture)").x + 40.0f;
            const float camNeed = ImGui::CalcTextSize("Dist: 99999.0").x + 40.0f;
            const float msgNeed =
                ImGui::CalcTextSize(app.statusMessage.c_str()).x + 20.0f;
            // Camera drops first, draw drops second (viewport + asset + status stay).
            const bool showCamera = availAfterAsset > drawNeed + camNeed + msgNeed + 80.0f;
            const bool showDraw = availAfterAsset > drawNeed + msgNeed + 80.0f;
            if (showDraw) {
                ImGui::Text("Draw calls: %d", stats.drawCalls);
                if (stats.texturedFallback) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "(no texture)");
                }
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();
            }
            if (showCamera) {
                ImGui::Text("Dist: %.1f", app.camera.distance);
                ImGui::SameLine();
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
                ImGui::SameLine();
            }
            if (!showDraw) {
                (void)stats;
            }
        }

        // Status message: always separated from the camera/draw stats by " | "
        // so a narrow window never runs the two together, and truncated with
        // "..." when it would otherwise extend past the window edge where ImGui
        // clips it mid-word.
        std::string statusText =
            app.statusMessage.empty() ? std::string("Ready") : app.statusMessage;
        ImGui::SameLine();
        ImGui::TextDisabled(" | ");
        ImGui::SameLine();
        float avail = ImGui::GetContentRegionAvail().x;
        float msgWidth = ImGui::CalcTextSize(statusText.c_str()).x + 20;
        if (avail > msgWidth + 100) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - msgWidth);
        } else {
            while (!statusText.empty() &&
                   ImGui::CalcTextSize((statusText + "...").c_str()).x > avail)
                statusText.pop_back();
            statusText += "...";
        }
        ImVec4 color = Theme::statusColor(app.statusKind);
        ImGui::TextColored(color, "%s", statusText.c_str());
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


}  // namespace m2rig
