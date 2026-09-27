#pragma once
// Wave 34 split: shared contract for the src/app/panels_*.cpp translation
// units (panel bodies were mechanically extracted from panels.cpp).
//
// Contract:
//  - EVERY symbol that crosses a panels_* TU boundary is declared here.
//  - State that only ONE TU touches stays file-local (static / anonymous
//    namespace) in that TU; anything reachable from two TUs is extern here.
//  - panels.hpp stays the public app-facing surface (main.cpp/app_gpu.cpp
//    see it, never this header).
//  - imgui types are allowed here (exe-only header); include/m2rig/*.hpp
//    must stay ImGui-free.
//
// NOTE: IMGUI_DEFINE_MATH_OPERATORS must precede <imgui.h> in every TU that
// uses vector operators on ImVec2/ImVec4 — doing it here once is the point.
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#ifdef M2RIG_WITH_GIZMO
#include "ImGuizmo.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "m2rig/app.hpp"
#include "m2rig/file_dialog.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/lod.hpp"
#include "m2rig/mse.hpp"
#include "m2rig/panels.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/ui_model.hpp"  // presets/labels/tokens: single source (no local copies)
#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
// theme module (exe-local): Theme:: forwarders below map to it.
#include "theme.hpp"

namespace m2rig {

// --- shared state (defined in panels.cpp / panels_msm.cpp) -----------------
// Native parent window handle for file dialogs (set once by main.cpp via
// setMainWindowHandle; read by the do* actions and doOpenMse).
extern void* g_mainWindow;

// Live dock layout flag (file-scope on purpose: both Reset paths force a
// rebuild without restart). Defined in panels.cpp.
extern bool g_dockBuilt;
void requestDockRebuild();
void markDockBuilt();
// Dock structure (first-run + both Reset paths). Defined in panels.cpp;
// called from drawAllPanels AND the Settings panel Reset button.
void buildDefaultDockLayout(ImGuiID dockspaceId);

// MSE preview state (panels-local, mirrors the MSM tab pattern). Defined in
// panels_msm.cpp; read by drawSceneContents (viewport tick) and the
// View>Panels "MSE Effects tab" menu toggle in drawAllPanels (the tab toggle
// itself lives in app.uiSettings.mseTabEnabled — Wave 37).
extern MseRuntime g_mse;
extern std::string g_msePath;
extern std::size_t g_mseEmitterCount;
extern bool g_msePreview;
extern bool g_msePlaying;
extern float g_mseTime;
extern float g_mseDuration;

// --- shared helpers (defined in panels.cpp) --------------------------------
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
// Wave 37: AllowWhenDisabled — disabled controls must still explain WHY
// they are disabled (the plain IsItemHovered() silently dropped them).
inline void tipFor(const char* t) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", t);
}

std::vector<GpuVertex> boneSegments(const Skeleton& skel, const App& app,
                                    float jointSize = 0.03f);
void drawSkeletonTree(App& app, std::int32_t boneId, int depth = 0);
#ifdef M2RIG_WITH_GIZMO
void updateBoneGizmo(App& app, const ImVec2& cursor, const ImVec2& avail, bool& gizmoUsing,
                     bool& gizmoOver);
#endif

// --- Semantic theme palette (FORWARDER — single source is ui_model tokens) --
// The actual values live in m2rig::themeTokens() (core floats, pinned by
// tests/test_ui_model.cpp) and are mapped to ImVec4 by src/app/theme.cpp.
// This namespace keeps the historical Theme:: call sites untouched.
// The teal-cyan accent lives in applyDarkTheme (theme.cpp) and is
// intentionally not duplicated here; overlay U32 colors (selection box,
// labels, brush ring) are a separate draw-list family and stay literal.
namespace Theme {
inline ImVec4 kOk = theme::ok();
inline ImVec4 kWarn = theme::warn();
inline ImVec4 kErr = theme::err();
inline ImVec4 kInfo = theme::info();
inline ImVec4 kDanger = theme::danger();
inline ImVec4 kDangerHover = theme::dangerHover();
inline ImVec4 kDangerActive = theme::dangerActive();
inline ImVec4 statusColor(const std::string& kind) { return theme::statusColor(kind); }
}  // namespace Theme

// --- component kit (Wave 37): shared button chrome -------------------------
// Destructive operations share one red family so they read as dangerous at a
// glance (still undoable — the surrounding hint says so). Single source for
// the trio of pushes/pops (was 2 copies with drifted indentation).
inline void dangerButtonPush() {
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::kDanger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kDangerHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kDangerActive);
}
inline void dangerButtonPop() { ImGui::PopStyleColor(3); }

// --- actions (panels_actions.cpp: import/export/workspace file dialogs) -----
void doImportSmd(App& app);
void doExportSmd(App& app);
void doImportBridged(App& app);
void doImportGr2FromModels(App& app);
void doImportGltf(App& app);
void doImportMeshOntoSkeleton(App& app);
void doExportMsm(App& app);
void doExportGr2(App& app);
void doExportGr2ToFbx(App& app);
void doExportFbx(App& app);
void doExportAni(App& app);
void doExportLod(App& app, float ratio);
void doSaveWorkspace(App& app);
void doLoadWorkspace(App& app);
void doOpenMsm(App& app);

// --- command palette + toolbar chrome --------------------------------------
void drawCommandPalette(App& app);
void drawToolbar(App& app);
void drawStatusBar(const App& app, const Renderer& renderer, const ViewportRect& rect);
void drawToasts(App& app);

// --- MSM / MSE tabs ---------------------------------------------------------
void drawMsmInspector(App& app);
void drawMseEffects(App& app);

// --- panel bodies (window titles match the dock builder + drawAllPanels) ----
void drawAssetsPanel(App& app);
void drawScenePanel(App& app);
void drawSkeletonPanel(App& app);
void drawBoneProperties(App& app);
void drawWeightPanel(App& app, Renderer& renderer);
void drawMaterialPanel(App& app);
void drawProjectPanel(App& app, const std::filesystem::path& projectsDir);
void drawExportPanel(App& app);
void drawValidationPanel(App& app);
void drawSystemPanel(App& app);
void drawConsolePanel();
void drawTimelinePanel(App& app);

}  // namespace m2rig
