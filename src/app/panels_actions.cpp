// Command palette (Ctrl+K) + import/export/workspace/MSM file actions.
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"

namespace m2rig {
void doImportSmd(App& app) {
    const DialogResult dlg =
        openFileDialog(g_mainWindow, "Import SMD model", "SMD files (*.smd)|*.smd|All files (*.*)|*.*", "smd");
    if (!dlg.confirmed) return;
    if (auto r = app.importSmdFile(dlg.path); !r)
        app.setStatus("SMD import failed: " + r.error().message, "error");
}

// Mesh-only import ("import clean mesh onto existing rig"): replaces only the
// current asset's geometry, keeping the skeleton/rig + Wave-22 coord profiles.
// The file dialog mirrors doImportBridged's format filter; statuses come from
// the method itself (success / honest failure), none duplicated here.
void doImportMeshOntoSkeleton(App& app) {
    if (!app.currentAsset()) {
        app.setStatus("No asset loaded — load a skeleton first.", "warning");
        return;
    }
    const DialogResult dlg = openFileDialog(
        g_mainWindow, "Import mesh (keep skeleton)",
        "Mesh files (*.smd;*.fbx;*.gltf;*.glb;*.obj)|*.smd;*.fbx;*.gltf;*.glb;*.obj|SMD "
        "(*.smd)|*.smd|FBX (*.fbx)|*.fbx|glTF (*.gltf;*.glb)|*.gltf;*.glb|OBJ (*.obj)|*.obj|All "
        "(*.*)|*.*",
        "");
    if (!dlg.confirmed) return;
    app.importMeshOntoSkeleton(dlg.path);
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

// GR2 import anchored at the Metin2 Data/Models directory — same resolution as
// the System panel scan (exe-local first, CWD fallback). The dialog is filtered
// to .gr2 only; the selected file goes through the SAME bridge path as
// doImportBridged (startBridgedImport) — no fork.
void doImportGr2FromModels(App& app) {
    if (app.bridgeBusy) {
        app.setStatus("A bridge import is already running.", "warning");
        return;
    }
    std::filesystem::path models = executableDir() / "Data" / "Models";
    if (!std::filesystem::exists(models))
        models = std::filesystem::current_path() / "Data" / "Models";
    const DialogResult dlg = openFileDialog(
        g_mainWindow, "Load GR2 from Data/Models", "GR2 (*.gr2)|*.gr2|All (*.*)|*.*", "gr2",
        models.string());
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

// Native GR2 export (Gr2Writer): same dialog shape as the bridge export, but
// the write is fully native (no external tool). Statuses come from
// App::exportGr2Native itself (success / honest failure), none duplicated.
void doExportGr2Native(App& app) {
    LoadedAsset* a = app.currentAsset();
    if (!a) {
        app.setStatus("No asset loaded.", "error");
        return;
    }
    const DialogResult dlg = saveFileDialog(g_mainWindow, "Export GR2 (native)",
                                            "GR2 files (*.gr2)|*.gr2", "gr2", a->id + ".gr2");
    if (!dlg.confirmed) return;
    if (auto r = app.exportGr2Native(dlg.path); !r)
        app.setStatus("GR2 native export failed: " + r.error().message, "error");
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

namespace {
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
    {labelValidateMenu(), "Run the full compatibility check", cmdPalAlways,
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
    {"Load GR2 from Data/Models...", "Import a Metin2 GR2 model via bridge", cmdPalBridgeFree,
     [](App& app) { doImportGr2FromModels(app); },  // reuses doImportGr2FromModels
     "bridge busy"},
    {"Import glTF...", "Import glTF model (.glb/.gltf)", cmdPalBridgeFree,
     [](App& app) { doImportGltf(app); },  // reuses doImportGltf
     "bridge busy"},
    {"Import mesh (keep skeleton)...", "Replace geometry, keep skeleton/rig", cmdPalHasAsset,
     [](App& app) { doImportMeshOntoSkeleton(app); },  // reuses doImportMeshOntoSkeleton
     "no asset"},
    {"Export SMD...", "Write skeletal mesh, re-validated, gate enforced", cmdPalHasAsset,
     [](App& app) { doExportSmd(app); },  // reuses doExportSmd
     "no asset"},
    {"Export MSM...", "Write Metin2 mesh groups, re-validated, gate enforced", cmdPalHasAsset,
      [](App& app) { doExportMsm(app); },  // reuses doExportMsm
      "no asset"},
    {"Export GR2 (native)...", "Write Metin2 GR2 binary (native writer), re-validated, gate enforced",
      cmdPalHasAsset,
      [](App& app) { doExportGr2Native(app); },  // reuses doExportGr2Native
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
}  // namespace

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


}  // namespace m2rig
