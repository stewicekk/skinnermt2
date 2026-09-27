#pragma once
// Pure UI model: the single source of truth for layout presets, panel
// descriptors, dock zones, status/toast semantics, view-mode labels, toolbar
// groups, the command-palette matcher and the theme token VALUES.
//
// Contract:
//  - Zero third-party deps (NO ImGui types) so m2rig_tests links it through
//    m2rig_core and pins exactly what the exe-only UI renders.
//  - Values are extracted verbatim from the pre-Wave-33 literals
//    (panels.cpp / main.cpp); behavior-changing visual fixes land later by
//    editing BOTH this table and its test in the same change.
//  - include/m2rig/*.hpp never includes <imgui.h> (core stays portable).
#include <cstddef>
#include <string>
#include <string_view>

#include "m2rig/app.hpp"

namespace m2rig {

// --- Semantic status kinds (status bar + toasts, one vocabulary) -----------
// Strings stay the persisted wire format ("info"|"success"|"warning"|"error");
// unknown strings degrade to Info (never transparent, never black).
enum class StatusKind { Info = 0, Success = 1, Warning = 2, Error = 3 };
StatusKind statusKindFrom(const std::string& kind);
const char* statusKindName(StatusKind kind);
// Error toasts stay until dismissed; warning 8 s; info/success 5 s.
bool statusKindSticky(StatusKind kind);
double statusKindToastTtl(StatusKind kind);
// Priority for the status queue: error > warning > success > info.
int statusKindPriority(StatusKind kind);
// Toast queue cap (oldest dropped first — current App::pushToast behavior).
constexpr std::size_t kToastQueueCap = 6;

// --- Theme token VALUES (plain floats; exe maps them to ImVec4) ------------
struct Rgba {
    float r, g, b, a;
};
struct ThemeTokens {
    // Accent (teal-cyan family)
    Rgba accent;          // Button / Header / TabActive fill
    Rgba accentHover;
    Rgba accentActive;
    Rgba accentBright;    // CheckMark / SliderGrab / DockingPreview
    Rgba accentSoft;      // selection wash (TextSelectedBg) + segmented-selected
    // Semantic feedback
    Rgba success;         // ok / passed
    Rgba warn;            // THE one amber (warnings, missing textures)
    Rgba danger;          // error text
    Rgba info;            // informational status text
    Rgba infoMuted;       // Severity::Info rows (validation/console lists)
    Rgba destructive;     // destructive button fill (Auto-rig, Flood/Prune)
    Rgba destructiveHover;
    Rgba destructiveActive;
    // Surfaces (deepest -> raised)
    Rgba surface0;        // DockingEmptyBg
    Rgba surface1;        // WindowBg
    Rgba surface2;        // ChildBg / MenuBarBg
    Rgba surface3;        // PopupBg / TitleBgActive
    Rgba surfaceInput;    // FrameBg
    // Text
    Rgba textPrimary;
    Rgba textSecondary;   // captions / key-value labels
    Rgba textDisabled;
    Rgba textOnAccent;    // text over accent / destructive fills
    // Structure
    Rgba border;
    // Chrome (promoted from one-off theme.cpp literals — fully data-driven).
    Rgba frameHover;        // FrameBgHovered
    Rgba frameActive;       // FrameBgActive
    Rgba titleActive;       // TitleBgActive
    Rgba sliderGrabActive;  // SliderGrabActive
    Rgba separatorHover;    // SeparatorHovered
    Rgba separatorActive;   // SeparatorActive
    Rgba tab;               // Tab
    Rgba tabHover;          // TabHovered
    Rgba tabUnfocusedActive;// TabUnfocusedActive
    Rgba scrollbarGrab;     // ScrollbarGrab
    Rgba scrollbarGrabHover;
    Rgba scrollbarGrabActive;
    Rgba resizeGrip;        // ResizeGrip
    Rgba resizeGripHover;
    Rgba resizeGripActive;
    Rgba dockingPreview;    // DockingPreview
    // Single source for the D3D11 viewport clear color (was 4 copies).
    Rgba viewportClear;
    // Data-viz only (heatmap ramp) — NOT a semantic UI color.
    Rgba heatmap[5];
};
const ThemeTokens& themeTokens();

// Theme variants: Dark is the canonical table (themeTokens()); Light and
// HighContrast are full separate tables (a light theme is not an invert of
// dark — elevation/border logic flips). Selected at runtime, persisted.
enum class ThemeVariant { Dark = 0, Light = 1, HighContrast = 2 };
constexpr int kThemeVariantCount = 3;
const char* themeVariantName(ThemeVariant v);
const ThemeTokens& themeTokensFor(ThemeVariant v);

// ImGuiStyle metrics currently written by applyDarkTheme (main.cpp) — kept
// here as pure floats so a future style module and the tests share one table.
struct StyleMetrics {
    float windowRounding, childRounding, frameRounding, grabRounding;
    float popupRounding, scrollbarRounding, tabRounding;
    float windowBorderSize, frameBorderSize, tabBorderSize;
    float windowPaddingX, windowPaddingY;
    float framePaddingX, framePaddingY;
    float itemSpacingX, itemSpacingY;
    float itemInnerSpacingX, itemInnerSpacingY;
    float indentSpacing, scrollbarSize, grabMinSize;
};
const StyleMetrics& styleMetrics();

// --- Layout presets (4x14, columns = App::UISettings declaration order) ----
constexpr int kPanelFlagCount = 14;
constexpr int kLayoutPresetCount = 4;
bool layoutPresetValue(int preset, int column);  // false when out of range
const char* layoutPresetName(int preset);
const char* layoutPresetTip(int preset);
void applyLayoutPreset(App::UISettings& ui, int preset);
// True when ui's 14 panel flags exactly equal the preset's row (toolbar
// Stage-group active highlight; presets are pairwise distinct).
bool layoutPresetMatches(const App::UISettings& ui, int preset);

// --- Panel descriptors: one table for dock + menu + toolbar + prefs --------
// window      = ImGui::Begin title ("" for tab-only panels)
// dockWindow  = window the panel lands in (tab-only panels use their host)
// menuLabel   = View > Panels label ("" = not listed there)
// prefsKey    = user_prefs.json key ("" = not persisted)
// presetColumn= index into the 14-column preset matrix, -1 = not in it
struct PanelDescriptor {
    const char* id;
    const char* window;
    const char* dockWindow;
    const char* menuLabel;
    const char* prefsKey;
    int presetColumn;
    bool defaultVisible;
};
const PanelDescriptor* panelDescriptors(std::size_t& count);
const PanelDescriptor* findPanelDescriptor(std::string_view id);
// Pointer to the live flag for a descriptor (nullptr when presetColumn < 0).
bool* panelFlagPtr(App::UISettings& ui, const PanelDescriptor& d);

// --- Dock plan (structure only; ratios + zone membership) ------------------
struct DockRatios {
    float left;                // Left navigator column
    float right;               // Right column (Props + Workflow share it)
    float bottom;              // Center-bottom row (Output + Tools share it)
    float top;                 // Toolbar strip
    float timeline;            // Full-width Timeline strip (Wave 36: split
                               // BEFORE left/right so it spans the window)
    float rightWorkflowSplit;  // Workflow share OF the right column (Props 60)
    float bottomToolsSplit;    // Tools share OF the bottom row (Output 60)
};
const DockRatios& dockRatios();
enum class DockZone {
    Top,
    Left,
    Main,
    RightProps,
    RightWorkflow,
    BottomOutput,
    BottomTools,
    TimelineStrip,
};
const char* const* dockZoneWindows(DockZone zone, std::size_t& count);

// --- Status bar (shared chrome constants + measured collapse) --------------
constexpr float kStatusBarHeight = 24.0f;
// Toast anchor = bar height + gap; stack step per toast. Keep these together:
// if they drift, toasts land under the bar (solution-judge D3 companion).
constexpr float kToastAnchorAboveBottom = 34.0f;
constexpr float kToastStackStepPx = 70.0f;
// Camera drops first, draw-calls drop second; viewport/asset/status always show.
bool statusBarShowCamera(float avail, float drawNeed, float camNeed, float msgNeed);
bool statusBarShowDraw(float avail, float drawNeed, float msgNeed);

// --- View modes (menu + segmented control + shortcuts 1-7 share one table) -
constexpr int kViewModeCount = 7;
const char* viewModeName(ViewMode mode);        // menu label ("Solid + Wire")
const char* viewModeShortLabel(ViewMode mode);  // segmented ("S+W")
const char* viewModeTip(ViewMode mode);         // tooltip with digit shortcut
const char* viewModeNameByIndex(int index);     // nullptr when out of range

// --- Toolbar groups (wrap rule: measured widths, single source) -----------
// Render order (Wave 36b): Stage first (workflow-stage preset switch), then
// task-first Rig, then View | Display | Status | Panels.
constexpr int kToolbarGroupCount = 7;
const char* toolbarGroupName(int group);
float toolbarGroupWidth(int group);  // measured widths for the default style
bool toolbarShouldWrap(float cursorX, float groupWidth, float toolbarWidth);

// --- Command palette matcher (earliest case-insensitive substring) --------
int uiFuzzyMatchPos(std::string_view hay, std::string_view needle);

// --- Gate predicates (menu / panel / palette share one source) ------------
bool exportActionEnabled(bool hasAsset);

// --- Canonical labels (deduplicates N-places-per-verb drift) ---------------
const char* labelXray();          // the one X-ray toggle label
const char* labelValidate();      // button label
const char* labelValidateMenu();  // menu + palette alias (fuzzy search keeps it)
const char* labelFrameTip();      // shared Frame tooltip

}  // namespace m2rig
