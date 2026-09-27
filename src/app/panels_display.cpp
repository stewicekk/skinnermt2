// Display settings panels: Settings / Bone Display / Gizmo / Viewport Settings.
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"
#include "theme.hpp"

namespace m2rig {
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
    // Single source: ui_model kLayoutPresets (the local 4x14 copy here was a
    // fork and went stale in Wave 36 — matrix/tips/apply now all come from
    // ui_model, pinned by tests/test_ui_model.cpp).
    for (int pi = 0; pi < kLayoutPresetCount; ++pi) {
        if (pi > 0) ImGui::SameLine();
        if (ImGui::Button(layoutPresetName(pi))) {
            applyLayoutPreset(app.uiSettings, pi);
            app.setStatus(std::string("Layout preset applied: ") + layoutPresetName(pi),
                          "success");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", layoutPresetTip(pi));
    }

    ImGui::Separator();
    // UI scale (font scaling) — live-adjusts ImGui::GetIO().FontGlobalScale.
    {
        float scale = ImGui::GetIO().FontGlobalScale;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("UI Scale", &scale, 0.6f, 2.0f, "%.2f")) {
            ImGui::GetIO().FontGlobalScale = scale;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Font/UI scaling (0.6x - 2.0x)");
    }

    ImGui::Separator();
    // Theme variant (Dark / Light / High Contrast) — full token tables.
    {
        const char* variantNames[kThemeVariantCount];
        for (int i = 0; i < kThemeVariantCount; ++i)
            variantNames[i] = themeVariantName(static_cast<ThemeVariant>(i));
        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("Theme", &app.uiSettings.themeVariant, variantNames,
                         kThemeVariantCount)) {
            theme::applyTheme(static_cast<ThemeVariant>(app.uiSettings.themeVariant));
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Color theme variant (Dark / Light / High Contrast)");
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
    ImGui::Text("Selection");
    ImGui::Checkbox("Apply to selection", &app.gizmoApplyToSelection);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When on and more than one bone is selected, the gizmo operates on the "
            "whole selection (median pivot) instead of just the primary bone.");
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
        ImGui::Checkbox(labelXray(), &app.xrayBones);
        ImGui::Checkbox("Wire Overlay", &app.showWireOverlay);
        ImGui::Checkbox("Textured View", &app.textured);
        ImGui::Separator();
        ImGui::Text("Skinning:");
        ImGui::Checkbox("DQS (Dual Quaternion)", &app.useDqs);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Dual Quaternion Skinning -- avoids candy-wrapper artifacts on twist joints.\nRequires previewDeform=ON for animated deformation preview.");
    }
}

}  // namespace m2rig
