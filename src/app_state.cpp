// Application state implementation.
#include "m2rig/app.hpp"

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "m2rig/logging.hpp"
#include "m2rig/coordsys.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/ast/msm_ast.hpp"
#ifdef M2RIG_WITH_OPENFBX
#include "m2rig/fbx/fbx_reader.hpp"
#endif
#ifdef M2RIG_WITH_CGLTF
#include "m2rig/gltf/gltf_reader.hpp"
#endif
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/workspace/project_file.hpp"

namespace m2rig {

const char* brushModeName(BrushMode mode) {
    switch (mode) {
        case BrushMode::Add: return "Add";
        case BrushMode::Subtract: return "Subtract";
        case BrushMode::Smooth: return "Smooth";
        case BrushMode::Normalize: return "Normalize";
        case BrushMode::Blur: return "Blur";
        case BrushMode::Sharpen: return "Sharpen";
    }
    return "Add";
}

const char* drawPathName(DrawPath path) {
    switch (path) {
        case DrawPath::SolidUntextured: return "SolidUntextured";
        case DrawPath::SolidTextured: return "SolidTextured";
        case DrawPath::FlatDebug: return "FlatDebug";
        case DrawPath::WireBlinn: return "WireBlinn";
        case DrawPath::PbrSolid: return "PbrSolid";
        case DrawPath::PbrTextured: return "PbrTextured";
    }
    return "SolidUntextured";
}

DrawPath resolveDrawPath(ViewMode mode, bool usePbr, bool wantTextured, bool hasTexture,
                         bool skinned) {
    // Accepted-but-ignored today (see the app.hpp contract): the renderer owns
    // the untextured fallback and the Skinned draw suffix, so routing stays
    // byte-identical while the signature reserves the G4 split points.
    (void)hasTexture;
    (void)skinned;
    switch (mode) {
        case ViewMode::Normals:
        case ViewMode::Height:
        case ViewMode::Weights:
        case ViewMode::UV:
            return DrawPath::FlatDebug;
        case ViewMode::Wireframe:
            return DrawPath::WireBlinn;
        case ViewMode::Solid:
        case ViewMode::SolidWireframe:
            break;
    }
    if (usePbr && wantTextured) return DrawPath::PbrTextured;
    if (usePbr) return DrawPath::PbrSolid;
    if (wantTextured) return DrawPath::SolidTextured;
    return DrawPath::SolidUntextured;
}

LoadedAsset* App::currentAsset() {
    auto it = assets.find(current);
    return it == assets.end() ? nullptr : &it->second;
}

const LoadedAsset* App::currentAsset() const {
    auto it = assets.find(current);
    return it == assets.end() ? nullptr : &it->second;
}

void App::setStatus(std::string message, std::string kind) {
    statusMessage = std::move(message);
    statusKind = std::move(kind);
    if (statusKind == "error")
        Logger::instance().error(statusMessage, "app");
    else if (statusKind == "warning")
        Logger::instance().warning(statusMessage, "app");
    else
        Logger::instance().info(statusMessage, "app");
}

void App::pushToast(std::string message, std::string kind, double nowSeconds) {
    static std::size_t nextId = 1;
    Toast t;
    t.message = std::move(message);
    t.kind = std::move(kind);
    t.sticky = (t.kind == "error");
    const double ttl = t.kind == "error" ? 0.0 : (t.kind == "warning" ? 8.0 : 5.0);
    t.until = nowSeconds + ttl;
    t.id = nextId++;
    toasts.push_back(std::move(t));
    if (toasts.size() > 6) toasts.erase(toasts.begin());
}

void App::dismissToast(std::size_t id) {
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [id](const Toast& t) { return t.id == id; }),
                 toasts.end());
}

void App::tickToasts(double nowSeconds) {
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [nowSeconds](const Toast& t) {
                                    return !t.sticky && nowSeconds >= t.until;
                                }),
                 toasts.end());
}

ResultVoid App::loadSampleArmor() {
    auto res = makeSampleArmor();
    if (!res) return ResultVoid::fail(res.error());
    SampleArmor sample = std::move(res.value());
    LoadedAsset asset;
    asset.id = "sample-warrior-armor";
    asset.mesh = std::move(sample.mesh);
    asset.skeleton = std::move(sample.skeleton);
    for (const auto& b : asset.skeleton.bones) asset.bindInverse.push_back(b.inverseBindTransform);
    asset.profileId = "pc_warrior";
    asset.gpuDirty = true;
    assets[asset.id] = std::move(asset);
    current = "sample-warrior-armor";
    selectedBone = -1;
    selectedBones.clear();
    hiddenBones.clear();
    soloBone = -1;
    selectedVertex = -1;
    lockedBones.clear();  // per-asset guard state must not leak into the fresh sample
    hiddenSubmeshes.clear();
    clearUndoHistory();  // snapshots reference the previous mesh, never reuse them
    noteWeightsChanged();
    if (const LoadedAsset* a = currentAsset(); a) camera.frameAabb(a->mesh.bounds);
    runValidation();
    setStatus("Loaded sample warrior armor (" + std::to_string(assets[current].mesh.vertices.size()) +
                  " vertices, " + std::to_string(assets[current].skeleton.bones.size()) + " bones).",
              "success");
    return ResultVoid::ok();
}

ResultVoid App::loadSampleArmorForProfile(const std::string& profileId) {
    const SkeletonProfile* prof = findProfile(profileId);
    if (!prof)
        return ResultVoid::fail("Unknown skeleton profile: " + profileId + ".", "PROFILE");
    auto res = makeSampleArmorForProfile(prof->identity);
    if (!res) return ResultVoid::fail(res.error());
    SampleArmor sample = std::move(res.value());
    LoadedAsset asset;
    asset.id = "sample-" + prof->race + "-" + prof->gender;
    asset.mesh = std::move(sample.mesh);
    asset.skeleton = std::move(sample.skeleton);
    for (const auto& b : asset.skeleton.bones) asset.bindInverse.push_back(b.inverseBindTransform);
    asset.profileId = prof->identity;
    asset.gpuDirty = true;
    const std::string newId = asset.id;
    assets[newId] = std::move(asset);
    current = newId;
    selectedBone = -1;
    selectedBones.clear();
    hiddenBones.clear();
    soloBone = -1;
    selectedVertex = -1;
    lockedBones.clear();
    hiddenSubmeshes.clear();
    clearUndoHistory();
    noteWeightsChanged();
    if (const LoadedAsset* a = currentAsset(); a) camera.frameAabb(a->mesh.bounds);
    runValidation();
    setStatus("Loaded sample template " + prof->identity + " (" +
                  std::to_string(assets[current].mesh.vertices.size()) + " vertices, " +
                  std::to_string(assets[current].skeleton.bones.size()) + " bones).",
              "success");
    return ResultVoid::ok();
}

Result<std::vector<App::BatchRow>> App::exportAllBatch() {
    if (assets.empty()) return Result<std::vector<BatchRow>>::fail("No assets loaded.", "EXPORT");
    // Base dir: next to the first asset with a source file, else temp.
    std::filesystem::path base;
    for (const auto& [id, a] : assets) {
        if (!a.sourcePath.empty()) {
            base = std::filesystem::path(a.sourcePath).parent_path() / "batch_export";
            break;
        }
    }
    if (base.empty())
        base = std::filesystem::temp_directory_path() / "m2rig_batch";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) return Result<std::vector<BatchRow>>::fail("Cannot create batch dir: " + ec.message());
    const std::string prev = current;
    std::vector<BatchRow> rows;
    std::size_t okCount = 0;
    for (auto& [id, a] : assets) {
        BatchRow row;
        row.id = id;
        current = id;
        row.smdPath = (base / (id + ".smd")).string();
        row.msmPath = (base / (id + ".msm")).string();
        if (auto r = exportSmdFile(row.smdPath); r)
            row.smdOk = true;
        else
            row.message += "SMD: " + r.error().message + " ";
        if (auto r = exportMsmFile(row.msmPath); r)
            row.msmOk = true;
        else
            row.message += "MSM: " + r.error().message;
        if (row.smdOk && row.msmOk) ++okCount;
        rows.push_back(std::move(row));
    }
    current = prev;
    runValidation();
    setStatus("Batch export: " + std::to_string(okCount) + "/" + std::to_string(rows.size()) +
                  " assets OK -> " + base.string(),
              okCount == rows.size() ? "success" : "warning");
    return Result<std::vector<BatchRow>>::ok(std::move(rows));
}

void App::runValidation() {
    report.clear();
    LoadedAsset* a = currentAsset();
    if (!a) {
        report.add("APP_NO_ASSET", ValidationCategory::Export, Severity::Info,
                   "No asset loaded; nothing to validate.", "", "", false);
        return;
    }
    validateMeshStructure(a->mesh, a->id, report);
    validateSkeleton(a->skeleton, a->id, report);
    validateMeshWeights(a->mesh, a->skeleton.bones.size(), a->id, report);
    if (const SkeletonProfile* profile = findProfile(a->profileId); profile) {
        validateAgainstProfile(a->skeleton, *profile, a->id, report);
        validateSocketDeformUse(a->mesh, a->skeleton, *profile, a->id, report);
    } else {
        report.add("APP_NO_PROFILE", ValidationCategory::Skeleton, Severity::Warning,
                   "Unknown skeleton profile '" + a->profileId + "'.", a->id, "", false);
    }
    // Metin2 export-readiness gate (spec section 68, wave-1 subset).
    if (!report.exportBlocked()) {
        report.add("EXPORT_READY_W1", ValidationCategory::Export, Severity::Info,
                   "Wave-1 checks passed (mesh/skeleton/weights/profile). "
                   "Format exporters arrive in waves 2/5/7.",
                   a->id, "", false);
    }
    setStatus("Validation: " + report.summaryLine(), report.exportBlocked() ? "error" : "success");
}

void App::clearUndoHistory() {
    undoStack.clear();
    redoStack.clear();
}

void App::noteWeightsChanged() {
    boneHistogramDirty_ = true;
    weightQualityDirty_ = true;
}

std::size_t App::boneInfluenceCount(std::uint32_t boneId) const {
    const LoadedAsset* a = currentAsset();
    if (!a) return 0;
    if (boneHistogramDirty_) {
        boneHistogram_.assign(a->skeleton.bones.size(), 0);
        for (const auto& v : a->mesh.vertices) {
            for (const auto& inf : v.influences) {
                if (inf.weight > 0.0f && inf.bone < boneHistogram_.size()) {
                    ++boneHistogram_[inf.bone];
                }
            }
        }
        boneHistogramDirty_ = false;
    }
    if (boneId < boneHistogram_.size()) return boneHistogram_[boneId];
    // Non-canonical id outside the histogram: rare direct scan (same rule).
    std::size_t n = 0;
    for (const auto& v : a->mesh.vertices)
        for (const auto& inf : v.influences)
            if (inf.bone == boneId && inf.weight > 0.0f) {
                ++n;
                break;
            }
    return n;
}

const WeightQualityMetrics& App::weightQuality() const {
    if (weightQualityDirty_) {
        if (const LoadedAsset* a = currentAsset())
            weightQualityCache_ = computeWeightQuality(a->mesh);
        else
            weightQualityCache_ = WeightQualityMetrics{};
        weightQualityDirty_ = false;
    }
    return weightQualityCache_;
}

void App::setBoneLocked(std::uint32_t bone, bool locked) {
    if (locked)
        lockedBones.insert(bone);
    else
        lockedBones.erase(bone);
}

void App::lockSocketBones() {
    LoadedAsset* a = currentAsset();
    if (!a) {
        setStatus("No asset loaded.", "error");
        return;
    }
    std::size_t n = 0;
    if (const SkeletonProfile* prof = findProfile(a->profileId); prof) {
        for (const auto& sock : prof->socketBones) {
            for (const auto& b : a->skeleton.bones) {
                if (canonicalBoneName(*prof, b.name) == sock && lockedBones.insert(b.id).second)
                    ++n;
            }
        }
    }
    setStatus("Locked " + std::to_string(n) + " socket bones.", "success");
}

void App::unlockAllBones() {
    lockedBones.clear();
    setStatus("All bones unlocked.", "success");
}

bool App::isBoneSelected(std::uint32_t bone) const {
    return selectedBones.count(bone) != 0;
}

void App::selectBone(std::uint32_t bone, bool additive) {
    if (!additive) selectedBones.clear();
    selectedBones.insert(bone);
    selectedBone = static_cast<int>(bone);
    if (LoadedAsset* a = currentAsset()) a->gpuDirty = true;
}

void App::clearBoneSelection() {
    selectedBones.clear();
    selectedBone = -1;
    if (LoadedAsset* a = currentAsset()) a->gpuDirty = true;
}

void App::selectBoneHierarchy(std::uint32_t bone, bool additive) {
    LoadedAsset* a = currentAsset();
    if (!a) return;
    if (!additive) selectedBones.clear();
    std::vector<std::uint32_t> stack{bone};
    while (!stack.empty()) {
        const std::uint32_t id = stack.back();
        stack.pop_back();
        selectedBones.insert(id);
        if (const Bone* b = a->skeleton.findById(id)) {
            for (std::uint32_t c : b->children) stack.push_back(c);
        }
    }
    selectedBone = static_cast<int>(bone);
    a->gpuDirty = true;
}

void App::saveBoneSelectionSet(const std::string& name) {
    if (name.empty()) return;
    LoadedAsset* a = currentAsset();
    std::set<std::string> names;
    if (a) {
        for (std::uint32_t id : selectedBones) {
            if (const Bone* b = a->skeleton.findById(id)) names.insert(b->name);
        }
    }
    boneSelectionSets[name] = std::move(names);
}

bool App::loadBoneSelectionSet(const std::string& name) {
    auto it = boneSelectionSets.find(name);
    if (it == boneSelectionSets.end()) return false;
    LoadedAsset* a = currentAsset();
    if (!a) return false;
    selectedBones.clear();
    selectedBone = -1;
    for (const std::string& n : it->second) {
        if (const Bone* b = a->skeleton.findByName(n)) {
            selectedBones.insert(b->id);
            if (selectedBone < 0) selectedBone = static_cast<int>(b->id);
        }
    }
    a->gpuDirty = true;
    return selectedBone >= 0;
}

void App::deleteBoneSelectionSet(const std::string& name) {
    boneSelectionSets.erase(name);
}

bool App::boneVisible(std::uint32_t bone) const {
    if (soloBone >= 0) return bone == static_cast<std::uint32_t>(soloBone);
    return hiddenBones.count(bone) == 0;
}

void App::setBoneHidden(std::uint32_t bone, bool hidden) {
    if (hidden)
        hiddenBones.insert(bone);
    else
        hiddenBones.erase(bone);
    if (LoadedAsset* a = currentAsset()) a->gpuDirty = true;
}

void App::clearHiddenBones() {
    hiddenBones.clear();
    soloBone = -1;
    if (LoadedAsset* a = currentAsset()) a->gpuDirty = true;
}

namespace {

std::string stemOf(const std::string& path) {
    std::string name = std::filesystem::path(path).stem().string();
    if (name.empty()) name = "imported-model";
    for (char& c : name) {
        if (c == ' ' || c == '.') c = '_';
    }
    return name;
}

}  // namespace

ResultVoid App::importSmdFile(const std::string& path) {
    auto text = readTextFile(path, path);
    if (!text) return ResultVoid::fail(text.error());
    return applySmdText(text.value(), path);
}

ResultVoid App::installConverted(Mesh mesh, Skeleton skeleton,
                                std::vector<SmdFrame> frames, const std::string& srcPath,
                                const std::string& how) {
    LoadedAsset asset;
    asset.id = stemOf(srcPath);
    asset.mesh = std::move(mesh);
    asset.skeleton = std::move(skeleton);
    for (const auto& b : asset.skeleton.bones) asset.bindInverse.push_back(b.inverseBindTransform);
    asset.animFrames = std::move(frames);
    asset.sourcePath = srcPath;
    asset.gpuDirty = true;
    const std::string newId = asset.id;
    const std::size_t nVerts = asset.mesh.vertices.size();
    const std::size_t nTris = asset.mesh.triangleCount();
    const std::size_t nBones = asset.skeleton.bones.size();
    const std::size_t nFrames = asset.animFrames.size();
    assets[newId] = std::move(asset);
    current = newId;
    selectedBone = -1;
    selectedBones.clear();
    hiddenBones.clear();
    soloBone = -1;
    selectedVertex = -1;
    lockedBones.clear();
    hiddenSubmeshes.clear();
    clearUndoHistory();
    noteWeightsChanged();
    if (const LoadedAsset* a = currentAsset()) camera.frameAabb(a->mesh.bounds);
    runValidation();
    char buf[384];
    std::snprintf(buf, sizeof(buf), "%s (%zu vertices, %zu triangles, %zu bones, %zu frames).",
                  how.c_str(), nVerts, nTris, nBones, nFrames);
    setStatus(buf, report.exportBlocked() ? "warning" : "success");
    return ResultVoid::ok();
}

namespace {

// Shared worker core: produces SMD text for gr2/fbx bridge imports.
// Runs on a worker thread: touches only the filesystem, subprocesses and
// the thread-safe Logger — never App, ImGui or status state. Text-only:
// canonical coordsys conversion happens on the UI thread in
// applyBridgedSmdText (same profiles as the sync path — HARD RULE).
Result<App::BridgeChainOutput> runBridgeChain(std::string path, std::string ext) {
    if (ext == ".gr2") {
        if (auto smd = convertGr2ToSmdViaGrnReader(path); smd)
            return Result<App::BridgeChainOutput>::ok({smd.value(), "gr2"});
        // Fall through to Noesis with the grnreader error preserved below.
    }
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    if (!std::filesystem::exists(cfg.noesisCliPath)) {
        return Result<App::BridgeChainOutput>::fail(
            "Noesis bridge not configured (missing noesis/Noesis.exe).", "IMPORT", path,
            "bridge.import");
    }
    static std::atomic<unsigned> counter{0};
    const std::string tmp =
        (std::filesystem::temp_directory_path() /
         ("m2rig_bridge_" + std::to_string(::GetCurrentProcessId()) + "_" +
          std::to_string(counter.fetch_add(1)) + ".smd"))
            .string();
    std::wstring cmd = L"\"" + cfg.noesisCliPath.wstring() + L"\" ?cmode \"" +
                       std::filesystem::path(path).wstring() + L"\" \"" +
                       std::filesystem::path(tmp).wstring() + L"\"";
    const BridgeResult br = runBridgeLogged(cmd, cfg.timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);
    if (!br.started) {
        // Best-effort: never leave the tmp behind on a failed start.
        std::error_code startEc;
        std::filesystem::remove(tmp, startEc);
        return Result<App::BridgeChainOutput>::fail("Failed to start Noesis bridge process.",
                                                    "IMPORT", path, "bridge.import");
    }
    if (br.timedOut) {
        // Best-effort: the terminated bridge may have left a partial file.
        std::error_code timeoutEc;
        std::filesystem::remove(tmp, timeoutEc);
        return Result<App::BridgeChainOutput>::fail(
            "Noesis bridge timed out. Last output: " + lastLine, "IMPORT", path, "bridge.import");
    }
    if (br.exitCode != 0) {
        // Fail closed: a failing bridge that leaves a file behind must NOT
        // be installed (same pattern as exportFbxFile).
        std::error_code exitEc;
        std::filesystem::remove(tmp, exitEc);
        return Result<App::BridgeChainOutput>::fail(
            "Noesis exited with code " + std::to_string(br.exitCode) +
                ". Last output: " + lastLine,
            "IMPORT", path, "bridge.import");
    }
    std::ifstream in(tmp, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        // Best-effort: the tmp name is unique per call, so a missing file
        // means the bridge produced nothing — remove anyway for an explicit
        // no-litter contract.
        std::error_code openEc;
        std::filesystem::remove(tmp, openEc);
        return Result<App::BridgeChainOutput>::fail(
            "Bridge produced no SMD output. Last output: " + lastLine, "IMPORT", path,
            "bridge.import");
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    in.close();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return Result<App::BridgeChainOutput>::ok({buf.str(), "noesis"});
}

}  // namespace

ResultVoid App::applySmdText(const std::string& smdText, const std::string& srcPath) {
    auto parsed = parseSmd(smdText, srcPath);
    if (!parsed) return ResultVoid::fail(parsed.error());
    auto conv = smdToAsset(parsed.value(), stemOf(srcPath));
    if (!conv) return ResultVoid::fail(conv.error());
    return installConverted(std::move(conv.value().mesh), std::move(conv.value().skeleton),
                            std::move(conv.value().frames), srcPath, "Imported " + srcPath);
}

ResultVoid App::applyBridgedSmdText(const std::string& smdText, const std::string& origin,
                                    const std::string& srcPath) {
    auto parsed = parseSmd(smdText, srcPath);
    if (!parsed) return ResultVoid::fail(parsed.error());
    auto conv = smdToAsset(parsed.value(), stemOf(srcPath));
    if (!conv) return ResultVoid::fail(conv.error());
    Mesh mesh = std::move(conv.value().mesh);
    Skeleton skeleton = std::move(conv.value().skeleton);
    std::vector<SmdFrame> frames = std::move(conv.value().frames);
    // Same profiles as the sync path / CLI gr22smd (HARD RULE: convert at the
    // transform source, never a viewport hack). SMD origin stays identity.
    if (origin == "gr2") {
        const auto& profile = gr2ConversionProfile();
        const std::optional<CoordSys> detected = detectGr2CoordSys(srcPath);
        if (auto r = applyConversionProfile(profile, detected, mesh); !r)
            return ResultVoid::fail(r.error());
        if (auto r = applyConversionProfile(profile, detected, skeleton); !r)
            return ResultVoid::fail(r.error());
        if (auto r = applyConversionProfile(profile, detected, frames); !r)
            return ResultVoid::fail(r.error());
    } else if (origin == "noesis") {
        const auto& profile = noesisConversionProfile();
        const std::optional<CoordSys> detected;
        if (auto r = applyConversionProfile(profile, detected, mesh); !r)
            return ResultVoid::fail(r.error());
        if (auto r = applyConversionProfile(profile, detected, skeleton); !r)
            return ResultVoid::fail(r.error());
        if (auto r = applyConversionProfile(profile, detected, frames); !r)
            return ResultVoid::fail(r.error());
    }
    const std::string how =
        origin == "gr2" ? "Imported GR2 via grnreader98 " + srcPath
        : origin == "noesis"
            ? "Imported via Noesis bridge " + srcPath + " [canonical " + origin + "]"
            : "Imported " + srcPath;
    return installConverted(std::move(mesh), std::move(skeleton), std::move(frames), srcPath, how);
}

ResultVoid App::exportSmdFile(const std::string& path) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "EXPORT", "", "smd.export");
    RepairStats stats = repairMeshWeights(a->mesh, a->skeleton.bones.size());
    if (stats.verticesChanged > 0) {
        a->gpuDirty = true;
        a->dirty = true;
    }
    runValidation();
    if (report.exportBlocked()) {
        return ResultVoid::fail("Export blocked: " + report.summaryLine(), "EXPORT", a->id,
                                "smd.export");
    }
    auto written = writeSmd(a->mesh, a->skeleton, a->animFrames);
    if (!written) return ResultVoid::fail(written.error());
    if (auto w = writeTextFile(path, written.value().text, a->id); !w)
        return ResultVoid::fail(w.error());
    char buf[256];
    std::snprintf(buf, sizeof(buf), "Exported %s (%zu bytes, %zu tris, dropped mass %.6f).",
                  path.c_str(), written.value().text.size(),
                  written.value().stats.trianglesWritten, written.value().stats.droppedMass);
    setStatus(buf, "success");
    return ResultVoid::ok();
}

ResultVoid App::setCurrentFrame(std::size_t frameIndex) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "ANIMATION");
    if (!a->clip.keys.empty()) {
        if (!sampleClip(a->clip, a->skeleton, static_cast<double>(frameIndex)))
            return ResultVoid::fail("Clip sampling failed.", "ANIMATION", a->id);
        a->currentFrame = frameIndex;
        a->gpuDirty = true;  // deform preview + bone overlay follow the pose
        return ResultVoid::ok();
    }
    if (a->animFrames.empty())
        return ResultVoid::fail("Asset has no animation frames.", "ANIMATION", a->id);
    if (auto r = poseSkeletonFromFrame(a->skeleton, a->animFrames, frameIndex); !r)
        return ResultVoid::fail(r.error());
    a->currentFrame = frameIndex;
    a->gpuDirty = true;  // deform preview + bone overlay follow the pose

    return ResultVoid::ok();
}

std::size_t App::timelineFrameCount() const {
    const LoadedAsset* a = currentAsset();
    if (!a) return 0;
    std::size_t n = a->animFrames.size();
    if (!a->clip.keys.empty()) {
        const std::size_t k = static_cast<std::size_t>(a->clip.keys.back().frame) + 1;
        if (k > n) n = k;
    }
    return n;
}

ResultVoid App::addKeyframeHere() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "ANIMATION");
    if (a->skeleton.bones.empty())
        return ResultVoid::fail("Skeleton is empty.", "ANIMATION", a->id);
    addKey(a->clip, capturePoseKey(a->skeleton, static_cast<int>(a->currentFrame)));
    a->dirty = true;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Key added at frame %zu (%zu keys).", a->currentFrame,
                  a->clip.keys.size());
    setStatus(buf, "success");
    return ResultVoid::ok();
}

ResultVoid App::deleteKeyframeHere() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "ANIMATION");
    if (!removeKeyAt(a->clip, static_cast<int>(a->currentFrame)))
        return ResultVoid::fail("No keyframe at the current frame.", "ANIMATION", a->id);
    a->dirty = true;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Keyframe deleted (%zu keys left).", a->clip.keys.size());
    setStatus(buf, "success");
    return ResultVoid::ok();
}

ResultVoid App::clearClip() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "ANIMATION");
    if (a->clip.keys.empty())
        return ResultVoid::fail("Clip already empty.", "ANIMATION", a->id);
    a->clip.keys.clear();
    a->dirty = true;
    setStatus("Animation clip cleared; timeline uses imported frames again.", "success");
    return ResultVoid::ok();
}

ResultVoid App::bakeClipToFrames() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "ANIMATION");
    if (a->clip.keys.empty())
        return ResultVoid::fail("Clip is empty; add keyframes first.", "ANIMATION", a->id);
    const std::size_t replaced = a->animFrames.size();
    a->animFrames = bakeClipFrames(a->clip);
    a->currentFrame = 0;
    if (auto r = setCurrentFrame(0); !r) return ResultVoid::fail(r.error());
    a->dirty = true;
    runValidation();
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "Baked %zu keyframes to %zu frames (replaced %zu imported frames; "
                  "frame array is not covered by pose undo).",
                  a->clip.keys.size(), a->animFrames.size(), replaced);
    setStatus(buf, "success");
    return ResultVoid::ok();
}

// --- Weight paint / symmetry / transfer / bridges --------------------------

static BrushOp brushOpFor(BrushMode m) {
    switch (m) {
        case BrushMode::Add: return BrushOp::Add;
        case BrushMode::Subtract: return BrushOp::Subtract;
        case BrushMode::Smooth: return BrushOp::Smooth;
        case BrushMode::Normalize: return BrushOp::Normalize;
        case BrushMode::Blur: return BrushOp::Blur;
        case BrushMode::Sharpen: return BrushOp::Sharpen;
    }
    return BrushOp::Add;
}

void App::pushUndoSnapshot(const std::string& label) {
    if (!currentAsset()) return;
    undoStack.push_back(takeSnapshot(label));
    if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

App::InfluenceSnapshot App::takeSnapshot(const std::string& label) {
    InfluenceSnapshot snap;
    snap.label = label;
    if (LoadedAsset* a = currentAsset()) {
        snap.influences.reserve(a->mesh.vertices.size());
        for (const auto& v : a->mesh.vertices) snap.influences.push_back(v.influences);
        snap.bonePos.reserve(a->skeleton.bones.size());
        snap.boneRot.reserve(a->skeleton.bones.size());
        snap.boneScale.reserve(a->skeleton.bones.size());
        for (const auto& b : a->skeleton.bones) {
            snap.bonePos.push_back(b.localPosition);
            snap.boneRot.push_back(b.localRotationEuler);
            snap.boneScale.push_back(b.localScale);
        }
    }
    return snap;
}

void App::restoreSnapshot(InfluenceSnapshot& snap) {
    LoadedAsset* a = currentAsset();
    if (!a) return;
    for (std::size_t i = 0; i < a->mesh.vertices.size() && i < snap.influences.size(); ++i)
        a->mesh.vertices[i].influences = std::move(snap.influences[i]);
    if (snap.bonePos.size() == a->skeleton.bones.size() &&
        snap.boneRot.size() == a->skeleton.bones.size() &&
        snap.boneScale.size() == a->skeleton.bones.size()) {
        for (std::size_t i = 0; i < a->skeleton.bones.size(); ++i) {
            a->skeleton.bones[i].localPosition = snap.bonePos[i];
            a->skeleton.bones[i].localRotationEuler = snap.boneRot[i];
            a->skeleton.bones[i].localScale = snap.boneScale[i];
        }
        rebuildSkeletonRuntime(a->skeleton);
    }
    a->dirty = true;
    a->gpuDirty = true;
}

void App::undo() {
    LoadedAsset* a = currentAsset();
    if (!a || undoStack.empty()) {
        setStatus("Nothing to undo.", "info");
        return;
    }
    redoStack.push_back(takeSnapshot("redo"));
    InfluenceSnapshot snap = std::move(undoStack.back());
    undoStack.pop_back();
    restoreSnapshot(snap);
    noteWeightsChanged();
    runValidation();
    setStatus("Undo: " + snap.label, "success");
}

void App::redo() {
    LoadedAsset* a = currentAsset();
    if (!a || redoStack.empty()) {
        setStatus("Nothing to redo.", "info");
        return;
    }
    undoStack.push_back(takeSnapshot("undo"));
    InfluenceSnapshot snap = std::move(redoStack.back());
    redoStack.pop_back();
    restoreSnapshot(snap);
    noteWeightsChanged();
    runValidation();
    setStatus("Redo applied.", "success");
}

std::size_t App::paintStroke(const Vec3& center) {
    LoadedAsset* a = currentAsset();
    if (!a || selectedBone < 0) return 0;
    const auto bone = static_cast<std::uint32_t>(selectedBone);
    if (!a->skeleton.findById(bone)) {
        setStatus("Selected bone no longer exists - pick a bone of the current asset.", "warning");
        return 0;
    }
    if (isBoneLocked(bone)) {
        setStatus("Bone is locked - unlock to paint.", "warning");
        return 0;
    }
    pushUndoSnapshot("paint " + std::to_string(selectedBone));
    PaintParams params;
    params.bone = bone;
    params.center = center;
    params.radius = brushRadius;
    params.strength = brushStrength;
    params.falloff = paintFalloff;
    params.op = brushOpFor(brushMode);
    params.normalize = true;
    PaintStrokeStats stats = paintMeshStroke(a->mesh, params);
    bool mirrorSkipped = false;
    if (symmetryEnabled && stats.verticesAffected > 0) {
        Vec3 mc = center;
        if (symmetryAxis == SymmetryAxis::X) mc.x = -mc.x;
        if (symmetryAxis == SymmetryAxis::Y) mc.y = -mc.y;
        if (symmetryAxis == SymmetryAxis::Z) mc.z = -mc.z;
        std::uint32_t mirrorBone = params.bone;
        if (const SkeletonProfile* prof = findProfile(a->profileId); prof) {
            if (const Bone* b = a->skeleton.findById(params.bone)) {
                const std::string mn = mirrorBoneName(*prof, b->name);
                if (const Bone* mb = a->skeleton.findByName(mn)) mirrorBone = mb->id;
            }
        }
        if (mirrorBone != params.bone && !isBoneLocked(mirrorBone)) {
            PaintParams mp = params;
            mp.bone = mirrorBone;
            mp.center = mc;
            PaintStrokeStats ms = paintMeshStroke(a->mesh, mp);
            stats.verticesAffected += ms.verticesAffected;
            stats.droppedMass += ms.droppedMass;
        } else {
            mirrorSkipped = true;  // no profile pair or mirror target locked
        }
    }
    if (stats.verticesAffected == 0) {
        // Nothing changed: drop the pre-pushed snapshot instead of burning an
        // undo slot and lying about a successful stroke.
        if (!undoStack.empty()) undoStack.pop_back();
        setStatus("Paint: no vertices inside the brush radius.", "info");
        return 0;
    }
    noteWeightsChanged();
    gPaintState.lastStats.verticesAffected = stats.verticesAffected;
    gPaintState.lastStats.totalStrokes += 1;
    gPaintState.lastStats.totalStrengthApplied += stats.totalStrengthApplied;
    a->dirty = true;
    a->gpuDirty = true;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "Paint %s: %zu verts, dropped mass %.4f%s.",
                  brushModeName(brushMode), stats.verticesAffected, stats.droppedMass,
                  mirrorSkipped ? " (mirror pass skipped)" : "");
    setStatus(buf, "success");
    return stats.verticesAffected;
}

ResultVoid App::mirrorWeights() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "SYMMETRY");
    const int axis = static_cast<int>(symmetryAxis);
    auto pairs = findMirrorPairs(a->mesh, axis);
    if (pairs.empty()) return ResultVoid::fail("No mirror vertex pairs found.", "SYMMETRY", a->id);
    pushUndoSnapshot("mirror");
    std::vector<std::uint32_t> boneMirror(a->skeleton.bones.size(), kInvalidBone);
    if (const SkeletonProfile* prof = findProfile(a->profileId); prof) {
        for (const auto& b : a->skeleton.bones) {
            const std::string mn = mirrorBoneName(*prof, b.name);
            if (const Bone* mb = a->skeleton.findByName(mn))
                boneMirror[b.id] = mb->id;
            else
                boneMirror[b.id] = b.id;
        }
    } else {
        for (const auto& b : a->skeleton.bones) boneMirror[b.id] = b.id;
    }
    const std::size_t n = mirrorMeshWeights(a->mesh, axis, pairs, boneMirror);
    // Locked bones keep their pre-mirror weights (guard, not undoable).
    if (!lockedBones.empty()) {
        // Rebuild locked weights from the undo snapshot we just pushed.
        const InfluenceSnapshot& snap = undoStack.back();
        for (std::size_t i = 0; i < a->mesh.vertices.size() && i < snap.influences.size(); ++i) {
            auto& infs = a->mesh.vertices[i].influences;
            for (const auto& old : snap.influences[i]) {
                if (!isBoneLocked(old.bone)) continue;
                bool found = false;
                for (auto& cur : infs) {
                    if (cur.bone == old.bone) {
                        cur.weight = old.weight;
                        found = true;
                        break;
                    }
                }
                if (!found) infs.push_back(old);
            }
            // Drop any newly-added locked-bone entries not in the snapshot.
            for (auto it = infs.begin(); it != infs.end();) {
                if (!isBoneLocked(it->bone)) {
                    ++it;
                    continue;
                }
                bool inSnap = false;
                for (const auto& old : snap.influences[i]) {
                    if (old.bone == it->bone) {
                        inSnap = true;
                        break;
                    }
                }
                if (inSnap)
                    ++it;
                else
                    it = infs.erase(it);
            }
            normalizeInfluences(infs);
        }
    }
    RepairStats rs = repairMeshWeights(a->mesh, a->skeleton.bones.size());
    (void)rs;
    noteWeightsChanged();
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus("Mirrored " + std::to_string(n) + " vertex pairs.", "success");
    return ResultVoid::ok();
}

ResultVoid App::transferWeightsFrom(const std::string& sourceAssetId) {
    LoadedAsset* dst = currentAsset();
    if (!dst) return ResultVoid::fail("No asset loaded.", "TRANSFER");
    auto it = assets.find(sourceAssetId);
    if (it == assets.end())
        return ResultVoid::fail("Source asset not found: " + sourceAssetId + ".", "TRANSFER");
    const LoadedAsset& src = it->second;
    if (src.mesh.vertices.empty())
        return ResultVoid::fail("Source mesh is empty.", "TRANSFER", src.id);
    std::vector<std::uint32_t> remap(src.skeleton.bones.size(), kInvalidBone);
    for (const auto& sb : src.skeleton.bones) {
        if (const Bone* db = dst->skeleton.findByName(sb.name)) {
            if (!isBoneLocked(db->id)) remap[sb.id] = db->id;
        }
    }
    pushUndoSnapshot("transfer from " + src.id);
    WeightTransferStats stats = transferWeightsKDTree(src.mesh, dst->mesh, remap, 3);
    noteWeightsChanged();
    if (!lockedBones.empty()) {
        // Locked destination bones keep their pre-transfer weights.
        const InfluenceSnapshot& snap = undoStack.back();
        for (std::size_t i = 0; i < dst->mesh.vertices.size() && i < snap.influences.size(); ++i) {
            auto& infs = dst->mesh.vertices[i].influences;
            for (const auto& old : snap.influences[i]) {
                if (!isBoneLocked(old.bone)) continue;
                bool found = false;
                for (auto& cur : infs) {
                    if (cur.bone == old.bone) {
                        cur.weight = old.weight;
                        found = true;
                        break;
                    }
                }
                if (!found) infs.push_back(old);
            }
            RepairStats lrs;
            repairVertexInfluences(infs, kMetin2MaxInfluences, &lrs);
            (void)lrs;
        }
    }
    dst->dirty = true;
    dst->gpuDirty = true;
    runValidation();
    setStatus(stats.toDisplayString(), "success");
    return ResultVoid::ok();
}

ResultVoid App::transferWeightsSelfTrained(const std::string& sourceAssetId) {
    LoadedAsset* dst = currentAsset();
    if (!dst) return ResultVoid::fail("No asset loaded.", "TRANSFER");
    auto it = assets.find(sourceAssetId);
    if (it == assets.end())
        return ResultVoid::fail("Source asset not found: " + sourceAssetId + ".", "TRANSFER");
    const SkeletonProfile* prof = findProfile(dst->profileId);
    if (!prof) return ResultVoid::fail("Unknown skeleton profile.", "TRANSFER", dst->id);
    const LoadedAsset& src = it->second;
    if (src.mesh.vertices.empty())
        return ResultVoid::fail("Source mesh is empty.", "TRANSFER", src.id);
    pushUndoSnapshot("self-train from " + src.id);
    SelfTrainStats stats = transferWeightsSelfTraining(src.mesh, src.skeleton, dst->mesh,
                                                        dst->skeleton, *prof, 3, 12, &lockedBones);
    noteWeightsChanged();
    if (!lockedBones.empty()) {
        // Locked destination bones keep their pre-transfer weights; the
        // self-trainer may still have used them as donors via remap.
        const InfluenceSnapshot& snap = undoStack.back();
        for (std::size_t i = 0; i < dst->mesh.vertices.size() && i < snap.influences.size(); ++i) {
            auto& infs = dst->mesh.vertices[i].influences;
            for (const auto& old : snap.influences[i]) {
                if (!isBoneLocked(old.bone)) continue;
                bool found = false;
                for (auto& cur : infs) {
                    if (cur.bone == old.bone) {
                        cur.weight = old.weight;
                        found = true;
                        break;
                    }
                }
                if (!found) infs.push_back(old);
            }
            RepairStats lrs;
            repairVertexInfluences(infs, kMetin2MaxInfluences, &lrs);
            (void)lrs;
        }
    }
    dst->dirty = true;
    dst->gpuDirty = true;
    runValidation();
    setStatus(stats.toDisplayString(), "success");
    return ResultVoid::ok();
}

ResultVoid App::floodSelectedBone() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "PAINT");
    if (selectedBone < 0) return ResultVoid::fail("No bone selected.", "PAINT", a->id);
    const auto bone = static_cast<std::uint32_t>(selectedBone);
    if (bone >= a->skeleton.bones.size())
        return ResultVoid::fail("Selected bone is out of range.", "PAINT", a->id);
    if (isBoneLocked(bone)) {
        return ResultVoid::fail("Bone is locked - unlock to flood.", "PAINT", a->id);
    }
    if (a->mesh.vertices.empty()) return ResultVoid::fail("Mesh is empty.", "PAINT", a->id);
    pushUndoSnapshot("flood " + a->skeleton.bones[bone].name);
    RepairStats stats;
    const std::size_t n = floodBone(a->mesh, bone, kMetin2MaxInfluences, &stats);
    noteWeightsChanged();
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    char buf[192];
    std::snprintf(buf, sizeof(buf), "Flood %s: %zu verts, dropped mass %.4f.",
                  a->skeleton.bones[bone].name.c_str(), n, stats.removedMass);
    setStatus(buf, "success");
    return ResultVoid::ok();
}

ResultVoid App::pruneSelectedBone() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "PAINT");
    if (selectedBone < 0) return ResultVoid::fail("No bone selected.", "PAINT", a->id);
    const auto bone = static_cast<std::uint32_t>(selectedBone);
    if (bone >= a->skeleton.bones.size())
        return ResultVoid::fail("Selected bone is out of range.", "PAINT", a->id);
    if (isBoneLocked(bone)) {
        return ResultVoid::fail("Bone is locked - unlock to prune.", "PAINT", a->id);
    }
    pushUndoSnapshot("prune " + a->skeleton.bones[bone].name);
    RepairStats stats;
    const std::size_t n = pruneBone(a->mesh, bone, kMetin2MaxInfluences, &stats);
    noteWeightsChanged();
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    char buf[192];
    std::snprintf(buf, sizeof(buf), "Prune %s: %zu verts carried it, dropped mass %.4f.",
                  a->skeleton.bones[bone].name.c_str(), n, stats.removedMass);
    setStatus(buf, "success");
    return ResultVoid::ok();
}

ResultVoid App::setVertexInfluenceWeight(std::size_t vertex, std::size_t slot, float weight) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "PAINT");
    if (vertex >= a->mesh.vertices.size())
        return ResultVoid::fail("Vertex index out of range.", "PAINT", a->id);
    auto& infs = a->mesh.vertices[vertex].influences;
    if (slot >= infs.size()) return ResultVoid::fail("Influence slot out of range.", "PAINT", a->id);
    if (!isFiniteF(weight) || weight < 0.0f || weight > 1.0f)
        return ResultVoid::fail("Weight must be finite 0..1.", "PAINT", a->id);
    if (isBoneLocked(infs[slot].bone))
        return ResultVoid::fail("Influence bone is locked - unlock to edit.", "PAINT", a->id);
    pushUndoSnapshot("edit vertex weight");
    infs[slot].weight = weight;
    RepairStats stats;
    repairVertexInfluences(infs, kMetin2MaxInfluences, &stats);
    noteWeightsChanged();
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Vertex %zu: weight set, dropped mass %.4f.", vertex,
                  stats.removedMass);
    setStatus(buf, "success");
    return ResultVoid::ok();
}

ResultVoid App::removeVertexInfluence(std::size_t vertex, std::size_t slot) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "PAINT");
    if (vertex >= a->mesh.vertices.size())
        return ResultVoid::fail("Vertex index out of range.", "PAINT", a->id);
    auto& infs = a->mesh.vertices[vertex].influences;
    if (slot >= infs.size()) return ResultVoid::fail("Influence slot out of range.", "PAINT", a->id);
    if (infs.size() <= 1)
        return ResultVoid::fail("Cannot remove the last influence.", "PAINT", a->id);
    if (isBoneLocked(infs[slot].bone))
        return ResultVoid::fail("Influence bone is locked - unlock to edit.", "PAINT", a->id);
    pushUndoSnapshot("remove vertex influence");
    infs.erase(infs.begin() + static_cast<std::ptrdiff_t>(slot));
    RepairStats stats;
    repairVertexInfluences(infs, kMetin2MaxInfluences, &stats);
    noteWeightsChanged();
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus("Vertex influence removed.", "success");
    return ResultVoid::ok();
}

ResultVoid App::normalizeVertexWeights(std::size_t vertex) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "PAINT");
    if (vertex >= a->mesh.vertices.size())
        return ResultVoid::fail("Vertex index out of range.", "PAINT", a->id);
    for (const auto& inf : a->mesh.vertices[vertex].influences) {
        if (isBoneLocked(inf.bone))
            return ResultVoid::fail("Vertex carries a locked bone - unlock to normalize.", "PAINT",
                                    a->id);
    }
    pushUndoSnapshot("normalize vertex weights");
    RepairStats stats;
    repairVertexInfluences(a->mesh.vertices[vertex].influences, kMetin2MaxInfluences, &stats);
    noteWeightsChanged();
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus("Vertex weights normalized.", "success");
    return ResultVoid::ok();
}

ResultVoid App::autoRigFromSkeleton() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "PAINT");
    if (a->mesh.vertices.empty()) return ResultVoid::fail("Mesh is empty.", "PAINT", a->id);
    if (a->skeleton.bones.empty())
        return ResultVoid::fail("Skeleton is empty.", "PAINT", a->id);
    pushUndoSnapshot("auto-rig");
    RepairStats stats;
    AutoRigStats rs = autoRigMesh(a->mesh, a->skeleton, 0.02f, kMetin2MaxInfluences, &stats);
    noteWeightsChanged();
    if (!lockedBones.empty()) {
        // Locked bones keep their pre-rig weights (same guard as transfer).
        const InfluenceSnapshot& snap = undoStack.back();
        for (std::size_t i = 0; i < a->mesh.vertices.size() && i < snap.influences.size(); ++i) {
            auto& infs = a->mesh.vertices[i].influences;
            for (const auto& old : snap.influences[i]) {
                if (!isBoneLocked(old.bone)) continue;
                bool found = false;
                for (auto& cur : infs) {
                    if (cur.bone == old.bone) {
                        cur.weight = old.weight;
                        found = true;
                        break;
                    }
                }
                if (!found) infs.push_back(old);
            }
            RepairStats lrs;
            repairVertexInfluences(infs, kMetin2MaxInfluences, &lrs);
            (void)lrs;
        }
    }
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus(rs.toDisplayString(), "success");
    return ResultVoid::ok();
}

ResultVoid App::exportMsmFile(const std::string& path) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "EXPORT", "", "msm.export");
    RepairStats rs = repairMeshWeights(a->mesh, a->skeleton.bones.size());
    (void)rs;
    runValidation();
    if (report.exportBlocked())
        return ResultVoid::fail("Export blocked: " + report.summaryLine(), "EXPORT", a->id,
                                "msm.export");
    std::ostringstream out;
    out << buildMsmExport(a->mesh, a->skeleton, a->id);
    if (auto w = writeTextFile(path, out.str(), a->id); !w) return ResultVoid::fail(w.error());
    setStatus("Exported MSM " + path + ".", "success");
    return ResultVoid::ok();
}

ResultVoid App::exportGr2Bridge(const std::string& path) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "EXPORT", "", "gr2.export");
    RepairStats rs = repairMeshWeights(a->mesh, a->skeleton.bones.size());
    (void)rs;
    runValidation();
    if (report.exportBlocked())
        return ResultVoid::fail("Export blocked: " + report.summaryLine(), "EXPORT", a->id,
                                "gr2.export");
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    std::string err;
    setStatus("Running GR2 bridge (external tool, up to 30s)...", "info");
    const Gr2ExportStatus st = exportGr2ViaBridge(a->mesh, a->skeleton, path, cfg, err);
    if (st == Gr2ExportStatus::Success) {
        setStatus("Exported GR2 via bridge: " + path, "success");
        return ResultVoid::ok();
    }
    if (st == Gr2ExportStatus::NotSupportedDirectly) {
        const std::string msg =
            "GR2 native emit is NOT supported directly. Configure Noesis/granny_compiler bridge. " +
            err;
        setStatus(msg, "warning");
        return ResultVoid::fail(msg, "EXPORT", a->id, "gr2.export");
    }
    setStatus("GR2 bridge failed: " + err, "error");
    return ResultVoid::fail(err.empty() ? std::string("GR2 bridge failed.") : err, "EXPORT", a->id,
                            "gr2.export");
}

ResultVoid App::exportFbxFile(const std::string& path) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "EXPORT", "", "fbx.export");
    RepairStats rs = repairMeshWeights(a->mesh, a->skeleton.bones.size());
    (void)rs;
    runValidation();
    if (report.exportBlocked())
        return ResultVoid::fail("Export blocked: " + report.summaryLine(), "EXPORT", a->id,
                                "fbx.export");

    // Export to temporary SMD first, then convert via Noesis with -rotate 90 0 0
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    if (!std::filesystem::exists(cfg.noesisCliPath)) {
        return ResultVoid::fail(
            "Noesis bridge not configured (missing noesis/Noesis.exe). "
            "FBX export requires Noesis for conversion from SMD.",
            "EXPORT", a->id, "fbx.export");
    }

    // Write SMD to temp file (unique per call: concurrent FBX exports must
    // never share one tmp name).
    std::error_code ec;
    static std::atomic<unsigned> fbxExportCounter{0};
    const std::filesystem::path tmpSmd = std::filesystem::temp_directory_path() /
        ("m2rig_fbx_export_" + std::to_string(::GetCurrentProcessId()) + "_" +
         std::to_string(fbxExportCounter.fetch_add(1)) + ".smd");
    auto written = writeSmd(a->mesh, a->skeleton, a->animFrames);
    if (!written) return ResultVoid::fail(written.error());
    {
        std::ofstream out(tmpSmd, std::ios::out | std::ios::binary);
        if (!out.is_open())
            return ResultVoid::fail("Cannot write temporary SMD: " + tmpSmd.string(), "IO");
        out << written.value().text;
    }

    // Noesis command: ?cmode input.smd output.fbx -rotate 90 0 0
    // -rotate 90 0 0 fixes the coordinate system (Z-up to Y-up) for Metin2
    std::wstring cmd = L"\"" + cfg.noesisCliPath.wstring() + L"\" ?cmode \"" +
                       tmpSmd.wstring() + L"\" \"" +
                       std::filesystem::path(path).wstring() + L"\" -rotate 90 0 0";
    if (!cfg.noesisArgs.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, cfg.noesisArgs.c_str(), -1, nullptr, 0);
        if (wlen > 1) {
            std::wstring wargs(static_cast<std::size_t>(wlen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, cfg.noesisArgs.c_str(), -1, wargs.data(), wlen);
            cmd += L" " + wargs;
        }
    }

    setStatus("Converting to FBX via Noesis (rotate 90 0 0)...", "info");
    const BridgeResult br = runBridgeLogged(cmd, cfg.timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);

    // Cleanup temp SMD
    std::filesystem::remove(tmpSmd, ec);

    if (!br.started) {
        return ResultVoid::fail("Failed to start Noesis process.", "EXPORT", a->id, "fbx.export");
    }
    if (br.timedOut) {
        return ResultVoid::fail("Noesis timed out. Last output: " + lastLine, "EXPORT", a->id, "fbx.export");
    }
    if (br.exitCode != 0) {
        return ResultVoid::fail(
            "Noesis exited with code " + std::to_string(br.exitCode) +
                ". Last output: " + lastLine,
            "EXPORT", a->id, "fbx.export");
    }
    if (!std::filesystem::exists(path)) {
        return ResultVoid::fail(
            "Noesis produced no FBX output. Last output: " + lastLine,
            "EXPORT", a->id, "fbx.export");
    }

    setStatus("Exported FBX via Noesis (rotate 90 0 0): " + path, "success");
    return ResultVoid::ok();
}

ResultVoid App::exportAniFile(const std::string& path) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "EXPORT", "", "ani.export");
    if (a->clip.keys.empty()) {
        return ResultVoid::fail("No animation keys to export. Add keyframes first.", "EXPORT", a->id, "ani.export");
    }
    runValidation();
    if (report.exportBlocked())
        return ResultVoid::fail("Export blocked: " + report.summaryLine(), "EXPORT", a->id,
                                "ani.export");
    
    float aniFps = timelineFps > 0.0f ? timelineFps : 30.0f;
    auto result = writeAniFile(path, a->clip, a->skeleton, aniFps);
    if (!result) {
        return ResultVoid::fail(result.error().message, "EXPORT", a->id, "ani.export");
    }
    setStatus("Exported ANI: " + path, "success");
    return ResultVoid::ok();
}

static std::string lowerExt(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

ResultVoid App::importBridgedFile(const std::string& path) {
    const std::string ext = lowerExt(path);
    if (ext == ".smd") return importSmdFile(path);
#ifdef M2RIG_WITH_OPENFBX
    if (ext == ".fbx") {
        // Primary path: native OpenFBX reader (no subprocess).
        if (auto conv = readFbxFile(path, stemOf(path)); conv) {
            // Surface the axis arbitration record (declared vs applied space,
            // fallback flag) so a liar-header import is visible, not silent.
            std::string how = "Imported FBX natively " + path;
            if (!conv.value().conversionNote.empty()) how += " [" + conv.value().conversionNote + "]";
            return installConverted(std::move(conv.value().mesh), std::move(conv.value().skeleton),
                                    {}, path, how);
        } else {
            setStatus("Native FBX failed (" + conv.error().message + "); trying Noesis.",
                      "warning");
        }
    }
#endif
    if (ext == ".gr2") {
        // Primary path: grnreader98 (documented batch tool, ships its own
        // granny2.dll). Fallback: Noesis ?cmode (needs GR2 plugins).
        setStatus("Running grnreader98 bridge (external tool, up to 60s)...", "info");
        if (auto smd = convertGr2ToSmdViaGrnReader(path); smd) {
            auto parsed = parseSmd(smd.value(), path);
            if (!parsed) return ResultVoid::fail(parsed.error());
            auto conv = smdToAsset(parsed.value(), stemOf(path));
            if (!conv) return ResultVoid::fail(conv.error());
            // Canonical conversion via the GR2 profile (single source of
            // truth in coordsys.cpp — no inline matrix copies). Frames are
            // converted too: they share the source space, and posing frame 0
            // must reproduce the converted bind pose.
            const auto& gr2Profile = gr2ConversionProfile();
            const std::optional<CoordSys> detected = detectGr2CoordSys(path);
            if (auto r = applyConversionProfile(gr2Profile, detected, conv.value().mesh); !r)
                return ResultVoid::fail(r.error());
            if (auto r = applyConversionProfile(gr2Profile, detected, conv.value().skeleton); !r)
                return ResultVoid::fail(r.error());
            if (auto r = applyConversionProfile(gr2Profile, detected, conv.value().frames); !r)
                return ResultVoid::fail(r.error());
            return installConverted(std::move(conv.value().mesh), std::move(conv.value().skeleton),
                                    std::move(conv.value().frames), path,
                                    "Imported GR2 via grnreader98 " + path);
        } else {
            setStatus("grnreader98 failed (" + smd.error().message + "); trying Noesis.",
                      "warning");
        }
    }
    Gr2BridgeConfig cfg = defaultGr2BridgeConfig();
    if (!std::filesystem::exists(cfg.noesisCliPath)) {
        return ResultVoid::fail(
            "Noesis bridge not configured (missing noesis/Noesis.exe). "
            "GR2 was already attempted via grnreader98 - see status log - "
            "or import SMD directly.",
            "IMPORT", "", "bridge.import");
    }
    // Unique temp output per call (PID + atomic counter, same recipe as the
    // grnreader98 staging in gr2_adapter.cpp): the old single shared
    // m2rig_bridge_import.smd raced concurrent imports and could install a
    // STALE file left by a previous run when the bridge produced nothing.
    static std::atomic<unsigned> bridgeCounter{0};
    const std::string tmp =
        (std::filesystem::temp_directory_path() /
         ("m2rig_bridge_import_" + stemOf(path) + "_" +
          std::to_string(::GetCurrentProcessId()) + "_" +
          std::to_string(bridgeCounter.fetch_add(1)) + ".smd"))
            .string();
    setStatus("Running Noesis bridge (external tool, up to 30s)...", "info");
    std::wstring cmd = L"\"" + cfg.noesisCliPath.wstring() + L"\" ?cmode \"" +
                       std::filesystem::path(path).wstring() + L"\" \"" +
                       std::filesystem::path(tmp).wstring() + L"\"";
    const BridgeResult br = runBridgeLogged(cmd, cfg.timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);
    if (!br.started) {
        // Best-effort: never leave the tmp behind on a failed start.
        std::error_code startEc;
        std::filesystem::remove(tmp, startEc);
        return ResultVoid::fail("Failed to start Noesis bridge process.", "IMPORT", "", "bridge.import");
    }
    if (br.timedOut) {
        // Best-effort: the terminated bridge may have left a partial file.
        std::error_code timeoutEc;
        std::filesystem::remove(tmp, timeoutEc);
        return ResultVoid::fail("Noesis bridge timed out. Last output: " + lastLine, "IMPORT", "",
                                "bridge.import");
    }
    if (br.exitCode != 0) {
        // Fail closed: a failing bridge that leaves a file behind must NOT
        // be installed (same pattern as exportFbxFile).
        std::error_code exitEc;
        std::filesystem::remove(tmp, exitEc);
        return ResultVoid::fail("Noesis exited with code " + std::to_string(br.exitCode) +
                                    ". Last output: " + lastLine,
                                "IMPORT", "", "bridge.import");
    }
    if (!std::filesystem::exists(tmp))
        return ResultVoid::fail("Bridge produced no SMD output. Last output: " + lastLine,
                                "IMPORT", "", "bridge.import");
    // Install under the ORIGINAL source path: id/sourcePath/status then name
    // the real model instead of the deleted temp file (unique tmp per call,
    // so concurrent/repeat imports never collide on one shared name).
    auto text = readTextFile(tmp, path);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    if (!text) {
        return ResultVoid::fail("Cannot read Noesis SMD output: " + text.error().message,
                                "IMPORT", path, "bridge.import");
    }
    // Noesis SMD shares the Z-up source space: convert via the Noesis profile
    // (same as the async path) instead of installing raw.
    return applyBridgedSmdText(text.value(), "noesis", path);
}

ResultVoid App::importGltfFile(const std::string& path) {
#ifdef M2RIG_WITH_CGLTF
    // Native glTF path (no subprocess): the CLI gltf2smd chain
    // (readGltfFile -> repair -> validate -> export gate) with the asset
    // installed via the same installConverted helper the FBX path uses.
    // Every failure returns before installConverted, so the live session
    // (assets/current/undo/locks/hidden) is never clobbered by a bad file.
    const std::string stem = stemOf(path);
    auto conv = readGltfFile(path, stem);
    if (!conv) {
        setStatus("glTF import failed: " + conv.error().message, "error");
        return ResultVoid::fail(conv.error());
    }
    ConvertedGltf gltf = std::move(conv.value());
    RepairStats repair = repairMeshWeights(gltf.mesh, gltf.skeleton.bones.size());
    (void)repair;
    ValidationReport gate;
    validateMeshStructure(gltf.mesh, stem, gate);
    validateSkeleton(gltf.skeleton, stem, gate);
    validateMeshWeights(gltf.mesh, gltf.skeleton.bones.size(), stem, gate);
    if (gate.exportBlocked()) {
        setStatus("glTF import blocked: " + gate.summaryLine(), "error");
        return ResultVoid::fail("Export blocked: " + gate.summaryLine(), "IMPORT", stem,
                                "gltf.import");
    }
    // conversionNote carries the Y-up record plus the warn-only orient gate
    // verdict ("Orient: ...", gltf_reader.cpp), so it surfaces in the status
    // exactly like the FBX arbitration note does.
    std::string how = "Imported glTF " + path;
    if (!gltf.conversionNote.empty()) how += " [" + gltf.conversionNote + "]";
    return installConverted(std::move(gltf.mesh), std::move(gltf.skeleton),
                            std::move(gltf.frames), path, how);
#else
    // Same explicit-fail style as the other gated paths: no silent fallback.
    (void)path;
    return ResultVoid::fail(
        "glTF import requires a M2RIG_WITH_CGLTF build (native cgltf reader unavailable).",
        "IMPORT", "", "gltf.import");
#endif
}

bool App::startBridgedImport(const std::string& path, double nowSeconds) {
    if (bridgeBusy) return false;
    const std::string ext = lowerExt(path);
    if (ext == ".smd") {
        if (auto r = importSmdFile(path); !r)
            setStatus("SMD import failed: " + r.error().message, "error");
        return true;
    }
#ifdef M2RIG_WITH_OPENFBX
    // Native FBX is subprocess-free: try it synchronously first so the async
    // worker only handles the Noesis fallback (parity with importBridgedFile).
    if (ext == ".fbx") {
        if (auto conv = readFbxFile(path, stemOf(path)); conv) {
            std::string how = "Imported FBX natively " + path;
            if (!conv.value().conversionNote.empty()) how += " [" + conv.value().conversionNote + "]";
            if (auto r = installConverted(std::move(conv.value().mesh),
                                          std::move(conv.value().skeleton), {}, path, how);
                !r)
                setStatus("FBX import failed: " + r.error().message, "error");
            return true;
        }
        setStatus("Native FBX failed; trying Noesis in the background...", "warning");
    }
#endif
    bridgeJob.label = ext == ".gr2" ? "GR2 bridge" : "Noesis bridge";
    bridgeJob.sourcePath = path;
    bridgeJob.sourceExt = ext;
    bridgeJob.startTime = nowSeconds;
    bridgeFuture = std::async(std::launch::async, [path, ext] {
        return runBridgeChain(path, ext);
    });
    bridgeBusy = true;
    setStatus("Running " + bridgeJob.label + " in the background...", "info");
    return true;
}

bool App::pollBridgeImport() {
    if (!bridgeBusy) return false;
    if (bridgeFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return false;
    auto res = bridgeFuture.get();
    bridgeBusy = false;
    if (!res) {
        setStatus("Bridge import failed: " + res.error().message, "error");
        return true;
    }
    // UI-thread canonical conversion (parity with sync importBridgedFile).
    if (auto r = applyBridgedSmdText(res.value().smdText, res.value().origin,
                                     bridgeJob.sourcePath);
        !r)
        setStatus("Bridge import failed: " + r.error().message, "error");
    return true;
}

ResultVoid App::saveWorkspaceFile(const std::string& path) {
    if (auto r = saveCurrentWorkspace(*this, path); !r) return ResultVoid::fail(r.error());
    setStatus("Workspace saved: " + path, "success");
    return ResultVoid::ok();
}

ResultVoid App::loadWorkspaceFile(const std::string& path) {
    if (auto r = restoreWorkspace(*this, path); !r) {
        setStatus("Workspace load failed: " + r.error().message, "error");
        return ResultVoid::fail(r.error());
    }
    runValidation();
    return ResultVoid::ok();  // status already set by restoreWorkspace
}

ResultVoid App::openMsmInspector(const std::string& path) {
    auto doc = readMsmFile(path);
    if (!doc) {
        msmDoc.reset();
        msmPath.clear();
        msmReport.clear();
        return ResultVoid::fail(doc.error());
    }
    msmDoc = std::move(doc.value());
    msmPath = path;
    msmReport.clear();
    validateMsmDoc(msmDoc.value(), std::filesystem::path(path).filename().string(), msmReport);
    setStatus("MSM inspected: " + msmReport.summaryLine(),
              msmReport.exportBlocked() ? "error" : "success");
    return ResultVoid::ok();
}

void App::closeMsmInspector() {
    msmDoc.reset();
    msmPath.clear();
    msmReport.clear();
}

void App::tickAutosave(const std::filesystem::path& projectsDir, double nowSeconds) {
    if (autosaveMinutes <= 0) return;
    bool anyDirty = false;
    for (const auto& [id, a] : assets) {
        if (a.dirty) {
            anyDirty = true;
            break;
        }
    }
    if (!anyDirty) return;
    if (nowSeconds - lastAutosaveTime < static_cast<double>(autosaveMinutes) * 60.0) return;
    // Tolerant path: projects dir may not exist in tests.
    std::error_code ec;
    std::filesystem::create_directories(projectsDir, ec);
    if (ec) return;
    const std::filesystem::path tmp = projectsDir / "autosave.m2rig.tmp";
    const std::filesystem::path dst = projectsDir / "autosave.m2rig";
    if (auto r = saveCurrentWorkspace(*this, tmp); !r) return;
    std::filesystem::rename(tmp, dst, ec);
    if (ec) return;
    lastAutosaveTime = nowSeconds;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Autosaved %.0fs.", nowSeconds);
    lastAutosaveInfo = buf;
    setStatus("Autosaved workspace.", "success");
}

ResultVoid App::saveAutosaveNow(const std::filesystem::path& projectsDir) {
    std::error_code ec;
    std::filesystem::create_directories(projectsDir, ec);
    if (ec)
        return ResultVoid::fail("Cannot create projects dir: " + ec.message(), "IO");
    const std::filesystem::path tmp = projectsDir / "autosave.m2rig.tmp";
    const std::filesystem::path dst = projectsDir / "autosave.m2rig";
    if (auto r = saveCurrentWorkspace(*this, tmp); !r) return ResultVoid::fail(r.error());
    std::filesystem::rename(tmp, dst, ec);
    if (ec) return ResultVoid::fail("Cannot finalize autosave: " + ec.message(), "IO");
    lastAutosaveInfo = "manual save";
    setStatus("Autosaved workspace.", "success");
    return ResultVoid::ok();
}

ResultVoid App::savePreferences(const std::filesystem::path& configDir) {
    std::error_code ec;
    std::filesystem::create_directories(configDir, ec);
    if (ec) return ResultVoid::fail("Cannot create config dir: " + ec.message(), "IO");
    
    const std::filesystem::path path = configDir / "user_prefs.json";
    std::ofstream out(path);
    if (!out.is_open()) {
        return ResultVoid::fail("Cannot open preferences file for writing.", "IO");
    }
    
    out << "{\n";
    out << "  \"camera\": {\n";
    out << "    \"fovY\": " << camera.fovY << ",\n";
    out << "    \"nearZ\": " << camera.nearZ << ",\n";
    out << "    \"farZ\": " << camera.farZ << ",\n";
    out << "    \"orthographic\": " << (camera.orthographic ? "true" : "false") << ",\n";
    out << "    \"orthoHeight\": " << camera.orthoHeight << ",\n";
    out << "    \"orbitSensitivity\": " << camera.orbitSensitivity << ",\n";
    out << "    \"panSensitivity\": " << camera.panSensitivity << ",\n";
    out << "    \"zoomSensitivity\": " << camera.zoomSensitivity << "\n";
    out << "  },\n";
    out << "  \"viewport\": {\n";
    out << "    \"showGrid\": " << (showGrid ? "true" : "false") << ",\n";
    out << "    \"showBones\": " << (showBones ? "true" : "false") << ",\n";
    out << "    \"xrayBones\": " << (xrayBones ? "true" : "false") << ",\n";
    out << "    \"showWireOverlay\": " << (showWireOverlay ? "true" : "false") << ",\n";
    out << "    \"textured\": " << (textured ? "true" : "false") << ",\n";
    out << "    \"previewDeform\": " << (previewDeform ? "true" : "false") << ",\n";
    out << "    \"useDqs\": " << (useDqs ? "true" : "false") << ",\n";
    out << "    \"viewMode\": " << static_cast<int>(viewMode) << "\n";
    out << "  },\n";
    out << "  \"boneVisualization\": {\n";
    out << "    \"boneThickness\": " << boneThickness << ",\n";
    out << "    \"boneJointSize\": " << boneJointSize << ",\n";
    out << "    \"boneShowLabels\": " << (boneShowLabels ? "true" : "false") << ",\n";
    out << "    \"boneShowJointCrosses\": " << (boneShowJointCrosses ? "true" : "false") << ",\n";
    out << "    \"boneSelectedColor\": [" << boneSelectedColor[0] << ", " << boneSelectedColor[1] << ", " << boneSelectedColor[2] << ", " << boneSelectedColor[3] << "],\n";
    out << "    \"boneHoveredColor\": [" << boneHoveredColor[0] << ", " << boneHoveredColor[1] << ", " << boneHoveredColor[2] << ", " << boneHoveredColor[3] << "],\n";
    out << "    \"boneLockedColor\": [" << boneLockedColor[0] << ", " << boneLockedColor[1] << ", " << boneLockedColor[2] << ", " << boneLockedColor[3] << "],\n";
    out << "    \"boneParentColor\": [" << boneParentColor[0] << ", " << boneParentColor[1] << ", " << boneParentColor[2] << ", " << boneParentColor[3] << "],\n";
    out << "    \"boneChildColor\": [" << boneChildColor[0] << ", " << boneChildColor[1] << ", " << boneChildColor[2] << ", " << boneChildColor[3] << "],\n";
    out << "    \"boneDefaultColor\": [" << boneDefaultColor[0] << ", " << boneDefaultColor[1] << ", " << boneDefaultColor[2] << ", " << boneDefaultColor[3] << "],\n";
    out << "    \"boneHiddenColor\": [" << boneHiddenColor[0] << ", " << boneHiddenColor[1] << ", " << boneHiddenColor[2] << ", " << boneHiddenColor[3] << "],\n";
    out << "    \"boneSocketColor\": [" << boneSocketColor[0] << ", " << boneSocketColor[1] << ", " << boneSocketColor[2] << ", " << boneSocketColor[3] << "]\n";
    out << "  },\n";
    out << "  \"gizmo\": {\n";
    out << "    \"gizmoOp\": " << static_cast<int>(gizmoOp) << ",\n";
    out << "    \"gizmoSpace\": " << static_cast<int>(gizmoSpace) << ",\n";
    out << "    \"gizmoSnap\": " << (gizmoSnap ? "true" : "false") << ",\n";
    out << "    \"snapTranslate\": " << snapTranslate << ",\n";
    out << "    \"snapRotateDeg\": " << snapRotateDeg << ",\n";
    out << "    \"snapScale\": " << snapScale << ",\n";
    out << "    \"gizmoShowAxisLabels\": " << (gizmoShowAxisLabels ? "true" : "false") << ",\n";
    out << "    \"gizmoHandleSize\": " << gizmoHandleSize << ",\n";
    out << "    \"gizmoShowPlaneHandles\": " << (gizmoShowPlaneHandles ? "true" : "false") << ",\n";
    out << "    \"gizmoShowCenterHandle\": " << (gizmoShowCenterHandle ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"weightPaint\": {\n";
    out << "    \"brushMode\": " << static_cast<int>(brushMode) << ",\n";
    out << "    \"brushRadius\": " << brushRadius << ",\n";
    out << "    \"brushStrength\": " << brushStrength << ",\n";
    out << "    \"symmetryEnabled\": " << (symmetryEnabled ? "true" : "false") << ",\n";
    out << "    \"symmetryAxis\": " << static_cast<int>(symmetryAxis) << ",\n";
    out << "    \"paintFalloff\": " << static_cast<int>(paintFalloff) << "\n";
    out << "  },\n";
    out << "  \"ui\": {\n";
    out << "    \"compactMode\": " << (uiSettings.compactMode ? "true" : "false") << ",\n";
    out << "    \"panelSpacing\": " << uiSettings.panelSpacing << ",\n";
    out << "    \"panelRounding\": " << uiSettings.panelRounding << ",\n";
    out << "    \"showBonePanel\": " << (uiSettings.showBonePanel ? "true" : "false") << ",\n";
    out << "    \"showWeightsPanel\": " << (uiSettings.showWeightsPanel ? "true" : "false") << ",\n";
    out << "    \"showMaterialsPanel\": " << (uiSettings.showMaterialsPanel ? "true" : "false") << ",\n";
    out << "    \"showMSMInspectorPanel\": " << (uiSettings.showMSMInspectorPanel ? "true" : "false") << ",\n";
    out << "    \"showProjectPanel\": " << (uiSettings.showProjectPanel ? "true" : "false") << ",\n";
    out << "    \"showExportPanel\": " << (uiSettings.showExportPanel ? "true" : "false") << ",\n";
    out << "    \"showValidationPanel\": " << (uiSettings.showValidationPanel ? "true" : "false") << ",\n";
    out << "    \"showConsolePanel\": " << (uiSettings.showConsolePanel ? "true" : "false") << ",\n";
    out << "    \"showSystemPanel\": " << (uiSettings.showSystemPanel ? "true" : "false") << ",\n";
    out << "    \"showTimelinePanel\": " << (uiSettings.showTimelinePanel ? "true" : "false") << ",\n";
    out << "    \"showSettingsPanel\": " << (uiSettings.showSettingsPanel ? "true" : "false") << ",\n";
    out << "    \"showBoneDisplayPanel\": " << (uiSettings.showBoneDisplayPanel ? "true" : "false") << ",\n";
    out << "    \"showGizmoPanel\": " << (uiSettings.showGizmoPanel ? "true" : "false") << ",\n";
    out << "    \"showViewportSettingsPanel\": " << (uiSettings.showViewportSettingsPanel ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"timeline\": {\n";
    out << "    \"timelineFps\": " << timelineFps << ",\n";
    out << "    \"timelineLoop\": " << (timelineLoop ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"export\": {\n";
    out << "    \"lodRatio\": " << prefs.lodRatio << "\n";
    out << "  },\n";
    out << "  \"autosave\": {\n";
    out << "    \"autosaveMinutes\": " << autosaveMinutes << "\n";
    out << "  }\n";
    out << "}\n";
    
    setStatus("Preferences saved: " + path.string(), "success");
    return ResultVoid::ok();
}

ResultVoid App::loadPreferences(const std::filesystem::path& configDir) {
    const std::filesystem::path path = configDir / "user_prefs.json";
    if (!std::filesystem::exists(path)) {
        return ResultVoid::ok();  // No prefs file is not an error
    }
    
    std::ifstream in(path);
    if (!in.is_open()) {
        return ResultVoid::fail("Cannot open preferences file for reading.", "IO");
    }
    
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string json = buf.str();
    
    // Simple JSON parsing (we only need to extract values)
    // For simplicity, we'll use a basic parser here
    // In production, you'd use a proper JSON library
    
    auto extractFloat = [&](const std::string& key, float& out) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos != std::string::npos) {
            pos = json.find(":", pos);
            if (pos != std::string::npos) {
                pos = json.find_first_not_of(" \t\n\r", pos + 1);
                if (pos != std::string::npos) {
                    try {
                        out = std::stof(json.substr(pos));
                    } catch (...) {}
                }
            }
        }
    };
    
    auto extractBool = [&](const std::string& key, bool& out) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos != std::string::npos) {
            pos = json.find(":", pos);
            if (pos != std::string::npos) {
                pos = json.find_first_not_of(" \t\n\r", pos + 1);
                if (pos != std::string::npos) {
                    if (json.compare(pos, 4, "true") == 0) out = true;
                    else if (json.compare(pos, 5, "false") == 0) out = false;
                }
            }
        }
    };
    
    auto extractInt = [&](const std::string& key, int& out) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos != std::string::npos) {
            pos = json.find(":", pos);
            if (pos != std::string::npos) {
                pos = json.find_first_not_of(" \t\n\r", pos + 1);
                if (pos != std::string::npos) {
                    try {
                        out = std::stoi(json.substr(pos));
                    } catch (...) {}
                }
            }
        }
    };
    
    auto extractColor = [&](const std::string& key, float out[4]) {
        std::string search = "\"" + key + "\"";
        size_t pos = json.find(search);
        if (pos != std::string::npos) {
            pos = json.find("[", pos);
            if (pos != std::string::npos) {
                for (int i = 0; i < 4; ++i) {
                    pos = json.find_first_not_of(" \t\n\r,", pos + 1);
                    if (pos == std::string::npos) break;
                    try {
                        out[i] = std::stof(json.substr(pos));
                    } catch (...) {}
                }
            }
        }
    };
    
    // Camera
    extractFloat("fovY", camera.fovY);
    extractFloat("nearZ", camera.nearZ);
    extractFloat("farZ", camera.farZ);
    extractBool("orthographic", camera.orthographic);
    extractFloat("orthoHeight", camera.orthoHeight);
    extractFloat("orbitSensitivity", camera.orbitSensitivity);
    extractFloat("panSensitivity", camera.panSensitivity);
    extractFloat("zoomSensitivity", camera.zoomSensitivity);
    
    // Viewport
    extractBool("showGrid", showGrid);
    extractBool("showBones", showBones);
    extractBool("xrayBones", xrayBones);
    extractBool("showWireOverlay", showWireOverlay);
    extractBool("textured", textured);
    extractBool("previewDeform", previewDeform);
    extractBool("useDqs", useDqs);
    {
        int vm = 0;
        extractInt("viewMode", vm);
        if (vm >= 0 && vm < 7) viewMode = static_cast<ViewMode>(vm);
    }
    
    // Bone visualization
    extractFloat("boneThickness", boneThickness);
    extractFloat("boneJointSize", boneJointSize);
    extractBool("boneShowLabels", boneShowLabels);
    extractBool("boneShowJointCrosses", boneShowJointCrosses);
    extractColor("boneSelectedColor", boneSelectedColor);
    extractColor("boneHoveredColor", boneHoveredColor);
    extractColor("boneLockedColor", boneLockedColor);
    extractColor("boneParentColor", boneParentColor);
    extractColor("boneChildColor", boneChildColor);
    extractColor("boneDefaultColor", boneDefaultColor);
    extractColor("boneHiddenColor", boneHiddenColor);
    extractColor("boneSocketColor", boneSocketColor);
    
    // Gizmo
    {
        int op = 0, sp = 0;
        extractInt("gizmoOp", op);
        extractInt("gizmoSpace", sp);
        if (op >= 0 && op < 3) gizmoOp = static_cast<GizmoOp>(op);
        if (sp >= 0 && sp < 3) gizmoSpace = static_cast<GizmoSpace>(sp);
    }
    extractBool("gizmoSnap", gizmoSnap);
    extractFloat("snapTranslate", snapTranslate);
    extractFloat("snapRotateDeg", snapRotateDeg);
    extractFloat("snapScale", snapScale);
    extractBool("gizmoShowAxisLabels", gizmoShowAxisLabels);
    extractFloat("gizmoHandleSize", gizmoHandleSize);
    extractBool("gizmoShowPlaneHandles", gizmoShowPlaneHandles);
    extractBool("gizmoShowCenterHandle", gizmoShowCenterHandle);
    
    // Weight paint
    {
        int bm = 0, sa = 0, pf = 0;
        extractInt("brushMode", bm);
        extractInt("symmetryAxis", sa);
        extractInt("paintFalloff", pf);
        if (bm >= 0 && bm < 6) brushMode = static_cast<BrushMode>(bm);
        if (sa >= 0 && sa < 3) symmetryAxis = static_cast<SymmetryAxis>(sa);
        if (pf >= 0 && pf < 3) paintFalloff = static_cast<PaintFalloff>(pf);
    }
    extractFloat("brushRadius", brushRadius);
    extractFloat("brushStrength", brushStrength);
    extractBool("symmetryEnabled", symmetryEnabled);
    
    // UI
    extractBool("compactMode", uiSettings.compactMode);
    extractFloat("panelSpacing", uiSettings.panelSpacing);
    extractFloat("panelRounding", uiSettings.panelRounding);
    extractBool("showBonePanel", uiSettings.showBonePanel);
    extractBool("showWeightsPanel", uiSettings.showWeightsPanel);
    extractBool("showMaterialsPanel", uiSettings.showMaterialsPanel);
    extractBool("showMSMInspectorPanel", uiSettings.showMSMInspectorPanel);
    extractBool("showProjectPanel", uiSettings.showProjectPanel);
    extractBool("showExportPanel", uiSettings.showExportPanel);
    extractBool("showValidationPanel", uiSettings.showValidationPanel);
    extractBool("showConsolePanel", uiSettings.showConsolePanel);
    extractBool("showSystemPanel", uiSettings.showSystemPanel);
    extractBool("showTimelinePanel", uiSettings.showTimelinePanel);
    extractBool("showSettingsPanel", uiSettings.showSettingsPanel);
    extractBool("showBoneDisplayPanel", uiSettings.showBoneDisplayPanel);
    extractBool("showGizmoPanel", uiSettings.showGizmoPanel);
    extractBool("showViewportSettingsPanel", uiSettings.showViewportSettingsPanel);
    
    // Timeline
    extractFloat("timelineFps", timelineFps);
    extractBool("timelineLoop", timelineLoop);
    
    // Export
    extractFloat("lodRatio", prefs.lodRatio);
    
    // Autosave
    extractInt("autosaveMinutes", autosaveMinutes);
    
    applyPreferences();
    
    setStatus("Preferences loaded: " + path.string(), "success");
    return ResultVoid::ok();
}

void App::applyPreferences() {
    // Apply camera settings
    camera.updateClip();
    
    // Apply viewport settings
    if (LoadedAsset* a = currentAsset()) a->gpuDirty = true;
    
    // Apply bone visualization settings
    // (handled in drawSceneContents via app settings)
    
    // Apply gizmo settings
    // (handled in updateBoneGizmo via app settings)
    
    // Apply UI settings
    // (handled in main.cpp via ImGui style)
}

// --- Self-learning weight transfer implementations ---

SelfLearningTransfer& App::getLearningTransfer() {
    if (!learningTransfer) {
        learningTransfer = std::make_unique<SelfLearningTransfer>(learningDb);
    }
    return *learningTransfer;
}

ResultVoid App::learnFromAsset(const std::string& assetId) {
    auto it = assets.find(assetId);
    if (it == assets.end())
        return ResultVoid::fail("Asset not found: " + assetId, "SELF_LEARN");
    LoadedAsset* a = &it->second;
    if (a->mesh.vertices.empty() || a->skeleton.bones.empty())
        return ResultVoid::fail("Asset has no mesh or skeleton.", "SELF_LEARN");
    Gr2CharacterProfile profile;
    profile.profileId = a->profileId;
    profile.race = "unknown";
    profile.gender = "none";
    profile.variant = "base";
    // Extract race from profileId
    if (a->profileId.rfind("pc_", 0) == 0) {
        std::string rest = a->profileId.substr(3);
        size_t pos = rest.find("_");
        if (pos != std::string::npos) {
            profile.race = rest.substr(0, pos);
            std::string gender = rest.substr(pos + 1);
            profile.gender = (gender == "m") ? "male" : (gender == "f") ? "female" : "none";
        }
    }
    learningDb.recordVertexAffinities(profile, a->mesh, a->skeleton);
    setStatus("Learned vertex affinities from " + a->id, "success");
    return ResultVoid::ok();
}

ResultVoid App::learnFromDirectory(const std::string& dirPath) {
    Gr2BatchAnalyzer analyzer(learningDb);
    auto r = analyzer.analyzeDirectory(dirPath);
    if (!r) return r;
    r = analyzer.buildInitialDatabase();
    if (!r) return r;
    setStatus("Analyzed GR2 directory and built learning database.", "success");
    return ResultVoid::ok();
}

ResultVoid App::saveLearningDatabase(const std::string& path) {
    return learningDb.save(path);
}

ResultVoid App::loadLearningDatabase(const std::string& path) {
    return learningDb.load(path);
}

ResultVoid App::selfLearningTransfer(const std::string& sourceAssetId) {
    LoadedAsset* dst = currentAsset();
    if (!dst) return ResultVoid::fail("No asset loaded.", "SELF_LEARN_TRANSFER");
    auto it = assets.find(sourceAssetId);
    if (it == assets.end())
        return ResultVoid::fail("Source asset not found: " + sourceAssetId, "SELF_LEARN_TRANSFER");
    const LoadedAsset& src = it->second;
    if (src.mesh.vertices.empty())
        return ResultVoid::fail("Source mesh is empty.", "SELF_LEARN_TRANSFER", src.id);
    
    Gr2CharacterProfile srcProfile;
    srcProfile.profileId = src.profileId;
    Gr2CharacterProfile dstProfile;
    dstProfile.profileId = dst->profileId;
    
    pushUndoSnapshot("self-learning transfer from " + src.id);
    auto stats = getLearningTransfer().transfer(learningDb, src.mesh, src.skeleton, srcProfile,
                                           dst->mesh, dst->skeleton, dstProfile, 3, &lockedBones);
    if (!stats) return ResultVoid::fail(stats.error());
    noteWeightsChanged();
    
    // Locked bones keep their pre-transfer weights
    if (!lockedBones.empty()) {
        const InfluenceSnapshot& snap = undoStack.back();
        for (std::size_t i = 0; i < dst->mesh.vertices.size() && i < snap.influences.size(); ++i) {
            auto& infs = dst->mesh.vertices[i].influences;
            for (const auto& old : snap.influences[i]) {
                if (!isBoneLocked(old.bone)) continue;
                bool found = false;
                for (auto& cur : infs) {
                    if (cur.bone == old.bone) {
                        cur.weight = old.weight;
                        found = true;
                        break;
                    }
                }
                if (!found) infs.push_back(old);
            }
            RepairStats lrs;
            repairVertexInfluences(infs, kMetin2MaxInfluences, &lrs);
            (void)lrs;
        }
    }
    dst->dirty = true;
    dst->gpuDirty = true;
    runValidation();
    setStatus(stats.value().toDisplayString(), "success");
    return ResultVoid::ok();
}

ResultVoid App::selfLearningAutoRig() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "SELF_LEARN_AUTORIG");
    if (a->mesh.vertices.empty()) return ResultVoid::fail("Mesh is empty.", "SELF_LEARN_AUTORIG", a->id);
    if (a->skeleton.bones.empty())
        return ResultVoid::fail("Skeleton is empty.", "SELF_LEARN_AUTORIG", a->id);
    
    Gr2CharacterProfile profile;
    profile.profileId = a->profileId;
    
    pushUndoSnapshot("self-learning auto-rig");
    auto stats = getLearningTransfer().autoRig(learningDb, a->mesh, a->skeleton, profile, 0.02f, &lockedBones);
    if (!stats) return ResultVoid::fail(stats.error());
    noteWeightsChanged();
    
    // Locked bones keep their pre-rig weights
    if (!lockedBones.empty()) {
        const InfluenceSnapshot& snap = undoStack.back();
        for (std::size_t i = 0; i < a->mesh.vertices.size() && i < snap.influences.size(); ++i) {
            auto& infs = a->mesh.vertices[i].influences;
            for (const auto& old : snap.influences[i]) {
                if (!isBoneLocked(old.bone)) continue;
                bool found = false;
                for (auto& cur : infs) {
                    if (cur.bone == old.bone) {
                        cur.weight = old.weight;
                        found = true;
                        break;
                    }
                }
                if (!found) infs.push_back(old);
            }
            RepairStats lrs;
            repairVertexInfluences(infs, kMetin2MaxInfluences, &lrs);
            (void)lrs;
        }
    }
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus(stats.value().toDisplayString(), "success");
    return ResultVoid::ok();
}

ResultVoid App::analyzeGr2Directory(const std::string& dirPath) {
    Gr2BatchAnalyzer analyzer(learningDb);
    auto r = analyzer.analyzeDirectory(dirPath);
    if (!r) return r;
    r = analyzer.buildInitialDatabase();
    if (!r) return r;
    // Export a report
    std::filesystem::path reportPath = std::filesystem::path(dirPath) / "gr2_analysis_report.md";
    r = analyzer.exportReport(reportPath);
    if (!r) return r;
    setStatus("GR2 directory analyzed. Report: " + reportPath.string(), "success");
    return ResultVoid::ok();
}

}  // namespace m2rig