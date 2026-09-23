// GPU upload for the desktop app (lives in the exe target, not the core
// library, so m2rig_core stays free of renderer translation units and the
// dependency-free test harness keeps linking).
#include "m2rig/app.hpp"

#include <filesystem>

#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/dds.hpp"

namespace m2rig {

namespace {

// Same probing as the material panel: literal path, exe-dir Data/Models,
// bare basename. Empty when nothing exists.
std::string resolveTextureFile(const std::string& texturePath) {
    if (texturePath.empty()) return {};
    if (std::filesystem::exists(texturePath)) return texturePath;
    const std::string base = std::filesystem::path(texturePath).filename().string();
    if (base.empty()) return {};
    const std::filesystem::path exeDir = executableDir();
    auto probe = [&](const std::filesystem::path& dir) -> std::string {
        if (dir.empty()) return {};
        const std::filesystem::path c = dir / base;
        if (std::filesystem::exists(c)) return c.string();
        const std::filesystem::path m = dir / "Data" / "Models" / base;
        if (std::filesystem::exists(m)) return m.string();
        return {};
    };
    auto probeAncestors = [&](std::filesystem::path root) -> std::string {
        while (!root.empty()) {
            if (std::string hit = probe(root); !hit.empty()) return hit;
            const std::filesystem::path parent = root.parent_path();
            if (parent == root) break;
            root = parent;
        }
        return {};
    };
    if (std::string hit = probeAncestors(exeDir); !hit.empty()) return hit;
    return probeAncestors(std::filesystem::current_path());
}

MeshColoring coloringFor(ViewMode mode) {
    switch (mode) {
        case ViewMode::Normals: return MeshColoring::Normals;
        case ViewMode::Height: return MeshColoring::Height;
        case ViewMode::Weights: return MeshColoring::Weight;
        case ViewMode::UV: return MeshColoring::UV;
        default: return MeshColoring::Solid;
    }
}

}  // namespace

ResultVoid App::refreshGpu(Renderer& renderer) {
    LoadedAsset* a = currentAsset();
    if (!a) return ResultVoid::ok();
    if (!a->gpuDirty && renderer.hasMesh(a->id)) return ResultVoid::ok();
    std::string err;
    std::vector<GpuVertex> verts;
    // GPU skinning (Wave 24): the viewport uploads BIND-pose vertices plus a
    // bone stream once, then deforms on the GPU per frame via the palette —
    // no per-frame CPU deform + re-upload during timeline playback. Only the
    // legacy CPU-DQS preview keeps uploading pre-deformed vertices, and then
    // carries no skin stream (hasSkinning gates the skinned draws, so the two
    // paths can never double-deform). Canonical bind data is untouched.
    const bool cpuDqs = previewDeform && !a->animFrames.empty() && useDqs;
    if (cpuDqs) {
        // Legacy CPU-DQS path (unchanged behavior): deformed verts, no skin
        // stream. Weights mode without a selected bone falls back to deformed
        // Solid (never a silent bone-0 heatmap).
        const bool weightsHeat = viewMode == ViewMode::Weights && selectedBone >= 0;
        const std::vector<DualQuat> palette = buildDqsPalette(a->skeleton, a->bindInverse);
        if (weightsHeat)
            verts = buildGpuVerticesDeformedDqs(
                a->mesh, palette, MeshColoring::Weight, static_cast<std::uint32_t>(selectedBone));
        else if (viewMode == ViewMode::Weights)
            verts = buildGpuVerticesDeformedDqs(a->mesh, palette, MeshColoring::Solid, 0);
        else
            verts = buildGpuVerticesDeformedDqs(a->mesh, palette, coloringFor(viewMode),
                                                 static_cast<std::uint32_t>(selectedBone < 0 ? 0
                                                                                             : selectedBone));
    } else if (viewMode == ViewMode::Weights && selectedBone >= 0) {
        verts = buildGpuVerticesWeight(a->mesh, static_cast<std::uint32_t>(selectedBone));
    } else {
        verts = buildGpuVertices(a->mesh, coloringFor(viewMode));
    }
    // Submesh isolation: hidden submeshes are skipped at upload only.
    const std::vector<std::uint32_t> visibleIndices = filterVisibleIndices(a->mesh, hiddenSubmeshes);
    if (visibleIndices.empty()) {
        // Everything hidden: remove the old upload so a previous asset or
        // visibility state cannot leave stale geometry on screen.
        renderer.releaseMesh(a->id);
        a->gpuDirty = false;
        return ResultVoid::ok();
    }
    if (!renderer.uploadMesh(a->id, verts, visibleIndices, err)) {
        return ResultVoid::fail(std::move(err), "RENDER", a->id, "uploadMesh");
    }
    // Pair the bind-pose upload with its GPU skin stream (skipped only for
    // the CPU-DQS path, which carries deformed verts and must never meet a
    // skin stream). buildSkinVertices fails explicitly on bad ids/counts —
    // then the mesh draws static with an honest warning, never mis-deformed.
    if (!cpuDqs) {
        if (auto skin = buildSkinVertices(a->mesh, a->skeleton.bones.size()); skin) {
            if (!renderer.uploadSkinning(a->id, skin.value(), err))
                setStatus("GPU skin upload failed: " + err, "warning");
        } else {
            renderer.releaseSkinning(a->id);
            setStatus("GPU skinning unavailable (" + skin.error().message + "); showing bind pose.",
                      "warning");
        }
    } else {
        renderer.releaseSkinning(a->id);
    }
    // Optional texturing: first material with a resolvable DDS wins
    // (materials[0]-only; per-submesh N-draws are Slice C2). Authored mips
    // are primary via setTextureMips; mipCount<=1 / procedural stays on the
    // GenerateMips fallback in setTexture.
    renderer.setActiveTexture({});
    if (textured && !a->mesh.materials.empty()) {
        const std::string found = resolveTextureFile(a->mesh.materials[0].texturePath);
        if (!found.empty()) {
            if (auto img = readDdsFile(found, a->id); img) {
                std::string texErr;
                const DdsImage& di = img.value();
                const bool useAuthored = di.mipCount > 1 && di.mips.size() > 1;
                const bool ok = useAuthored
                                    ? renderer.setTextureMips(a->id, di, texErr)
                                    : renderer.setTexture(a->id, di.rgba.data(), di.width,
                                                          di.height, texErr);
                if (ok)
                    renderer.setActiveTexture(a->id);
                else
                    setStatus("Texture upload failed: " + texErr, "warning");
            }
        }
    }
    a->gpuDirty = false;
    return ResultVoid::ok();
}

}  // namespace m2rig
