// GPU upload for the desktop app (lives in the exe target, not the core
// library, so m2rig_core stays free of renderer translation units and the
// dependency-free test harness keeps linking).
#include "m2rig/app.hpp"

#include <filesystem>

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
    const std::filesystem::path exeDir = std::filesystem::current_path();
    const std::filesystem::path inModels = exeDir / "Data" / "Models" / base;
    if (std::filesystem::exists(inModels)) return inModels.string();
    if (std::filesystem::exists(exeDir / base)) return (exeDir / base).string();
    return {};
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
    if (!a->gpuDirty) return ResultVoid::ok();
    std::string err;
    std::vector<GpuVertex> verts;
    const bool deformed = previewDeform && !a->animFrames.empty();
    if (deformed) {
        // Render-only CPU skinning preview; canonical bind data is untouched.
        const std::vector<Mat4> palette = buildSkinningPalette(a->skeleton, a->bindInverse);
        if (viewMode == ViewMode::Weights && selectedBone >= 0)
            verts = buildGpuVerticesDeformed(
                a->mesh, palette, MeshColoring::Weight, static_cast<std::uint32_t>(selectedBone));
        else
            verts = buildGpuVerticesDeformed(a->mesh, palette, coloringFor(viewMode),
                                             static_cast<std::uint32_t>(selectedBone < 0
                                                                            ? 0
                                                                            : selectedBone));
    } else if (viewMode == ViewMode::Weights && selectedBone >= 0) {
        verts = buildGpuVerticesWeight(a->mesh, static_cast<std::uint32_t>(selectedBone));
    } else {
        verts = buildGpuVertices(a->mesh, coloringFor(viewMode));
    }
    // Submesh isolation: hidden submeshes are skipped at upload only.
    const std::vector<std::uint32_t> visibleIndices = filterVisibleIndices(a->mesh, hiddenSubmeshes);
    if (visibleIndices.empty()) {
        // Everything hidden: keep the last upload, just clear the flag.
        a->gpuDirty = false;
        return ResultVoid::ok();
    }
    if (!renderer.uploadMesh(a->id, verts, visibleIndices, err)) {
        return ResultVoid::fail(std::move(err), "RENDER", a->id, "uploadMesh");
    }
    // Optional texturing: first material with a resolvable DDS wins.
    renderer.setActiveTexture({});
    if (textured && !a->mesh.materials.empty()) {
        const std::string found = resolveTextureFile(a->mesh.materials[0].texturePath);
        if (!found.empty()) {
            if (auto img = readDdsFile(found, a->id); img) {
                std::string texErr;
                if (renderer.setTexture(a->id, img.value().rgba.data(), img.value().width,
                                        img.value().height, texErr))
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
