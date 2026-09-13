// Application state implementation.
#include "m2rig/app.hpp"

#include <cstdio>
#include <filesystem>
#include <sstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "m2rig/logging.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/workspace/project_file.hpp"

namespace m2rig {

const char* viewModeName(ViewMode mode) {
    switch (mode) {
        case ViewMode::Solid: return "Solid";
        case ViewMode::Wireframe: return "Wireframe";
        case ViewMode::SolidWireframe: return "Solid + Wire";
        case ViewMode::Normals: return "Normals";
        case ViewMode::Height: return "Height";
        case ViewMode::Weights: return "Weights";
        case ViewMode::UV: return "UV";
    }
    return "Solid";
}

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
    if (const LoadedAsset* a = currentAsset(); a) camera.frameAabb(a->mesh.bounds);
    setStatus("Loaded sample warrior armor (" + std::to_string(assets[current].mesh.vertices.size()) +
                  " vertices, " + std::to_string(assets[current].skeleton.bones.size()) + " bones).",
              "success");
    return ResultVoid::ok();
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

std::size_t App::boneInfluenceCount(std::uint32_t boneId) const {    const LoadedAsset* a = currentAsset();
    if (!a) return 0;
    std::size_t n = 0;
    for (const auto& v : a->mesh.vertices)
        for (const auto& inf : v.influences)
            if (inf.bone == boneId && inf.weight > 0.0f) {
                ++n;
                break;
            }
    return n;
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
    auto parsed = parseSmd(text.value(), path);
    if (!parsed) return ResultVoid::fail(parsed.error());
    const std::string id = stemOf(path);
    auto conv = smdToAsset(parsed.value(), id);
    if (!conv) return ResultVoid::fail(conv.error());
    LoadedAsset asset;
    asset.id = id;
    asset.mesh = std::move(conv.value().mesh);
    asset.skeleton = std::move(conv.value().skeleton);
    for (const auto& b : asset.skeleton.bones) asset.bindInverse.push_back(b.inverseBindTransform);
    asset.animFrames = std::move(conv.value().frames);
    asset.currentFrame = 0;
    asset.sourcePath = path;
    asset.gpuDirty = true;
    assets[id] = std::move(asset);
    current = id;
    selectedBone = -1;
    if (const LoadedAsset* a = currentAsset()) camera.frameAabb(a->mesh.bounds);
    runValidation();
    setStatus("Imported " + path + " (" + std::to_string(assets[id].mesh.vertices.size()) +
                  " vertices, " + std::to_string(assets[id].mesh.triangleCount()) +
                  " triangles, " + std::to_string(assets[id].skeleton.bones.size()) +
                  " bones, " + std::to_string(assets[id].animFrames.size()) + " frames).",
              report.exportBlocked() ? "warning" : "success");
    return ResultVoid::ok();
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
    if (a->animFrames.empty())
        return ResultVoid::fail("Asset has no animation frames.", "ANIMATION", a->id);
    if (auto r = poseSkeletonFromFrame(a->skeleton, a->animFrames, frameIndex); !r)
        return ResultVoid::fail(r.error());
    a->currentFrame = frameIndex;
    a->gpuDirty = true;  // deform preview + bone overlay follow the pose

    return ResultVoid::ok();
}

// Undo/redo command implementations (legacy single-vertex stubs kept for ABI).

void UndoPaintCommand::undo() {}

void UndoPaintCommand::redo() {}

void commitUndo() {}

void commitRedo() {}

void doRedo() {}

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
    LoadedAsset* a = currentAsset();
    if (!a) return;
    InfluenceSnapshot snap;
    snap.label = label;
    snap.influences.reserve(a->mesh.vertices.size());
    for (const auto& v : a->mesh.vertices) snap.influences.push_back(v.influences);
    snap.bonePos.reserve(a->skeleton.bones.size());
    snap.boneRot.reserve(a->skeleton.bones.size());
    for (const auto& b : a->skeleton.bones) {
        snap.bonePos.push_back(b.localPosition);
        snap.boneRot.push_back(b.localRotationEuler);
    }
    undoStack.push_back(std::move(snap));
    if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void App::undo() {
    LoadedAsset* a = currentAsset();
    if (!a || undoStack.empty()) {
        setStatus("Nothing to undo.", "info");
        return;
    }
    InfluenceSnapshot redoSnap;
    redoSnap.label = "redo";
    redoSnap.influences.reserve(a->mesh.vertices.size());
    for (const auto& v : a->mesh.vertices) redoSnap.influences.push_back(v.influences);
    for (const auto& b : a->skeleton.bones) {
        redoSnap.bonePos.push_back(b.localPosition);
        redoSnap.boneRot.push_back(b.localRotationEuler);
    }
    redoStack.push_back(std::move(redoSnap));
    InfluenceSnapshot snap = std::move(undoStack.back());
    undoStack.pop_back();
    for (std::size_t i = 0; i < a->mesh.vertices.size() && i < snap.influences.size(); ++i)
        a->mesh.vertices[i].influences = std::move(snap.influences[i]);
    if (snap.bonePos.size() == a->skeleton.bones.size()) {
        for (std::size_t i = 0; i < a->skeleton.bones.size(); ++i) {
            a->skeleton.bones[i].localPosition = snap.bonePos[i];
            a->skeleton.bones[i].localRotationEuler = snap.boneRot[i];
        }
        rebuildSkeletonRuntime(a->skeleton);
    }
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus("Undo: " + snap.label, "success");
}

void App::redo() {
    LoadedAsset* a = currentAsset();
    if (!a || redoStack.empty()) {
        setStatus("Nothing to redo.", "info");
        return;
    }
    InfluenceSnapshot undoSnap;
    undoSnap.label = "undo";
    undoSnap.influences.reserve(a->mesh.vertices.size());
    for (const auto& v : a->mesh.vertices) undoSnap.influences.push_back(v.influences);
    for (const auto& b : a->skeleton.bones) {
        undoSnap.bonePos.push_back(b.localPosition);
        undoSnap.boneRot.push_back(b.localRotationEuler);
    }
    undoStack.push_back(std::move(undoSnap));
    InfluenceSnapshot snap = std::move(redoStack.back());
    redoStack.pop_back();
    for (std::size_t i = 0; i < a->mesh.vertices.size() && i < snap.influences.size(); ++i)
        a->mesh.vertices[i].influences = std::move(snap.influences[i]);
    if (snap.bonePos.size() == a->skeleton.bones.size()) {
        for (std::size_t i = 0; i < a->skeleton.bones.size(); ++i) {
            a->skeleton.bones[i].localPosition = snap.bonePos[i];
            a->skeleton.bones[i].localRotationEuler = snap.boneRot[i];
        }
        rebuildSkeletonRuntime(a->skeleton);
    }
    a->dirty = true;
    a->gpuDirty = true;
    runValidation();
    setStatus("Redo applied.", "success");
}

std::size_t App::paintStroke(const Vec3& center) {
    LoadedAsset* a = currentAsset();
    if (!a || selectedBone < 0) return 0;
    if (isBoneLocked(static_cast<std::uint32_t>(selectedBone))) {
        setStatus("Bone is locked - unlock to paint.", "warning");
        return 0;
    }
    pushUndoSnapshot("paint " + std::to_string(selectedBone));
    PaintParams params;
    params.bone = static_cast<std::uint32_t>(selectedBone);
    params.center = center;
    params.radius = brushRadius;
    params.strength = brushStrength;
    params.falloff = paintFalloff;
    params.op = brushOpFor(brushMode);
    params.normalize = true;
    PaintStrokeStats stats = paintMeshStroke(a->mesh, params);
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
        }
    }
    gPaintState.lastStats.verticesAffected = stats.verticesAffected;
    gPaintState.lastStats.totalStrokes += 1;
    gPaintState.lastStats.totalStrengthApplied += stats.totalStrengthApplied;
    a->dirty = true;
    a->gpuDirty = true;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "Paint %s: %zu verts, dropped mass %.4f.",
                  brushModeName(brushMode), stats.verticesAffected, stats.droppedMass);
    setStatus(buf, "success");
    return stats.verticesAffected;
}

ResultVoid App::mirrorWeights() {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::fail("No asset loaded.", "SYMMETRY");
    const int axis = static_cast<int>(symmetryAxis);
    pushUndoSnapshot("mirror");
    auto pairs = findMirrorPairs(a->mesh, axis);
    if (pairs.empty()) return ResultVoid::fail("No mirror vertex pairs found.", "SYMMETRY", a->id);
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

ResultVoid App::exportMsmFile(const std::string& path) {    LoadedAsset* a = currentAsset();
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

static std::string lowerExt(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

ResultVoid App::importBridgedFile(const std::string& path) {
    const std::string ext = lowerExt(path);
    if (ext == ".smd") return importSmdFile(path);
    if (ext == ".gr2") {
        // Primary path: grnreader98 (documented batch tool, ships its own
        // granny2.dll). Fallback: Noesis ?cmode (needs GR2 plugins).
        setStatus("Running grnreader98 bridge (external tool, up to 60s)...", "info");
        if (auto smd = convertGr2ToSmdViaGrnReader(path); smd) {
            auto parsed = parseSmd(smd.value(), path);
            if (!parsed) return ResultVoid::fail(parsed.error());
            auto conv = smdToAsset(parsed.value(), stemOf(path));
            if (!conv) return ResultVoid::fail(conv.error());
            LoadedAsset asset;
            asset.id = stemOf(path);
            asset.mesh = std::move(conv.value().mesh);
            asset.skeleton = std::move(conv.value().skeleton);
            for (const auto& b : asset.skeleton.bones)
                asset.bindInverse.push_back(b.inverseBindTransform);
            asset.animFrames = std::move(conv.value().frames);
            asset.sourcePath = path;
            asset.gpuDirty = true;
            assets[asset.id] = std::move(asset);
            current = asset.id;
            selectedBone = -1;
            if (const LoadedAsset* a = currentAsset()) camera.frameAabb(a->mesh.bounds);
            runValidation();
            setStatus("Imported GR2 via grnreader98 (" +
                          std::to_string(assets[current].mesh.vertices.size()) + " verts, " +
                          std::to_string(assets[current].skeleton.bones.size()) + " bones).",
                      report.exportBlocked() ? "warning" : "success");
            return ResultVoid::ok();
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
    const std::string tmp =
        (std::filesystem::temp_directory_path() / "m2rig_bridge_import.smd").string();
    setStatus("Running Noesis bridge (external tool, up to 30s)...", "info");
    std::wstring cmd = L"\"" + cfg.noesisCliPath.wstring() + L"\" ?cmode \"" +
                       std::filesystem::path(path).wstring() + L"\" \"" +
                       std::filesystem::path(tmp).wstring() + L"\"";
    const BridgeResult br = runBridgeLogged(cmd, cfg.timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);
    if (!br.started) {
        return ResultVoid::fail("Failed to start Noesis bridge process.", "IMPORT", "", "bridge.import");
    }
    if (br.timedOut) {
        return ResultVoid::fail("Noesis bridge timed out. Last output: " + lastLine, "IMPORT", "",
                                "bridge.import");
    }
    if (!std::filesystem::exists(tmp))
        return ResultVoid::fail("Bridge produced no SMD output. Last output: " + lastLine,
                                "IMPORT", "", "bridge.import");
    auto r = importSmdFile(tmp);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return r;
}

ResultVoid App::saveWorkspaceFile(const std::string& path) {
    if (auto r = saveCurrentWorkspace(*this, path); !r) return ResultVoid::fail(r.error());
    setStatus("Workspace saved: " + path, "success");
    return ResultVoid::ok();
}

ResultVoid App::loadWorkspaceFile(const std::string& path) {
    if (auto r = restoreWorkspace(*this, path); !r) return ResultVoid::fail(r.error());
    runValidation();
    setStatus("Workspace loaded: " + path, "success");
    return ResultVoid::ok();
}

}  // namespace m2rig