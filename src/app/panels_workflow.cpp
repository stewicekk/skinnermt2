// Workflow column + Output row: Materials / Project / Export / Validation
// / Tools / Console / Timeline panels.
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"

namespace m2rig {
namespace {
bool cachedPathExists(const std::string& literalPath, double nowSeconds) {
    static std::map<std::string, std::pair<double, bool>> cache;  // path -> (time, hit)
    const auto it = cache.find(literalPath);
    if (it != cache.end() && nowSeconds - it->second.first <= 2.0) return it->second.second;
    std::error_code ec;
    const bool hit = std::filesystem::exists(literalPath, ec) && !ec;
    cache[literalPath] = {nowSeconds, hit};
    return hit;
}

}  // namespace
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
        ImGui::SameLine();
        if (ImGui::Button("Browse##tex")) {
            const DialogResult dlg = openFileDialog(g_mainWindow, "Select albedo texture",
                                                    "DDS (*.dds)|*.dds|All (*.*)|*.*", "");
            if (dlg.confirmed) {
                m.texturePath = dlg.path;
                a->dirty = true;
                a->gpuDirty = true;
            }
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
                       (cachedPathExists((dir / base).string(), nowS) ||
                        cachedPathExists((dir / "Data" / "Models" / base).string(), nowS));
            };
            // Ancestor-walking probe: same logic as resolveTextureFile in
            // app_gpu.cpp — walk UP the directory tree from exeDir and
            // current_path() so textures in parent Data/Models are found.
            auto probeAncestors = [&](std::filesystem::path root) {
                while (!root.empty()) {
                    if (existsUnder(root)) return true;
                    const std::filesystem::path parent = root.parent_path();
                    if (parent == root) break;
                    root = parent;
                }
                return false;
            };
            const bool found =
                !base.empty() && (cachedPathExists(m.texturePath, nowS) ||
                probeAncestors(exeDir) || probeAncestors(std::filesystem::current_path()));
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
        ImGui::SameLine();
        if (ImGui::Button("Browse##nrm")) {
            const DialogResult dlg = openFileDialog(g_mainWindow, "Select normal map",
                                                    "DDS (*.dds)|*.dds|All (*.*)|*.*", "");
            if (dlg.confirmed) {
                m.normalTexturePath = dlg.path;
                a->dirty = true;
                a->gpuDirty = true;
            }
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
        // UV transforms (offset/scale/rotation/wrap mode)
        ImGui::Separator();
        ImGui::Text("UV Transform");
        if (ImGui::SliderFloat2("Offset", m.uvOffset, -10.0f, 10.0f)) { a->dirty = true; a->gpuDirty = true; }
        if (ImGui::SliderFloat2("Scale", m.uvScale, 0.01f, 10.0f)) { a->dirty = true; a->gpuDirty = true; }
        if (ImGui::SliderFloat("Rotation", &m.uvRotation, -3.14159f, 3.14159f)) { a->dirty = true; a->gpuDirty = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("UV rotation in radians");
        const char* wrapNames[] = {"Wrap", "Clamp", "Mirror"};
        int wrapMode = static_cast<int>(m.wrapMode);
        if (ImGui::Combo("Wrap", &wrapMode, wrapNames, 3)) {
            m.wrapMode = static_cast<float>(wrapMode);
            a->dirty = true; a->gpuDirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Texture wrapping mode");
        // Auto-discover normal map: if material texture is foo.dds, look for foo_n.dds
        if (m.normalTexturePath.empty() && !m.texturePath.empty()) {
            std::filesystem::path texPath(m.texturePath);
            std::string stem = texPath.stem().string();
            std::string parent = texPath.parent_path().string();
            const char* normalSuffixes[] = {"_n", "_normal", "_nm", "_norm"};
            for (const char* suffix : normalSuffixes) {
                std::string candidate = parent + "/" + stem + suffix + ".dds";
                if (std::filesystem::exists(candidate)) {
                    m.normalTexturePath = candidate;
                    a->dirty = true; a->gpuDirty = true;
                    ImGui::TextColored(Theme::kOk, "Auto-found normal map: %s", (stem + suffix + ".dds").c_str());
                    break;
                }
            }
        }
        ImGui::PopID();
    }
    // Per-submesh PBR overrides (metallic/roughness/AO per submesh)
    ImGui::Separator();
    ImGui::Text("Per-submesh PBR");
    int selSubmesh = app.selectedSubmesh >= 0 ? app.selectedSubmesh : 0;
    int submeshCount = static_cast<int>(a->mesh.subMeshes.size());
    if (submeshCount > 0) {
        if (ImGui::SliderInt("Submesh", &selSubmesh, 0, submeshCount - 1)) {
            app.selectSubmesh(selSubmesh);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Select a submesh to override its PBR factors");
        auto& sp = a->submeshPbr[static_cast<std::size_t>(selSubmesh)];
        if (ImGui::SliderFloat("Metallic##sub", &sp.metallic, 0.0f, 1.0f)) a->dirty = true;
        if (ImGui::SliderFloat("Roughness##sub", &sp.roughness, 0.05f, 1.0f)) a->dirty = true;
        if (ImGui::SliderFloat("AO##sub", &sp.ao, 0.0f, 1.0f)) a->dirty = true;
        if (ImGui::Button("Reset##sub")) {
            a->submeshPbr.erase(static_cast<std::size_t>(selSubmesh));
            a->dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove override, use asset-level PBR");
    } else {
        ImGui::TextDisabled("No submeshes.");
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
            if (ImGui::GetItemRectMax().x + expStyle.ItemSpacing.x + expNeedW(label) <=
                sectionW)
                ImGui::SameLine();
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
        flow("Export GR2 (native)...");
        if (ImGui::Button("Export GR2 (native)...")) doExportGr2Native(app);
        tipFor("Granny3D export via the native writer (no external tool needed)");
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
        ImGui::BeginDisabled(app.assets.size() < 2);
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
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Needs two loaded assets");
        flow("Self-learning auto-rig");
        ImGui::BeginDisabled(app.currentAsset() == nullptr);
        if (ImGui::Button("Self-learning auto-rig")) {
            auto r = app.selfLearningAutoRig();
            if (!r)
                app.setStatus("Auto-rig failed: " + r.error().message, "error");
            else
                app.setStatus("Self-learning auto-rig complete.", "success");
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Needs a loaded asset");
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

namespace {
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
}  // namespace

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

}  // namespace m2rig
