// GPU upload for the desktop app (lives in the exe target, not the core
// library, so m2rig_core stays free of renderer translation units and the
// dependency-free test harness keeps linking).
#include "m2rig/app.hpp"

namespace m2rig {

namespace {

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
    if (!renderer.uploadMesh(a->id, verts, a->mesh.indices, err)) {
        return ResultVoid::fail(std::move(err), "RENDER", a->id, "uploadMesh");
    }
    a->gpuDirty = false;
    return ResultVoid::ok();
}

}  // namespace m2rig
