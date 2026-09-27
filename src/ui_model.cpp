// Pure UI model — see include/m2rig/ui_model.hpp for the contract.
// Every value here was extracted VERBATIM from the pre-Wave-33 literals in
// src/app/panels.cpp (presets, dock, status bar, labels, ramp) and
// src/app/main.cpp (style metrics + colors). Behavior-changing visual fixes
// edit this table AND tests/test_ui_model.cpp in the same change.
#include "m2rig/ui_model.hpp"

#include <cctype>

namespace m2rig {

// --- Semantic status kinds --------------------------------------------------
StatusKind statusKindFrom(const std::string& kind) {
    if (kind == "success") return StatusKind::Success;
    if (kind == "warning") return StatusKind::Warning;
    if (kind == "error") return StatusKind::Error;
    return StatusKind::Info;  // unknown degrades to Info (never transparent)
}
const char* statusKindName(StatusKind kind) {
    switch (kind) {
        case StatusKind::Success: return "success";
        case StatusKind::Warning: return "warning";
        case StatusKind::Error: return "error";
        case StatusKind::Info: break;
    }
    return "info";
}
bool statusKindSticky(StatusKind kind) { return kind == StatusKind::Error; }
double statusKindToastTtl(StatusKind kind) {
    // Mirrors App::pushToast: error 0 (sticky), warning 8 s, else 5 s.
    switch (kind) {
        case StatusKind::Error: return 0.0;
        case StatusKind::Warning: return 8.0;
        case StatusKind::Success:
        case StatusKind::Info: break;
    }
    return 5.0;
}
int statusKindPriority(StatusKind kind) { return static_cast<int>(kind); }

// --- Theme token VALUES -----------------------------------------------------
const ThemeTokens& themeTokens() {
    // "newschool" dark palette — electric indigo accent on a cool blue-grey
    // surface ramp. Accent is color-blind-safe (blue axis preserved for
    // deuteranopia/protanopia) and reads modern/premium (Blender 4.x, VS Code,
    // Figma family). All chrome promoted to tokens (fully data-driven).
    static const ThemeTokens kTokens{
        // Accent family (electric indigo)
        {0.42f, 0.35f, 0.92f, 1.00f},   // accent
        {0.50f, 0.43f, 0.96f, 1.00f},   // accentHover
        {0.35f, 0.28f, 0.85f, 1.00f},   // accentActive
        {0.60f, 0.52f, 1.00f, 1.00f},   // accentBright
        {0.42f, 0.35f, 0.92f, 0.32f},   // accentSoft (TextSelectedBg wash)
        // Semantic feedback
        {0.30f, 0.85f, 0.55f, 1.00f},   // success
        {0.95f, 0.72f, 0.25f, 1.00f},   // warn (amber)
        {0.95f, 0.42f, 0.38f, 1.00f},   // danger (error text)
        {0.55f, 0.65f, 0.80f, 1.00f},   // info
        {0.40f, 0.48f, 0.58f, 1.00f},   // infoMuted
        {0.88f, 0.30f, 0.28f, 1.00f},   // destructive
        {0.95f, 0.38f, 0.35f, 1.00f},   // destructiveHover
        {0.78f, 0.24f, 0.22f, 1.00f},   // destructiveActive
        // Surfaces (deepest -> raised, cool blue-grey)
        {0.055f, 0.060f, 0.075f, 1.00f}, // surface0 (DockingEmptyBg)
        {0.070f, 0.078f, 0.095f, 1.00f}, // surface1 (WindowBg)
        {0.088f, 0.098f, 0.118f, 1.00f}, // surface2 (ChildBg/MenuBarBg)
        {0.108f, 0.120f, 0.145f, 1.00f}, // surface3 (PopupBg)
        {0.130f, 0.145f, 0.175f, 1.00f}, // surfaceInput (FrameBg)
        // Text
        {0.93f, 0.94f, 0.96f, 1.00f},   // textPrimary
        {0.62f, 0.66f, 0.72f, 1.00f},   // textSecondary
        {0.42f, 0.45f, 0.50f, 1.00f},   // textDisabled
        {0.97f, 0.97f, 1.00f, 1.00f},   // textOnAccent
        // Structure
        {0.18f, 0.20f, 0.25f, 1.00f},   // border (== Separator)
        // Chrome (promoted from one-off theme.cpp literals)
        {0.16f, 0.18f, 0.22f, 1.00f},   // frameHover
        {0.20f, 0.23f, 0.28f, 1.00f},   // frameActive
        {0.11f, 0.13f, 0.17f, 1.00f},   // titleActive
        {0.60f, 0.52f, 1.00f, 1.00f},   // sliderGrabActive
        {0.60f, 0.52f, 1.00f, 0.50f},   // separatorHover
        {0.60f, 0.52f, 1.00f, 1.00f},   // separatorActive
        {0.11f, 0.13f, 0.17f, 1.00f},   // tab
        {0.50f, 0.43f, 0.96f, 1.00f},   // tabHover
        {0.13f, 0.15f, 0.19f, 1.00f},   // tabUnfocusedActive
        {0.22f, 0.25f, 0.30f, 1.00f},   // scrollbarGrab
        {0.32f, 0.36f, 0.42f, 1.00f},   // scrollbarGrabHover
        {0.40f, 0.45f, 0.52f, 1.00f},   // scrollbarGrabActive
        {0.60f, 0.52f, 1.00f, 0.25f},   // resizeGrip
        {0.60f, 0.52f, 1.00f, 0.67f},   // resizeGripHover
        {0.60f, 0.52f, 1.00f, 0.95f},   // resizeGripActive
        {0.60f, 0.52f, 1.00f, 0.40f},   // dockingPreview
        // Viewport clear (darker than surface0 — a "void" framing the 3D)
        {0.040f, 0.045f, 0.060f, 1.00f},
        // Heatmap ramp (modernized blue -> red jet)
        {{0.10f, 0.20f, 0.85f, 1.00f},
         {0.15f, 0.65f, 0.85f, 1.00f},
         {0.35f, 0.85f, 0.45f, 1.00f},
         {0.95f, 0.80f, 0.20f, 1.00f},
         {0.90f, 0.20f, 0.15f, 1.00f}},
    };
    return kTokens;
}

const ThemeTokens& themeTokensFor(ThemeVariant v) {
    // Light: blue-tinted whites (not pure white — OLED smearing + harsh
    // contrast), indigo accent darkened for contrast on light surfaces.
    static const ThemeTokens kLight{
        {0.30f, 0.45f, 0.90f, 1.00f},   // accent
        {0.25f, 0.40f, 0.85f, 1.00f},   // accentHover
        {0.20f, 0.35f, 0.75f, 1.00f},   // accentActive
        {0.45f, 0.55f, 0.95f, 1.00f},   // accentBright
        {0.30f, 0.45f, 0.90f, 0.25f},   // accentSoft
        {0.20f, 0.65f, 0.35f, 1.00f},   // success
        {0.85f, 0.60f, 0.15f, 1.00f},   // warn
        {0.85f, 0.25f, 0.20f, 1.00f},   // danger
        {0.35f, 0.45f, 0.60f, 1.00f},   // info
        {0.50f, 0.55f, 0.62f, 1.00f},   // infoMuted
        {0.80f, 0.20f, 0.18f, 1.00f},   // destructive
        {0.88f, 0.28f, 0.25f, 1.00f},   // destructiveHover
        {0.70f, 0.15f, 0.12f, 1.00f},   // destructiveActive
        {0.90f, 0.91f, 0.93f, 1.00f},   // surface0
        {0.96f, 0.97f, 0.98f, 1.00f},   // surface1
        {0.93f, 0.94f, 0.96f, 1.00f},   // surface2
        {0.90f, 0.91f, 0.93f, 1.00f},   // surface3
        {1.00f, 1.00f, 1.00f, 1.00f},   // surfaceInput
        {0.08f, 0.09f, 0.12f, 1.00f},   // textPrimary
        {0.35f, 0.38f, 0.42f, 1.00f},   // textSecondary
        {0.55f, 0.57f, 0.60f, 1.00f},   // textDisabled
        {0.97f, 0.97f, 1.00f, 1.00f},   // textOnAccent
        {0.75f, 0.77f, 0.80f, 1.00f},   // border
        {0.88f, 0.89f, 0.91f, 1.00f},   // frameHover
        {0.85f, 0.86f, 0.88f, 1.00f},   // frameActive
        {0.94f, 0.95f, 0.96f, 1.00f},   // titleActive
        {0.45f, 0.55f, 0.95f, 1.00f},   // sliderGrabActive
        {0.45f, 0.55f, 0.95f, 0.50f},   // separatorHover
        {0.45f, 0.55f, 0.95f, 1.00f},   // separatorActive
        {0.94f, 0.95f, 0.96f, 1.00f},   // tab
        {0.25f, 0.40f, 0.85f, 1.00f},   // tabHover
        {0.92f, 0.93f, 0.95f, 1.00f},   // tabUnfocusedActive
        {0.80f, 0.82f, 0.85f, 1.00f},   // scrollbarGrab
        {0.70f, 0.72f, 0.76f, 1.00f},   // scrollbarGrabHover
        {0.60f, 0.63f, 0.68f, 1.00f},   // scrollbarGrabActive
        {0.45f, 0.55f, 0.95f, 0.25f},   // resizeGrip
        {0.45f, 0.55f, 0.95f, 0.67f},   // resizeGripHover
        {0.45f, 0.55f, 0.95f, 0.95f},   // resizeGripActive
        {0.45f, 0.55f, 0.95f, 0.40f},   // dockingPreview
        {0.93f, 0.94f, 0.96f, 1.00f},   // viewportClear
        {{0.10f, 0.20f, 0.85f, 1.00f},
         {0.15f, 0.65f, 0.85f, 1.00f},
         {0.35f, 0.85f, 0.45f, 1.00f},
         {0.95f, 0.80f, 0.20f, 1.00f},
         {0.90f, 0.20f, 0.15f, 1.00f}},
    };
    // HighContrast: pure black surfaces, white text, cyan accent, white
    // borders — maximum legibility for low-vision users.
    static const ThemeTokens kHighContrast{
        {0.00f, 0.80f, 1.00f, 1.00f},   // accent (pure cyan)
        {0.00f, 0.90f, 1.00f, 1.00f},   // accentHover
        {0.00f, 1.00f, 1.00f, 1.00f},   // accentActive
        {0.50f, 1.00f, 1.00f, 1.00f},   // accentBright
        {0.00f, 0.80f, 1.00f, 0.35f},   // accentSoft
        {0.00f, 1.00f, 0.00f, 1.00f},   // success
        {1.00f, 1.00f, 0.00f, 1.00f},   // warn
        {1.00f, 0.00f, 0.00f, 1.00f},   // danger
        {0.00f, 1.00f, 1.00f, 1.00f},   // info
        {0.50f, 1.00f, 1.00f, 1.00f},   // infoMuted
        {1.00f, 0.00f, 0.00f, 1.00f},   // destructive
        {1.00f, 0.20f, 0.20f, 1.00f},   // destructiveHover
        {0.80f, 0.00f, 0.00f, 1.00f},   // destructiveActive
        {0.00f, 0.00f, 0.00f, 1.00f},   // surface0
        {0.00f, 0.00f, 0.00f, 1.00f},   // surface1
        {0.05f, 0.05f, 0.05f, 1.00f},   // surface2
        {0.10f, 0.10f, 0.10f, 1.00f},   // surface3
        {0.00f, 0.00f, 0.00f, 1.00f},   // surfaceInput
        {1.00f, 1.00f, 1.00f, 1.00f},   // textPrimary
        {0.85f, 0.85f, 0.85f, 1.00f},   // textSecondary
        {0.60f, 0.60f, 0.60f, 1.00f},   // textDisabled
        {0.00f, 0.00f, 0.00f, 1.00f},   // textOnAccent
        {1.00f, 1.00f, 1.00f, 1.00f},   // border
        {0.15f, 0.15f, 0.15f, 1.00f},   // frameHover
        {0.20f, 0.20f, 0.20f, 1.00f},   // frameActive
        {0.08f, 0.08f, 0.08f, 1.00f},   // titleActive
        {0.50f, 1.00f, 1.00f, 1.00f},   // sliderGrabActive
        {0.50f, 1.00f, 1.00f, 0.50f},   // separatorHover
        {0.50f, 1.00f, 1.00f, 1.00f},   // separatorActive
        {0.08f, 0.08f, 0.08f, 1.00f},   // tab
        {0.00f, 0.90f, 1.00f, 1.00f},   // tabHover
        {0.12f, 0.12f, 0.12f, 1.00f},   // tabUnfocusedActive
        {0.30f, 0.30f, 0.30f, 1.00f},   // scrollbarGrab
        {0.50f, 0.50f, 0.50f, 1.00f},   // scrollbarGrabHover
        {0.70f, 0.70f, 0.70f, 1.00f},   // scrollbarGrabActive
        {0.50f, 1.00f, 1.00f, 0.25f},   // resizeGrip
        {0.50f, 1.00f, 1.00f, 0.67f},   // resizeGripHover
        {0.50f, 1.00f, 1.00f, 0.95f},   // resizeGripActive
        {0.50f, 1.00f, 1.00f, 0.40f},   // dockingPreview
        {0.00f, 0.00f, 0.00f, 1.00f},   // viewportClear
        {{0.10f, 0.20f, 0.85f, 1.00f},
         {0.15f, 0.65f, 0.85f, 1.00f},
         {0.35f, 0.85f, 0.45f, 1.00f},
         {0.95f, 0.80f, 0.20f, 1.00f},
         {0.90f, 0.20f, 0.15f, 1.00f}},
    };
    switch (v) {
        case ThemeVariant::Light: return kLight;
        case ThemeVariant::HighContrast: return kHighContrast;
        case ThemeVariant::Dark: break;
    }
    return themeTokens();
}

const char* themeVariantName(ThemeVariant v) {
    switch (v) {
        case ThemeVariant::Light: return "Light";
        case ThemeVariant::HighContrast: return "High Contrast";
        case ThemeVariant::Dark: break;
    }
    return "Dark";
}

const StyleMetrics& styleMetrics() {
    // Modern, slightly soft — not the flat 2012 look, not the bubbly 2024 look.
    static const StyleMetrics kMetrics{
        8.0f, 6.0f, 5.0f, 4.0f,  // window/child/frame/grab rounding
        6.0f, 3.0f, 5.0f,        // popup/scrollbar/tab rounding
        1.0f, 1.0f, 1.0f,        // window/frame/tab border
        12.0f, 10.0f,            // window padding
        8.0f, 5.0f,              // frame padding
        8.0f, 6.0f,              // item spacing
        6.0f, 4.0f,              // item inner spacing
        20.0f, 14.0f, 12.0f,     // indent / scrollbar / grab min
    };
    return kMetrics;
}

// --- Layout presets (verbatim from panels.cpp kLayoutPresets) ---------------
namespace {
// Columns follow App::UISettings declaration order: Bone, Weights, Materials,
// MSMInspector, Project, Export, Validation, Console, System/Tools, Timeline,
// Settings, BoneDisplay, Gizmo, ViewportSettings.
constexpr bool kLayoutPresets[kLayoutPresetCount][kPanelFlagCount] = {
    // Rig: bone + weights + MSM + tools + project + validation + bone display
    // + gizmo (Wave 36: MSM/Tools enabled — the rigging stage needs both).
    {true, true, false, true, true, false, true, false, true, false, false, true,
     true, false},
    // Paint: bone + weights + materials + viewport settings (brush context).
    {true, true, true, false, false, false, false, false, false, false, false, false,
     false, true},
    // Anim: bone + timeline + gizmo.
    {true, false, false, false, false, false, false, false, false, true, false, false,
     true, false},
    // Review: materials + export + validation + console + tools + timeline.
    {false, false, true, false, false, true, true, true, true, true, false, false,
     false, false},
};
constexpr const char* kLayoutPresetNames[kLayoutPresetCount] = {"Rig", "Paint", "Anim",
                                                                "Review"};
constexpr const char* kLayoutPresetTips[kLayoutPresetCount] = {
    "Rigging: Bone + Weights + MSM + Tools + Project + Validation + Gizmo + Bone Display",
    "Paint: Bone + Weights + Materials + Viewport Settings",
    "Animation: Bone + Timeline + Gizmo",
    "Review: Materials + Export + Validation + Console + Tools + Timeline",
};
}  // namespace

bool layoutPresetValue(int preset, int column) {
    if (preset < 0 || preset >= kLayoutPresetCount) return false;
    if (column < 0 || column >= kPanelFlagCount) return false;
    return kLayoutPresets[preset][column];
}
const char* layoutPresetName(int preset) {
    if (preset < 0 || preset >= kLayoutPresetCount) return "";
    return kLayoutPresetNames[preset];
}
const char* layoutPresetTip(int preset) {
    if (preset < 0 || preset >= kLayoutPresetCount) return "";
    return kLayoutPresetTips[preset];
}
void applyLayoutPreset(App::UISettings& ui, int preset) {
    if (preset < 0 || preset >= kLayoutPresetCount) return;
    bool* flags[kPanelFlagCount] = {
        &ui.showBonePanel,         &ui.showWeightsPanel,     &ui.showMaterialsPanel,
        &ui.showMSMInspectorPanel, &ui.showProjectPanel,     &ui.showExportPanel,
        &ui.showValidationPanel,   &ui.showConsolePanel,     &ui.showSystemPanel,
        &ui.showTimelinePanel,     &ui.showSettingsPanel,    &ui.showBoneDisplayPanel,
        &ui.showGizmoPanel,        &ui.showViewportSettingsPanel,
    };
    for (int c = 0; c < kPanelFlagCount; ++c) *flags[c] = kLayoutPresets[preset][c];
}
bool layoutPresetMatches(const App::UISettings& ui, int preset) {
    if (preset < 0 || preset >= kLayoutPresetCount) return false;
    const bool flags[kPanelFlagCount] = {
        ui.showBonePanel,         ui.showWeightsPanel,     ui.showMaterialsPanel,
        ui.showMSMInspectorPanel, ui.showProjectPanel,     ui.showExportPanel,
        ui.showValidationPanel,   ui.showConsolePanel,     ui.showSystemPanel,
        ui.showTimelinePanel,     ui.showSettingsPanel,    ui.showBoneDisplayPanel,
        ui.showGizmoPanel,        ui.showViewportSettingsPanel,
    };
    for (int c = 0; c < kPanelFlagCount; ++c)
        if (flags[c] != kLayoutPresets[preset][c]) return false;
    return true;
}

// --- Panel descriptors (dock titles + menu labels + prefs keys) -------------
namespace {
constexpr PanelDescriptor kPanels[] = {
    // Flagged panels, preset-column order.
    {"bone", "Bone", "Bone", "Bone", "showBonePanel", 0, true},
    {"weights", "Weights", "Weights", "Weights", "showWeightsPanel", 1, true},
    {"materials", "Materials", "Materials", "Materials", "showMaterialsPanel", 2, true},
    {"msm", "", "Tools", "MSM Inspector tab", "showMSMInspectorPanel", 3, true},
    {"project", "Project", "Project", "Project", "showProjectPanel", 4, true},
    {"export", "Export", "Export", "Export", "showExportPanel", 5, true},
    {"validation", "Validation", "Validation", "Validation", "showValidationPanel", 6, true},
    {"console", "Console", "Console", "Console", "showConsolePanel", 7, true},
    {"tools", "Tools", "Tools", "Tools", "showSystemPanel", 8, true},
    {"timeline", "Timeline", "Timeline", "Timeline", "showTimelinePanel", 9, true},
    {"settings", "Settings", "Settings", "Settings", "showSettingsPanel", 10, true},
    {"bone_display", "Bone Display", "Bone Display", "Bone Display", "showBoneDisplayPanel",
     11, false},
    {"gizmo", "Gizmo", "Gizmo", "Gizmo", "showGizmoPanel", 12, false},
    {"viewport_settings", "Viewport Settings", "Viewport Settings", "Viewport Settings",
     "showViewportSettingsPanel", 13, false},
    // Always-on chrome (no flag, not in the preset matrix, not persisted).
    {"toolbar", "Toolbar", "Toolbar", "", "", -1, true},
    {"assets", "Assets", "Assets", "", "", -1, true},
    {"scene", "Scene", "Scene", "", "", -1, true},
    {"skeleton", "Skeleton", "Skeleton", "", "", -1, true},
    {"viewport", "Viewport", "Viewport", "", "", -1, true},
};
}  // namespace

const PanelDescriptor* panelDescriptors(std::size_t& count) {
    count = sizeof(kPanels) / sizeof(kPanels[0]);
    return kPanels;
}
const PanelDescriptor* findPanelDescriptor(std::string_view id) {
    for (const auto& p : kPanels) {
        if (id == p.id) return &p;
    }
    return nullptr;
}
bool* panelFlagPtr(App::UISettings& ui, const PanelDescriptor& d) {
    switch (d.presetColumn) {
        case 0: return &ui.showBonePanel;
        case 1: return &ui.showWeightsPanel;
        case 2: return &ui.showMaterialsPanel;
        case 3: return &ui.showMSMInspectorPanel;
        case 4: return &ui.showProjectPanel;
        case 5: return &ui.showExportPanel;
        case 6: return &ui.showValidationPanel;
        case 7: return &ui.showConsolePanel;
        case 8: return &ui.showSystemPanel;
        case 9: return &ui.showTimelinePanel;
        case 10: return &ui.showSettingsPanel;
        case 11: return &ui.showBoneDisplayPanel;
        case 12: return &ui.showGizmoPanel;
        case 13: return &ui.showViewportSettingsPanel;
        default: break;
    }
    return nullptr;
}

// --- Dock plan (verbatim from buildDefaultDockLayout) ----------------------
const DockRatios& dockRatios() {
    static const DockRatios kRatios{0.17f, 0.35f, 0.24f, 0.075f, 0.10f, 0.4f, 0.4f};
    return kRatios;
}
const char* const* dockZoneWindows(DockZone zone, std::size_t& count) {
    static const char* const kTop[] = {"Toolbar"};
    static const char* const kLeft[] = {"Assets", "Scene", "Skeleton"};
    static const char* const kMain[] = {"Viewport"};
    static const char* const kRightProps[] = {"Bone", "Weights", "Materials",
                                              "Bone Display", "Gizmo", "Viewport Settings"};
    static const char* const kRightWorkflow[] = {"Export", "Project", "Settings"};
    static const char* const kBottomOut[] = {"Validation", "Console"};
    static const char* const kBottomTools[] = {"Tools"};
    static const char* const kTimeline[] = {"Timeline"};
    switch (zone) {
        case DockZone::Top: count = 1; return kTop;
        case DockZone::Left: count = 3; return kLeft;
        case DockZone::Main: count = 1; return kMain;
        case DockZone::RightProps: count = 6; return kRightProps;
        case DockZone::RightWorkflow: count = 3; return kRightWorkflow;
        case DockZone::BottomOutput: count = 2; return kBottomOut;
        case DockZone::BottomTools: count = 1; return kBottomTools;
        case DockZone::TimelineStrip: count = 1; return kTimeline;
    }
    count = 0;
    return nullptr;
}

// --- Status bar -------------------------------------------------------------
bool statusBarShowCamera(float avail, float drawNeed, float camNeed, float msgNeed) {
    return avail > drawNeed + camNeed + msgNeed + 80.0f;
}
bool statusBarShowDraw(float avail, float drawNeed, float msgNeed) {
    return avail > drawNeed + msgNeed + 80.0f;
}

// --- View modes -------------------------------------------------------------
namespace {
constexpr const char* kViewModeNames[kViewModeCount] = {"Solid",       "Wireframe", "Solid + Wire",
                                                        "Normals",     "Height",    "Weights",
                                                        "UV"};
constexpr const char* kViewModeShort[kViewModeCount] = {"Solid", "Wire", "S+W",
                                                       "Nrml",  "Hght", "Wght", "UV"};
constexpr const char* kViewModeTips[kViewModeCount] = {
    "Solid (1)", "Wireframe (2)", "Solid + Wire (3)", "Normals (4)",
    "Height (5)", "Weights heatmap — needs a selected bone (6)", "UV checker (7)"};
int viewModeIndex(ViewMode mode) {
    const int i = static_cast<int>(mode);
    return (i >= 0 && i < kViewModeCount) ? i : 0;
}
}  // namespace

const char* viewModeName(ViewMode mode) { return kViewModeNames[viewModeIndex(mode)]; }
const char* viewModeShortLabel(ViewMode mode) { return kViewModeShort[viewModeIndex(mode)]; }
const char* viewModeTip(ViewMode mode) { return kViewModeTips[viewModeIndex(mode)]; }
const char* viewModeNameByIndex(int index) {
    if (index < 0 || index >= kViewModeCount) return nullptr;
    return kViewModeNames[index];
}

// --- Toolbar groups ---------------------------------------------------------
namespace {
constexpr const char* kToolbarGroupNames[kToolbarGroupCount] = {
    "File", "Stage", "Rig", "View", "Display", "Status", "Panels"};
// Default-style estimates (button/checkbox label + frame padding + spacing).
// beginGroup consumes these via toolbarGroupWidth() — single source; tests
// pin names/order/positivity. File = New/Open/Save. Stage = 4 preset buttons
// (Rig/Paint/Anim/Review). Display = 9 checkboxes incl. the canonical "X-ray bones" label.
constexpr float kToolbarGroupWidths[kToolbarGroupCount] = {200.0f, 260.0f, 110.0f, 660.0f,
                                                            680.0f, 300.0f, 60.0f};
}  // namespace

const char* toolbarGroupName(int group) {
    if (group < 0 || group >= kToolbarGroupCount) return "";
    return kToolbarGroupNames[group];
}
float toolbarGroupWidth(int group) {
    if (group < 0 || group >= kToolbarGroupCount) return 0.0f;
    return kToolbarGroupWidths[group];
}
bool toolbarShouldWrap(float cursorX, float groupWidth, float toolbarWidth) {
    return cursorX + groupWidth > toolbarWidth;
}

// --- Command palette matcher (verbatim semantics from cmdPalMatchPos) ------
namespace {
char fuzzyLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
}  // namespace

int uiFuzzyMatchPos(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return 0;
    if (needle.size() > hay.size()) return -1;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool hit = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (fuzzyLower(hay[i + j]) != fuzzyLower(needle[j])) {
                hit = false;
                break;
            }
        }
        if (hit) return static_cast<int>(i);
    }
    return -1;
}

// --- Gate predicates --------------------------------------------------------
bool exportActionEnabled(bool hasAsset) { return hasAsset; }

// --- Canonical labels -------------------------------------------------------
const char* labelXray() { return "X-ray bones"; }
const char* labelValidate() { return "Validate"; }
const char* labelValidateMenu() { return "Run validation"; }
const char* labelFrameTip() { return "Fit the whole model in view (F)"; }

}  // namespace m2rig
