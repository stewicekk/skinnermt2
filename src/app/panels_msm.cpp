// MSM Inspector + MSE Effects tabs (panels-local MSE transport state).
// Wave 34 mechanical split: extracted verbatim from panels.cpp.

#include "panels_internal.hpp"

namespace m2rig {
namespace {
void drawMsmNode(const MsmNode& node) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    char label[256];
    std::snprintf(label, sizeof(label), "%s  [#%u]", node.name.c_str(), node.lineNumber);
    const bool open = ImGui::TreeNodeEx(label, flags);
    for (const auto& [key, val] : node.attributes)
        ImGui::TextDisabled("  %s = %s", key.c_str(), val.c_str());
    if (open) {
        for (const auto& child : node.children) drawMsmNode(child);
        ImGui::TreePop();
    }
}
}  // namespace

void drawMsmInspector(App& app) {
    ImGui::Text("MSM Inspector");
    ImGui::Separator();
    if (ImGui::Button("Open .msm...")) doOpenMsm(app);
    if (!app.msmDoc) {
        ImGui::SameLine();
        ImGui::TextDisabled("No MSM loaded (reference view only).");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) app.closeMsmInspector();
    ImGui::TextDisabled("%s", app.msmPath.c_str());
    ImGui::Text("%s", app.msmReport.summaryLine().c_str());
    if (ImGui::BeginChild("msm_tree", ImVec2(0, 0), true)) drawMsmNode(app.msmDoc->root);
    ImGui::EndChild();
}

// --- MSE Effects preview (Wave-30b, panels-local state) ----------------------
// App carries no MSE fields (headers are out of scope for this slice), so the
// effect document + transport live here as TU-local state, mirroring the MSM
// Inspector tab pattern (doOpenMsm/drawMsmInspector above).
// MseRuntime::update had zero callers before this slice (repo-wide grep finds
// only the mse.cpp definition); the per-frame tick in drawSceneContents below
// is its first caller.
MseRuntime g_mse;
std::string g_msePath;
std::size_t g_mseEmitterCount = 0;
bool g_msePreview = true;     // in-tab "Preview in viewport" checkbox
bool g_msePlaying = false;
float g_mseTime = 0.0f;
float g_mseDuration = 5.0f;
namespace {

float mseEffectDuration(const MseDocument& doc) {
    float dur = 0.0f;
    for (const auto& att : doc.attachments)
        for (const auto& em : att.emitters)
            if (std::isfinite(em.lifeTime) && em.lifeTime > dur) dur = em.lifeTime;
    // Global loop clock for the time slider; per-emitter loop flags are
    // approximated as loop (documented at the tick). Zero-emitter or
    // degenerate documents still get a scrubable range.
    if (!std::isfinite(dur) || dur <= 0.0f) return 5.0f;
    return dur > 600.0f ? 600.0f : dur;
}

void doOpenMse(App& app) {
    const DialogResult dlg = openFileDialog(g_mainWindow, "Open MSE effect",
                                            "MSE files (*.mse)|*.mse|All (*.*)|*.*", "mse");
    if (!dlg.confirmed) return;
    std::ifstream file(dlg.path, std::ios::binary);
    if (!file.is_open()) {
        app.setStatus("MSE open failed: cannot open file", "error");
        return;
    }
    std::ostringstream textStream;
    textStream << file.rdbuf();
    auto parsed = parseMse(textStream.str(), dlg.path);
    if (!parsed) {
        g_mse = MseRuntime{};
        g_msePath.clear();
        g_mseEmitterCount = 0;
        g_msePlaying = false;
        g_mseTime = 0.0f;
        app.setStatus("MSE open failed: " + parsed.error().message, "error");
        return;
    }
    g_mse = MseRuntime{};
    g_mse.doc = std::move(parsed.value());
    g_msePath = dlg.path;
    g_mseEmitterCount = 0;
    for (const auto& att : g_mse.doc.attachments) g_mseEmitterCount += att.emitters.size();
    g_mseDuration = mseEffectDuration(g_mse.doc);
    g_mseTime = 0.0f;
    g_msePlaying = true;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "MSE loaded: %zu attachment(s), %zu emitter(s) — preview in Tools > MSE Effects.",
                  g_mse.doc.attachments.size(), g_mseEmitterCount);
    app.setStatus(buf, g_mse.doc.attachments.empty() ? "warning" : "success");
}
}  // namespace

void drawMseEffects(App& app) {
    ImGui::Text("MSE Effects");
    ImGui::Separator();
    if (ImGui::Button("Open .mse...")) doOpenMse(app);
    if (g_msePath.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("No MSE loaded (effect preview only).");
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        g_mse = MseRuntime{};
        g_msePath.clear();
        g_mseEmitterCount = 0;
        g_msePlaying = false;
        g_mseTime = 0.0f;
        app.setStatus("MSE closed.", "info");
    }
    ImGui::TextDisabled("%s", g_msePath.c_str());
    ImGui::Text("%zu attachment(s), %zu emitter(s)", g_mse.doc.attachments.size(),
                g_mseEmitterCount);
    ImGui::Checkbox("Preview in viewport", &g_msePreview);
    ImGui::BeginDisabled(g_mse.doc.attachments.empty());
    if (ImGui::Button(g_msePlaying ? "Pause" : "Play")) g_msePlaying = !g_msePlaying;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("No attachments in document.");
    ImGui::SameLine();
    ImGui::SliderFloat("Time", &g_mseTime, 0.0f, g_mseDuration, "%.2f s");
    ImGui::EndDisabled();
    if (g_mseTime < 0.0f) g_mseTime = 0.0f;
    if (g_mseTime > g_mseDuration) g_mseTime = g_mseDuration;
    if (!app.currentAsset())
        ImGui::TextDisabled("Load a model to preview particles in the viewport.");
    else if (!g_msePreview)
        ImGui::TextDisabled("Preview disabled — tick the checkbox to draw particles.");
    else if (g_mse.doc.attachments.empty())
        ImGui::TextDisabled("Document holds no attachments — nothing to preview.");
    if (ImGui::BeginChild("mse_tree", ImVec2(0, 0), true)) {
        for (std::size_t ai = 0; ai < g_mse.doc.attachments.size(); ++ai) {
            const MseAttachment& att = g_mse.doc.attachments[ai];
            ImGui::PushID(static_cast<int>(ai));
            char label[192];
            std::snprintf(label, sizeof(label), "%s  (%zu emitter%s)", att.boneName.c_str(),
                          att.emitters.size(), att.emitters.size() == 1 ? "" : "s");
            ImGuiTreeNodeFlags flags =
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if (att.emitters.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx(label, flags);
            if (open) {
                for (std::size_t ei = 0; ei < att.emitters.size(); ++ei) {
                    const MseEmitter& em = att.emitters[ei];
                    ImGui::PushID(static_cast<int>(ei));
                    ImGui::Text("%s", em.name.c_str());
                    ImGui::TextDisabled("type=%s rate=%.1f/s life=%.2fs size=%.2f->%.2f",
                                        em.type.c_str(), em.emitRate, em.lifeTime, em.startSize,
                                        em.endSize);
                    if (!em.texturePath.empty())
                        ImGui::TextDisabled("texture=%s", em.texturePath.c_str());
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}  // namespace m2rig
