// Viewport: per-submesh textured/PBR routing, picking, and the offscreen
// scene render (drawSceneContents + drawViewportPanel).
// Wave 34 mechanical split: extracted verbatim from panels.cpp.
// HIGH-RISK file (viewport compositing) - moved last, zero edits inside.

#include "panels_internal.hpp"

namespace m2rig {
namespace {
// --- Per-submesh textured routing (uploads live in refreshGpu) -------------
// All 12 range variants exist (renderer.hpp:180-210, Slice C + C2). Routing
// stays intentionally static-textured-only (Blinn) plus textured-PBR below:
// skinned-Blinn, flat-debug and untextured modes keep their whole-draw calls
// (per-mode range expansion is mechanical from here), and only the static
// textured + textured-PBR paths go per-submesh.
bool meshHasDistinctVisibleMaterials(const Mesh& mesh, const std::set<std::size_t>& hidden) {
    // Any hiding compacts the uploaded index buffer (filterVisibleIndices),
    // so original startIndex/count ranges no longer address it — hidden state
    // forces the whole-draw path (today's exact behavior, zero risk).
    if (!hidden.empty()) return false;
    if (mesh.subMeshes.size() < 2) return false;
    std::set<std::uint32_t> seen;
    for (const SubMesh& sm : mesh.subMeshes) {
        if (sm.indexCount == 0) continue;  // degenerate: the renderer skips it
        seen.insert(sm.materialIndex);
        if (seen.size() >= 2) return true;
    }
    return false;
}

int drawTexturedSubmeshRanges(Renderer& renderer, LoadedAsset& asset,
                              const std::set<std::size_t>& hidden, const Mat4& viewProj,
                              FillMode fill) {
    int calls = 0;
    // Single-texture restore binding: reproduces exactly what refreshGpu left
    // behind (materials[0] upload under the asset id, or nothing when no DDS
    // resolved). Used only to restore state after the per-submesh loop.
    const std::string restoreTex = renderer.hasTexture(asset.id) ? asset.id : std::string{};
    constexpr std::uint32_t kMaxRange = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t i = 0; i < asset.mesh.subMeshes.size(); ++i) {
        if (hidden.count(i) != 0) continue;  // belt-and-braces: caller pre-filters
        const SubMesh& sm = asset.mesh.subMeshes[i];
        if (sm.indexCount == 0 || sm.startIndex > kMaxRange) continue;  // renderer would skip
        const std::uint32_t count = sm.indexCount > kMaxRange
                                        ? kMaxRange
                                        : static_cast<std::uint32_t>(sm.indexCount);
        // Per-material lookup at draw time ONLY via hasTexture: the uploader
        // (refreshGpu in app_gpu.cpp, NOT this file) owns uploads under
        // <assetId>#mat<i>, so a key that was never uploaded (or was
        // released after its path went stale) simply misses here — no
        // per-frame decode, no fake success. Submeshes without their own
        // texture are untextured (empty string), NOT material 0's texture.
        const std::string matKey = asset.id + "#mat" + std::to_string(sm.materialIndex);
        renderer.setActiveTexture(renderer.hasTexture(matKey) ? matKey : std::string{});
        renderer.drawMeshTexturedRange(asset.id, static_cast<std::uint32_t>(sm.startIndex),
                                       count, viewProj, fill);
        ++calls;
    }
    renderer.setActiveTexture(restoreTex);  // restore single-texture state for later passes
    return calls;
}

// First non-degenerate submesh material (whole-draw PBR normal binding).
// Falls back to material 0 for meshes without submeshes; indices beyond the
// uploaded keys simply miss the hasTexture probe below.
std::uint32_t primaryMaterialIndex(const Mesh& mesh) {
    for (const SubMesh& sm : mesh.subMeshes)
        if (sm.indexCount != 0) return sm.materialIndex;
    return 0;
}

// Binds the uploaded LINEAR normal map for one material index. Returns true
// when a key was bound (caller must clearPbrNormalMap after the draw);
// missing keys bind nothing, so the textured-PBR draw stays on PsTexPbr
// byte-identically (resolvePbrNormal would ignore them anyway).
bool bindPbrNormalForMaterial(Renderer& renderer, const LoadedAsset& asset,
                              std::uint32_t materialIndex) {
    const std::string key = asset.id + "#nmat" + std::to_string(materialIndex);
    if (!renderer.hasTexture(key)) return false;
    renderer.bindPbrNormalMap(key);
    return true;
}

// Per-submesh textured-PBR routing (static + skinned): mirrors
// drawTexturedSubmeshRanges — per-range albedo binding plus a per-range
// normal-map bind (missing key = unbound = PsTexPbr for that range only).
// Both bindings are restored after the loop.
int drawTexturedPbrSubmeshRanges(Renderer& renderer, LoadedAsset& asset,
                                 const std::set<std::size_t>& hidden, const Mat4& viewProj,
                                 FillMode fill, bool skinned) {
    int calls = 0;
    const std::string restoreTex = renderer.hasTexture(asset.id) ? asset.id : std::string{};
    for (std::size_t i = 0; i < asset.mesh.subMeshes.size(); ++i) {
        if (hidden.count(i) != 0) continue;  // belt-and-braces: caller pre-filters
        const SubMesh& sm = asset.mesh.subMeshes[i];
        if (sm.indexCount == 0) continue;  // renderer would skip
        const std::string matKey = asset.id + "#mat" + std::to_string(sm.materialIndex);
        renderer.setActiveTexture(renderer.hasTexture(matKey) ? matKey : std::string{});
        const std::string nKey = asset.id + "#nmat" + std::to_string(sm.materialIndex);
        renderer.bindPbrNormalMap(renderer.hasTexture(nKey) ? nKey : std::string{});
        if (skinned)
            renderer.drawMeshTexturedSkinnedPbrRange(asset.id, sm.startIndex, sm.indexCount,
                                                     viewProj, fill);
        else
            renderer.drawMeshTexturedPbrRange(asset.id, sm.startIndex, sm.indexCount, viewProj,
                                              fill);
        ++calls;
    }
    renderer.setActiveTexture(restoreTex);  // restore single-texture state for later passes
    renderer.clearPbrNormalMap();
    return calls;
}

// Closest mesh-surface point to a click ray (ray-triangle Moller). Returns
// false when nothing hit; used as the paint brush center.
bool pickMeshPoint(const App& app, float panelW, float panelH, float clickX, float clickY,
                   Vec3& outPoint) {
    const LoadedAsset* a = app.currentAsset();
    if (!a || panelW <= 0 || panelH <= 0 || a->mesh.indices.size() < 3) return false;
    const float aspect = panelW / panelH;
    const Mat4 invVp = (app.camera.viewMatrix() * app.camera.projMatrix(aspect)).inverseGeneral();
    const float nx = (clickX / panelW) * 2.0f - 1.0f;
    const float ny = 1.0f - (clickY / panelH) * 2.0f;
    const Vec3 ro = invVp.transformPoint({nx, ny, 0.0f});
    const Vec3 rf = invVp.transformPoint({nx, ny, 1.0f});
    const Vec3 rd = normalized(rf - ro);
    bool hit = false;
    float bestT = std::numeric_limits<float>::max();
    const auto& verts = a->mesh.vertices;
    for (std::size_t i = 0; i + 2 < a->mesh.indices.size(); i += 3) {
        const std::uint32_t i0 = a->mesh.indices[i], i1 = a->mesh.indices[i + 1],
                             i2 = a->mesh.indices[i + 2];
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;  // MESH_BAD_INDEX
        const Vec3& v0 = verts[i0].position;
        const Vec3& v1 = verts[i1].position;
        const Vec3& v2 = verts[i2].position;
        const Vec3 e1 = v1 - v0, e2 = v2 - v0;
        const Vec3 p = cross(rd, e2);
        const float det = dot(e1, p);
        if (std::fabs(det) < 1e-9f) continue;
        const float inv = 1.0f / det;
        const Vec3 tv = ro - v0;
        const float u = dot(tv, p) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const Vec3 q = cross(tv, e1);
        const float v = dot(rd, q) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        const float t = dot(e2, q) * inv;
        if (t > 0.0f && t < bestT) {
            bestT = t;
            hit = true;
        }
    }
    if (hit) outPoint = ro + rd * bestT;
    return hit;
}

// Viewport bone picking: nearest bone segment to the click ray, threshold in
// pixels converted to world units at the camera target depth.
int pickBoneAt(const App& app, float panelW, float panelH, float clickX, float clickY) {
    const LoadedAsset* a = app.currentAsset();
    if (!a || panelW <= 0 || panelH <= 0) return -1;
    const float aspect = panelW / panelH;
    const Mat4 invVp = (app.camera.viewMatrix() * app.camera.projMatrix(aspect)).inverseGeneral();
    const float nx = (clickX / panelW) * 2.0f - 1.0f;
    const float ny = 1.0f - (clickY / panelH) * 2.0f;
    const Vec3 pNear = invVp.transformPoint({nx, ny, 0.0f});
    const Vec3 pFar = invVp.transformPoint({nx, ny, 1.0f});
    const Vec3 dir = normalized(pFar - pNear);
    const float eyeDist = distance(app.camera.eye(), app.camera.target);
    float worldPerPixel = 0.01f;
    if (app.camera.orthographic) {
        worldPerPixel = app.camera.orthoHeight / panelH;
    } else {
        worldPerPixel =
            2.0f * eyeDist * std::tan(app.camera.fovY * 0.5f) / panelH;
    }
    const float threshold = worldPerPixel * 10.0f;

    int best = -1;
    float bestDist = threshold;
    for (const auto& b : a->skeleton.bones) {
        if (!app.boneVisible(b.id)) continue;
        const Vec3 joint{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                         b.globalTransform.m[3][2]};
        Vec3 segA = joint, segB = joint;
        if (b.parentId != kNoParent) {
            if (const Bone* p = a->skeleton.findById(static_cast<std::uint32_t>(b.parentId))) {
                segA = {p->globalTransform.m[3][0], p->globalTransform.m[3][1],
                        p->globalTransform.m[3][2]};
            }
        }
        const Vec3 e = segB - segA;
        const Vec3 w0 = pNear - segA;
        float d;
        if (dot(e, e) <= 1e-12f) {
            // Root/orphan joint: point-to-ray distance (roots are pickable).
            const float s = std::max(0.0f, dot(dir, joint - pNear));
            d = distance(pNear + dir * s, joint);
        } else {
            const float bb = dot(dir, e);
            const float ee = dot(e, e);
            const float c1 = dot(dir, w0);
            const float c2 = dot(e, w0);
            const float denom = ee - bb * bb;  // |dir| == 1
            float s = 0.0f, t = 0.0f;
            if (denom > 1e-12f) {
                t = (c2 - c1 * bb) / denom;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                s = t * bb - c1;
                if (s < 0.0f) s = 0.0f;
                t = (s * bb + c2) / ee;  // re-solve clamped
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                s = t * bb - c1;
                if (s < 0.0f) s = 0.0f;
            }
            const Vec3 closest = pNear + dir * s;
            const Vec3 onBone = segA + e * t;
            d = distance(closest, onBone);
        }
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(b.id);
        }
    }
    return best;
}

}  // namespace

// Draw the 3D scene contents (mesh, grid, bones, gizmo) to the currently bound
// render target. This is shared between the offscreen viewport path and the
// headless smoke test. Returns the number of draw calls issued.
int drawSceneContents(App& app, Renderer& renderer, const Mat4& viewProj, const ViewportRect& rect,
                      float modelRadius) {
    int drawCalls = 0;
    const float* const cv = theme::viewportClearF();  // single source (ui_model token)
    const float clear[4] = {cv[0], cv[1], cv[2], cv[3]};

    if (app.showGrid) {
        float half = modelRadius * 1.5f;
        if (half < 5.0f) half = 5.0f;
        if (half > 500.0f) half = 500.0f;
        renderer.drawLines(buildGridLines(half, half / 10.0f), viewProj);
        drawCalls++;
    }
    if (LoadedAsset* a = app.currentAsset()) {
        if (auto r = app.refreshGpu(renderer); !r) {
            app.setStatus("GPU upload failed: " + r.error().message, "error");
        } else {
            // G4 routing (pure, pinned by tests/test_app.cpp): resolved from
            // the RAW toggles — Solid/SolidWireframe gating lives inside
            // resolveDrawPath, so fill/pbr below match the old
            // inline conditions exactly (same draws, same call counts).
            // GPU skinning (Wave 24): when a skin stream is paired, the mesh
            // deforms on the GPU from the per-frame palette (bindInverse *
            // currentGlobal) — no CPU re-upload during playback. At bind pose
            // the palette is identity, so static views take the same path.
            const bool skinned = renderer.hasSkinning(a->id);
            const DrawPath path = resolveDrawPath(app.viewMode, app.usePbr, app.textured,
                                                  renderer.hasTexture(a->id), skinned);
            const FillMode fill =
                (path == DrawPath::WireBlinn) ? FillMode::Wireframe : FillMode::Solid;
            // Textured shading is solid-modes-only via the path itself
            // (SolidTextured/PbrTextured): debug ramps (Normals / Height /
            // Weights / UV) stay untextured, never washed out.
            // MSE particle preview shares the per-frame palette; it is built
            // once when either the skinned draws or the MSE tick need it.
            const bool mseWanted =
                app.uiSettings.mseTabEnabled && g_msePreview && !g_mse.doc.attachments.empty();
            const bool mseTickNow = mseWanted && g_msePlaying;
            std::vector<Mat4> framePalette;
            if (skinned || mseTickNow)
                framePalette = buildSkinningPalette(a->skeleton, a->bindInverse);
            if (skinned) {
                if (app.useDqs) {
                    // GPU DQS: dual-quat palette (bindInverse * global as dual
                    // quats). Same skin stream as LBS; only the palette differs.
                    renderer.setDqsSkinningPalette(buildDqsPalette(a->skeleton, a->bindInverse));
                } else {
                    renderer.setSkinningPalette(framePalette);
                }
            }
            // PBR (Wave 25a): Solid/SolidWireframe only — debug ramps stay
            // flat (display-referred) and Wireframe stays Blinn (topology
            // view, not a material view). Derived from the resolved path so
            // the condition cannot drift from the routing table above.
            const bool pbr =
                (path == DrawPath::PbrSolid || path == DrawPath::PbrTextured);
            if (pbr) renderer.setPbrMaterial(a->pbr);
            // Per-material keys <assetId>#mat<i> / <assetId>#nmat<i> are
            // uploaded by refreshGpu. Static textured multi-material meshes
            // route per visible submesh (hasTexture probe + ranged draw,
            // single-texture fallback per range); textured-PBR multi-material
            // meshes do the same with a per-range normal bind (skinned-PBR
            // included via the skinned range variant — the palette is already
            // set above). Every other mode keeps its whole-draw call by
            // design (static ranges on a skinned-Blinn mesh would drop the
            // GPU deform). Single-material meshes always whole-draw
            // (byte-identical behavior and draw-call count).
            // SolidTextured implies useTextured && !pbr on a solid mode, so
            // this matches the old (useTextured && !skinned && !pbr) gate.
            const bool multiMat = (path == DrawPath::SolidTextured) && !skinned &&
                                  meshHasDistinctVisibleMaterials(a->mesh, app.hiddenSubmeshes);
            const bool multiMatPbr =
                (path == DrawPath::PbrTextured) &&
                meshHasDistinctVisibleMaterials(a->mesh, app.hiddenSubmeshes);
            if (multiMat) {
                drawCalls +=
                    drawTexturedSubmeshRanges(renderer, *a, app.hiddenSubmeshes, viewProj, fill);
            } else if (multiMatPbr) {
                drawCalls += drawTexturedPbrSubmeshRanges(renderer, *a, app.hiddenSubmeshes,
                                                          viewProj, fill, skinned);
            } else {
                // Same draws as before the G4 extraction, dispatched on the
                // resolved path (skinned picks the Skinned suffix of the same
                // path; multiMat/multiMatPbr above stay SolidTextured/
                // PbrTextured sub-variants).
                switch (path) {
                    case DrawPath::SolidTextured:
                    case DrawPath::PbrTextured: {
                        // Whole-draw path: bind the primary material's albedo
                        // so single-material meshes with materialIndex != 0 show
                        // the correct texture (not always material 0's).
                        const std::uint32_t primaryMat = primaryMaterialIndex(a->mesh);
                        const std::string primaryMatKey =
                            a->id + "#mat" + std::to_string(primaryMat);
                        renderer.setActiveTexture(
                            renderer.hasTexture(primaryMatKey) ? primaryMatKey : std::string{});
                        if (skinned) {
                            if (pbr) {
                                // Whole-draw normal bind for the draw's
                                // material; missing key binds nothing, so the
                                // no-normal-map frame is call-identical.
                                const bool nBound =
                                    bindPbrNormalForMaterial(renderer, *a, primaryMat);
                                renderer.drawMeshTexturedSkinnedPbr(a->id, viewProj, fill);
                                if (nBound) renderer.clearPbrNormalMap();
                            } else
                                renderer.drawMeshTexturedSkinned(a->id, viewProj, fill);
                        } else {
                            if (pbr) {
                                const bool nBound =
                                    bindPbrNormalForMaterial(renderer, *a, primaryMat);
                                renderer.drawMeshTexturedPbr(a->id, viewProj, fill);
                                if (nBound) renderer.clearPbrNormalMap();
                            } else
                                renderer.drawMeshTextured(a->id, viewProj, fill);
                        }
                        break;
                    }
                    case DrawPath::FlatDebug:
                        // Debug ramps are display-referred: the unlit flat draw keeps
                        // them exact (and legend-consistent) under the linear lit
                        // pipeline instead of washing them through Blinn-Phong.
                        if (skinned)
                            renderer.drawMeshFlatSkinned(a->id, viewProj, fill);
                        else
                            renderer.drawMeshFlat(a->id, viewProj, fill);
                        break;
                    case DrawPath::SolidUntextured:
                    case DrawPath::WireBlinn:
                    case DrawPath::PbrSolid:
                        if (skinned) {
                            if (pbr)
                                renderer.drawMeshSkinnedPbr(a->id, viewProj, fill);
                            else
                                renderer.drawMeshSkinned(a->id, viewProj, fill);
                        } else {
                            if (pbr)
                                renderer.drawMeshPbr(a->id, viewProj, fill);
                            else
                                renderer.drawMesh(a->id, viewProj, fill);
                        }
                        break;
                }
            }
            if (!multiMat && !multiMatPbr) drawCalls++;
            // SolidWireframe mode implies the overlay; the checkbox adds it
            // to every other solid-based mode (drawn once, never stacked).
            if (app.viewMode == ViewMode::SolidWireframe ||
                (app.showWireOverlay && app.viewMode != ViewMode::SolidWireframe &&
                 app.viewMode != ViewMode::Wireframe)) {
                if (skinned)
                    renderer.drawMeshWireOverlaySkinned(a->id, viewProj);
                else
                    renderer.drawMeshWireOverlay(a->id, viewProj);
                drawCalls++;
            }
            if (app.showBones) {
                float worldPerPixel = 0.01f;
                if (rect.h > 0) {
                    if (app.camera.orthographic)
                        worldPerPixel = app.camera.orthoHeight / static_cast<float>(rect.h);
                    else
                        worldPerPixel = 2.0f * app.camera.distance *
                                        std::tan(app.camera.fovY * 0.5f) /
                                        static_cast<float>(rect.h);
                }
                float joint = worldPerPixel * 6.0f;
                const float lo = modelRadius * 0.002f, hi = modelRadius * 0.05f;
                if (joint < lo) joint = lo;
                if (joint > hi) joint = hi;
                // Use app.boneJointSize as base, scale by worldPerPixel
                float jointSize = app.boneJointSize * (joint / 0.03f);
                auto segs = boneSegments(a->skeleton, app, jointSize);
                if (app.xrayBones)
                    renderer.drawLinesXRay(segs, viewProj);
                else
                    renderer.drawLines(segs, viewProj);
                drawCalls++;
            }
            // MSE particle preview (Wave-30b): emission tick + cross draw.
            // Gated by the tab toggle, the in-tab preview checkbox, and a
            // loaded document — the headless smoke path never loads one, so no
            // ImGui clock is touched there. MseRuntime::update is
            // emission-stateless (it rebuilds instances from dt each call), so
            // Pause freezes the last instances and scrubbing the time slider
            // only moves the loop clock below, never history.
            if (mseWanted) {
                if (mseTickNow) {
                    float mseDt = ImGui::GetIO().DeltaTime;
                    if (mseDt < 0.0f) mseDt = 0.0f;
                    if (mseDt > 0.1f) mseDt = 0.1f;  // hitch-proof: no emission burst on resume
                    g_mseTime += mseDt;
                    // Global loop wrap (per-emitter loop flags approximated as
                    // loop; documented, not hidden).
                    if (g_mseTime < 0.0f) g_mseTime = 0.0f;
                    if (g_mseTime >= g_mseDuration) g_mseTime = 0.0f;
                    g_mse.update(mseDt, a->skeleton, framePalette);
                }
                const std::vector<ParticleOverlay> mseParts = g_mse.getActiveParticles();
                // Honest cap: at most 200 particles (600 segments); emitters
                // beyond the cap are dropped tail-first, never sampled.
                constexpr std::size_t kMseParticleCap = 200;
                const std::size_t mseCount =
                    mseParts.size() < kMseParticleCap ? mseParts.size() : kMseParticleCap;
                if (mseCount > 0) {
                    std::vector<GpuVertex> mseSegs;
                    mseSegs.reserve(mseCount * 6);
                    for (std::size_t pi = 0; pi < mseCount; ++pi) {
                        const ParticleOverlay& part = mseParts[pi];
                        float half = (std::isfinite(part.size) && part.size > 0.0f)
                                         ? part.size * 0.5f
                                         : modelRadius * 0.005f;
                        const float halfMax = modelRadius > 0.0f ? modelRadius * 0.05f : 1.0f;
                        if (half > halfMax) half = halfMax;
                        const Vec3 ticks[3] = {{half, 0.0f, 0.0f},
                                               {0.0f, half, 0.0f},
                                               {0.0f, 0.0f, half}};
                        for (int t = 0; t < 3; ++t) {
                            GpuVertex v0{}, v1{};
                            v0.position = part.worldPos - ticks[t];
                            v1.position = part.worldPos + ticks[t];
                            v0.normal = v1.normal = {0.0f, 1.0f, 0.0f};
                            v0.color[0] = v1.color[0] = part.color.x;
                            v0.color[1] = v1.color[1] = part.color.y;
                            v0.color[2] = v1.color[2] = part.color.z;
                            v0.color[3] = v1.color[3] = 1.0f;
                            mseSegs.push_back(v0);
                            mseSegs.push_back(v1);
                        }
                    }
                    // Depth-tested lines: particles hide honestly behind the mesh.
                    renderer.drawLines(mseSegs, viewProj);
                    drawCalls++;
                }
            }
        }
    }
    return drawCalls;
}

// Viewport: dock-proof offscreen rendering (Wave 21). The 3D scene renders
// into an offscreen D3D11 texture sized to the panel rect, composited via
// ImGui::Image(). This survives the occupied-central-node opaque dock
// background that hid the old backbuffer-direct scene (imgui.cpp:19577-19580
// draws full WindowBg when the central node is occupied, so NoBackground on
// the window alone was necessary but not sufficient). Camera input handled here.
ViewportRect drawViewportPanel(App& app, Renderer& renderer) {
    ViewportRect rect;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 240), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Viewport", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse)) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();

        // Ensure offscreen render target matches the panel size (physical pixels)
        const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
        const float sx = ImGui::GetIO().DisplayFramebufferScale.x;
        const float sy = ImGui::GetIO().DisplayFramebufferScale.y;
        
        // Update camera DPI scale for correct coordinate mapping
        app.camera.setDpiScale(sx, sy);
        
        rect.x = static_cast<int>((cursor.x - vpPos.x) * sx);
        rect.y = static_cast<int>((cursor.y - vpPos.y) * sy);
        rect.w = static_cast<int>(avail.x * sx);
        rect.h = static_cast<int>(avail.y * sy);
        rect.valid = rect.w > 8 && rect.h > 8;

        // Input handling (must happen before rendering so camera is up to date)
        // SetNextItemAllowOverlap lets overlay buttons (Frame, Ortho/Persp, Front,
        // Back, Top, Bottom, Left, Right, Shading) receive hover/click instead of
        // being blocked by the full-viewport invisible button. When the mouse is
        // over an overlay button, IsItemHovered() on the invisible button returns
        // false, so btnDown is not set and no orbit drag starts — the arbitration
        // is automatic.
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("viewport_input", avail,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();
        bool gizmoUsing = false;
        bool gizmoOver = false;
        #ifdef M2RIG_WITH_GIZMO
        // Pass physical pixel rect for gizmo (matches offscreen render target)
        ImVec2 physCursor = ImVec2(cursor.x * sx, cursor.y * sy);
        ImVec2 physAvail = ImVec2(avail.x * sx, avail.y * sy);
        updateBoneGizmo(app, physCursor, physAvail, gizmoUsing, gizmoOver);
        #endif
        static bool btnDown = false;
        static ImVec2 downAt{0, 0};
        // Shading popover open: viewport gestures defer to the popup (same
        // arbitration spirit as the Wave-23 overlay fix — a press starting on
        // the Shading button or inside the popup must not orbit, pan, paint,
        // zoom, or click-select; reset/completion paths below are untouched).
        // cmd_palette defers exactly like shadingOpen: a press starting in the
        // palette must not orbit, pan, paint, zoom, or click-select (one
        // extended guard expression; all !shadingOpen uses below are untouched).
        const bool cmdPaletteOpen = ImGui::IsPopupOpen("cmd_palette", ImGuiPopupFlags_None);
        const bool shadingOpen = ImGui::IsPopupOpen("viewport_shading_pop", ImGuiPopupFlags_None) ||
                                 cmdPaletteOpen;
        // Track if mouse is over viewport rect (including overlay area) for drag continuity
        const bool mouseOverViewport = hovered || (io.MouseDown[0] && btnDown && !gizmoUsing);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            btnDown = true;
            downAt = io.MousePos;
            // Start box select on Shift+Click (without Ctrl) when not painting
            if (io.KeyShift && !io.KeyCtrl && !app.paintMode && !gizmoUsing && !gizmoOver) {
                app.boxSelecting = true;
                app.boxSelectStart = {io.MousePos.x, io.MousePos.y};
                app.boxSelectEnd = {io.MousePos.x, io.MousePos.y};
            }
        }
        if (mouseOverViewport) {
            // Update box select
            if (app.boxSelecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                app.boxSelectEnd = {io.MousePos.x, io.MousePos.y};
            }
            const bool paintDrag =
                app.paintMode && app.selectedBone >= 0 && !gizmoUsing && !shadingOpen &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left) && (io.KeyCtrl || io.KeyShift);
            const bool orbitDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !paintDrag &&
                                   !gizmoUsing && !io.KeyCtrl && !io.KeyShift && !app.boxSelecting &&
                                   !shadingOpen;
            if (paintDrag) {
                Vec3 hit;
                if (pickMeshPoint(app, avail.x, avail.y, io.MousePos.x - cursor.x,
                                  io.MousePos.y - cursor.y, hit))
                    app.paintStroke(hit);
            } else if (orbitDrag)
                app.camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
            else if ((ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                      ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) &&
                     !gizmoUsing && !shadingOpen)
                app.camera.pan(io.MouseDelta.x, io.MouseDelta.y);
            if (io.MouseWheel != 0.0f && !gizmoUsing && !shadingOpen) app.camera.zoom(io.MouseWheel);
            if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput && !gizmoUsing) {
                frameWholeModel(app, false);
            }
            const bool anyDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                                 ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
            // Check if mouse is within viewport logical rect for hover highlight
            const bool mouseInViewport = io.MousePos.x >= cursor.x && io.MousePos.x <= cursor.x + avail.x &&
                                         io.MousePos.y >= cursor.y && io.MousePos.y <= cursor.y + avail.y;
            if (!anyDrag && !btnDown && !gizmoOver && !gizmoUsing && !app.boxSelecting && mouseInViewport) {
                app.hoveredBone = pickBoneAt(app, avail.x, avail.y, io.MousePos.x - cursor.x,
                                             io.MousePos.y - cursor.y);
                if (app.hoveredBone >= 0) {
                    if (const LoadedAsset* ha = app.currentAsset()) {
                        if (const Bone* hb = ha->skeleton.findById(
                                static_cast<std::uint32_t>(app.hoveredBone))) {
                            char tip[256];
                            std::snprintf(tip, sizeof(tip), "%s  [id %u]%s", hb->name.c_str(),
                                          hb->id,
                                          app.isBoneLocked(hb->id) ? "  [LOCKED]" : "");
                            ImGui::SetTooltip("%s", tip);
                        }
                    }
                }
            } else {
                app.hoveredBone = -1;
            }
        }
        // Viewport click-select (was documented but unhandled; btnDown/downAt
        // were dead state). Release with <6px drag, no gizmo, no Ctrl/Shift
        // promotes the hovered bone to selection (real core state, gpuDirty).
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (btnDown) {
                const float rdx = io.MousePos.x - downAt.x;
                const float rdy = io.MousePos.y - downAt.y;
                const bool click = (rdx * rdx + rdy * rdy) < 36.0f;
                if (click && mouseOverViewport && !gizmoUsing && !gizmoOver && !io.KeyShift && !app.boxSelecting && !shadingOpen) {
                    const int hit = pickBoneAt(app, avail.x, avail.y,
                                               io.MousePos.x - cursor.x, io.MousePos.y - cursor.y);
                    if (hit >= 0) {
                        // Ctrl+click toggles multi-select, plain click replaces.
                        const bool additive = io.KeyCtrl != 0;
                        if (additive && app.isBoneSelected(static_cast<std::uint32_t>(hit)) &&
                            app.selectedBones.size() > 1) {
                            app.selectedBones.erase(static_cast<std::uint32_t>(hit));
                            app.selectedBone = static_cast<int>(*app.selectedBones.rbegin());
                        } else {
                            app.selectBone(static_cast<std::uint32_t>(hit), additive);
                        }
                        if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
                    } else if (!io.KeyCtrl) {
                        app.clearBoneSelection();
                    }
                }
            }
            // Handle box select completion
            if (app.boxSelecting) {
                app.boxSelecting = false;
                if (LoadedAsset* a = app.currentAsset()) {
                    // Compute screen-space box
                    const float minX = std::min(app.boxSelectStart.x, app.boxSelectEnd.x);
                    const float maxX = std::max(app.boxSelectStart.x, app.boxSelectEnd.x);
                    const float minY = std::min(app.boxSelectStart.y, app.boxSelectEnd.y);
                    const float maxY = std::max(app.boxSelectStart.y, app.boxSelectEnd.y);
                    
                    // Use logical coordinates for aspect ratio to match viewport
                    const float aspect = avail.x / avail.y;
                    const Mat4 vp = app.camera.viewMatrix() * app.camera.projMatrix(aspect);
                    
// Find all bones within the box
                    std::vector<std::uint32_t> selectedInBox;
                    for (const auto& b : a->skeleton.bones) {
                        if (!app.boneVisible(b.id)) continue;
                        
                        const Vec3 joint{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                                         b.globalTransform.m[3][2]};
                        
                        // Project to screen space
                        const Vec4 clip = {
                            joint.x * vp.m[0][0] + joint.y * vp.m[1][0] + joint.z * vp.m[2][0] + vp.m[3][0],
                            joint.x * vp.m[0][1] + joint.y * vp.m[1][1] + joint.z * vp.m[2][1] + vp.m[3][1],
                            joint.x * vp.m[0][2] + joint.y * vp.m[1][2] + joint.z * vp.m[2][2] + vp.m[3][2],
                            joint.x * vp.m[0][3] + joint.y * vp.m[1][3] + joint.z * vp.m[2][3] + vp.m[3][3]
                        };
                        
                        if (clip.w <= 0.0f) continue;
                        
                        const float ndcX = clip.x / clip.w;
                        const float ndcY = clip.y / clip.w;
                        const float ndcZ = clip.z / clip.w;
                        
                        if (ndcZ < 0.0f || ndcZ > 1.0f) continue;
                        
                        const float screenX = cursor.x + (ndcX * 0.5f + 0.5f) * avail.x;
                        const float screenY = cursor.y + (1.0f - (ndcY * 0.5f + 0.5f)) * avail.y;
                        
                        if (screenX >= minX && screenX <= maxX && screenY >= minY && screenY <= maxY) {
                            selectedInBox.push_back(b.id);
                        }
                    }
                    
                    if (!selectedInBox.empty()) {
                        const bool additive = io.KeyCtrl != 0;
                        if (!additive) {
                            app.clearBoneSelection();
                        }
                        for (std::uint32_t boneId : selectedInBox) {
                            app.selectBone(boneId, true);
                        }
                        if (!selectedInBox.empty()) {
                            app.selectedBone = static_cast<int>(selectedInBox.back());
                        }
                        a->gpuDirty = true;
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "Box selected %zu bones.", selectedInBox.size());
                        app.setStatus(buf, "success");
                    }
                }
            }
            btnDown = false;
        }
        if (!mouseOverViewport && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) btnDown = false;

        // Update camera smooth interpolation
        app.camera.updateSmooth(ImGui::GetTime());
        
        // Render 3D scene to offscreen texture if viewport is valid
        const float* const cv = theme::viewportClearF();  // single source (ui_model token)
        const float clear[4] = {cv[0], cv[1], cv[2], cv[3]};
        if (rect.valid) {
            std::string err;
            if (renderer.ensureViewportTarget(rect.w, rect.h, err)) {
                if (renderer.beginViewportPass(clear)) {
                    // Use logical coordinates for aspect ratio to avoid stretch on multi-DPI
                    const float aspect = avail.x / avail.y;
                    const Mat4 view = app.camera.viewMatrix();
                    renderer.setSceneView(view);
                    const Mat4 vp = view * app.camera.projMatrix(aspect);

                    // Compute model radius for adaptive grid/joint sizing
                    float modelRadius = 2.8f;
                    if (const LoadedAsset* ga = app.currentAsset()) {
                        if (!ga->mesh.bounds.empty) {
                            const float r = ga->mesh.bounds.radius();
                            if (r > 0.01f) modelRadius = r;
                        }
                    }

                    drawSceneContents(app, renderer, vp, rect, modelRadius);
                    renderer.endViewportPass();
                }
            } else {
                app.setStatus("Viewport target failed: " + err, "error");
            }
        }

        // Display the offscreen render target via ImGui::Image()
        // D3D11 texture origin is top-left (same as ImGui), so UV (0,0)->(1,1) is correct.
        // No V-flip needed (that was an OpenGL convention).
        if (rect.valid && renderer.viewportSrv()) {
            ImGui::GetWindowDrawList()->AddImage(
                renderer.viewportSrv(),
                cursor,
                ImVec2(cursor.x + avail.x, cursor.y + avail.y),
                ImVec2(0, 0), ImVec2(1, 1)
            );
        }

        // Render bone labels as ImGui overlay (world to screen projection)
        LoadedAsset* asset = app.currentAsset();
        if (app.showBones && app.boneShowLabels && rect.valid && asset) {
            // Use logical coordinates for aspect ratio to match viewport
            const float aspect = avail.x / avail.y;
            const Mat4 vp = app.camera.viewMatrix() * app.camera.projMatrix(aspect);
            
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            for (const auto& b : asset->skeleton.bones) {
                if (!app.boneVisible(b.id)) continue;
                
                const Vec3 joint{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                                 b.globalTransform.m[3][2]};
                
                // Project to screen space
                const Vec4 clip = {
                    joint.x * vp.m[0][0] + joint.y * vp.m[1][0] + joint.z * vp.m[2][0] + vp.m[3][0],
                    joint.x * vp.m[0][1] + joint.y * vp.m[1][1] + joint.z * vp.m[2][1] + vp.m[3][1],
                    joint.x * vp.m[0][2] + joint.y * vp.m[1][2] + joint.z * vp.m[2][2] + vp.m[3][2],
                    joint.x * vp.m[0][3] + joint.y * vp.m[1][3] + joint.z * vp.m[2][3] + vp.m[3][3]
                };
                
                if (clip.w <= 0.0f) continue; // Behind camera
                
                const float ndcX = clip.x / clip.w;
                const float ndcY = clip.y / clip.w;
                const float ndcZ = clip.z / clip.w;
                
                if (ndcZ < 0.0f || ndcZ > 1.0f) continue; // Outside near/far
                
                const float screenX = cursor.x + (ndcX * 0.5f + 0.5f) * avail.x;
                const float screenY = cursor.y + (1.0f - (ndcY * 0.5f + 0.5f)) * avail.y;
                
                // Check if on screen
                if (screenX < cursor.x || screenX > cursor.x + avail.x ||
                    screenY < cursor.y || screenY > cursor.y + avail.y) continue;
                
                // Determine label color based on bone state
                ImU32 labelCol = IM_COL32(230, 230, 240, 255);
                if (app.isBoneSelected(b.id) || static_cast<int>(b.id) == app.selectedBone) {
                    labelCol = IM_COL32(255, 217, 51, 255);
                } else if (static_cast<int>(b.id) == app.hoveredBone) {
                    labelCol = IM_COL32(255, 128, 25, 255);
                } else if (app.isBoneLocked(b.id)) {
                    labelCol = IM_COL32(128, 128, 128, 255);
                }
                
                // Draw label with background for readability
                const char* label = b.name.c_str();
                const ImVec2 textSize = ImGui::CalcTextSize(label);
                const float padding = 2.0f;
                const ImVec2 bgMin(screenX - textSize.x * 0.5f - padding, screenY - textSize.y - padding);
                const ImVec2 bgMax(screenX + textSize.x * 0.5f + padding, screenY + padding);
                drawList->AddRectFilled(bgMin, bgMax, IM_COL32(10, 10, 15, 200), 3.0f);
                drawList->AddText(ImVec2(screenX - textSize.x * 0.5f, screenY - textSize.y), labelCol, label);
            }
        }

        // Draw box selection rectangle
        if (app.boxSelecting) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 min(app.boxSelectStart.x, app.boxSelectStart.y);
            const ImVec2 max(app.boxSelectEnd.x, app.boxSelectEnd.y);
            const ImVec2 rectMin(std::min(min.x, max.x), std::min(min.y, max.y));
            const ImVec2 rectMax(std::max(min.x, max.x), std::max(min.y, max.y));
            drawList->AddRect(rectMin, rectMax, IM_COL32(80, 170, 255, 255), 0.0f, 0, 2.0f);
            drawList->AddRectFilled(rectMin, rectMax, IM_COL32(80, 170, 255, 50));
        }

        // Overlay controls.
        ImGui::SetCursorScreenPos(cursor + ImVec2(8, 8));
        ImGui::BeginDisabled(app.currentAsset() == nullptr);
        if (ImGui::Button("Frame (F)")) {
            frameWholeModel(app, true);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fit the whole model in view (F)");
        ImGui::SameLine();
        const char* proj = app.camera.orthographic ? "Ortho -> Persp" : "Persp -> Ortho";
        if (ImGui::Button(proj)) app.camera.setOrthographic(!app.camera.orthographic, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle perspective / orthographic");
        ImGui::SameLine();
        if (ImGui::Button("Front")) app.camera.applyPreset(m2rig::CameraPreset::Front, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to front (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Back")) app.camera.applyPreset(m2rig::CameraPreset::Back, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to back (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Top")) app.camera.applyPreset(m2rig::CameraPreset::Top, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to top (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Bottom")) app.camera.applyPreset(m2rig::CameraPreset::Bottom, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to bottom (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Left")) app.camera.applyPreset(m2rig::CameraPreset::Left, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to left (keeps framing)");
        ImGui::SameLine();
        if (ImGui::Button("Right")) app.camera.applyPreset(m2rig::CameraPreset::Right, ImGui::GetTime());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap camera to right (keeps framing)");
        // Two-row wrap when narrow: presets stay on row 1, Shading drops to
        // row 2 so the 9-button row never clips at 720p widths.
        if (avail.x < 720.0f) {
            ImGui::SetCursorScreenPos(cursor + ImVec2(8, 8 + ImGui::GetFrameHeight() + 4.0f));
        } else {
            ImGui::SameLine();
        }
        // Shading popover: the same view-mode + overlay switches as the
        // toolbar, where viewport-focused users look (single bools, no
        // duplicate state — Blender-style header pattern).
        if (ImGui::Button("Shading")) ImGui::OpenPopup("viewport_shading_pop");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shading mode + overlays");
        if (ImGui::BeginPopup("viewport_shading_pop")) {
            // Scrollable body (max ~300px) so the popover never runs off
            // screen at 720p. Popup ID + shadingOpen guard above unchanged.
            if (ImGui::BeginChild("viewport_shading_scroll", ImVec2(0, 300.0f), true)) {
                const char* modes[] = {"Solid",     "Wireframe", "Solid + Wire", "Normals",
                                       "Height",    "Weights",   "UV"};
                for (int i = 0; i < 7; ++i) {
                    if (ImGui::RadioButton(modes[i], app.viewMode == static_cast<ViewMode>(i))) {
                        app.viewMode = static_cast<ViewMode>(i);
                        if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                        if (i == 5 && app.selectedBone < 0)
                            app.setStatus("Weights view needs a selected bone.", "warning");
                    }
                }
                ImGui::Separator();
                ImGui::Checkbox(labelXray(), &app.xrayBones);
                if (ImGui::Checkbox("Textured", &app.textured)) {
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
                ImGui::Checkbox("PBR shading", &app.usePbr);
            ImGui::Checkbox("Wire overlay", &app.showWireOverlay);
            ImGui::Checkbox("Grid", &app.showGrid);
            ImGui::Checkbox("Bones", &app.showBones);
            ImGui::Separator();
            if (ImGui::Checkbox("Paint mode", &app.paintMode)) {
                if (app.paintMode && app.selectedBone < 0)
                    app.setStatus("Paint mode: select a bone first (click in Skeleton or viewport)",
                                  "warning");
            }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        // Blender-style gizmo op indicator (switched live by the W/E/R hotkeys).
        // Only meaningful with an asset and a selected bone — hidden otherwise.
        if (app.currentAsset() != nullptr && app.selectedBone >= 0) {
            ImGui::SameLine();
            ImGui::Text("Gizmo: %s", app.gizmoOp == GizmoOp::Translate ? "Move (W)" :
                                       app.gizmoOp == GizmoOp::Rotate ? "Rotate (E)" : "Scale (R)");
        }

        if (const LoadedAsset* a = app.currentAsset()) {
            ImVec2 overlay = cursor + ImVec2(8, avail.y - 44);
            ImGui::SetCursorScreenPos(overlay);
            ImGui::TextDisabled("%s | %zu v / %zu t | bone %d%s", a->id.c_str(),
                                a->mesh.vertices.size(), a->mesh.triangleCount(),
                                app.selectedBone, app.paintMode ? " | PAINT (Ctrl+drag)" : "");
            ImVec2 diag = cursor + ImVec2(8, avail.y - 24);
            ImGui::SetCursorScreenPos(diag);
            const Vec3 eye = app.camera.eye();
            ImGui::TextDisabled("view %dx%d%s | eye (%.1f,%.1f,%.1f) d=%.1f | grid %d bones %d",
                                rect.w, rect.h, rect.valid ? "" : " too small",
                                eye.x, eye.y, eye.z, app.camera.distance,
                                app.showGrid ? 1 : 0, app.showBones ? 1 : 0);
        } else {
            // Measured centering (no fixed -150px offsets): each line is
            // centered via CalcTextSize, with PushTextWrapPos so long lines
            // wrap inside the viewport instead of clipping. Same strings.
            const ImVec2 center = cursor + ImVec2(avail.x * 0.5f, avail.y * 0.45f);
            const float wrapX = cursor.x + avail.x - 16.0f;
            auto centeredLine = [&](const char* text, float yOff, bool disabled) {
                const ImVec2 sz = ImGui::CalcTextSize(text);
                float x = center.x - sz.x * 0.5f;
                if (x < cursor.x + 8.0f) x = cursor.x + 8.0f;
                ImGui::SetCursorScreenPos(ImVec2(x, center.y + yOff));
                ImGui::PushTextWrapPos(wrapX);
                if (disabled)
                    ImGui::TextDisabled("%s", text);
                else
                    ImGui::Text("%s", text);
                ImGui::PopTextWrapPos();
            };
            centeredLine("No model loaded", -40.0f, false);
            centeredLine("1. Project > Import FBX/GR2  or  Project > Load sample armor", -20.0f,
                         true);
            centeredLine("2. Load a model (Project > Import), then press F to frame it", -4.0f, true);
            centeredLine("Drag = orbit | Right-drag = pan | Wheel = zoom", 12.0f, true);
            ImVec2 overlay = cursor + ImVec2(8, avail.y - 44);
            ImGui::SetCursorScreenPos(overlay);
            ImGui::TextDisabled("No asset loaded — Project > Load sample armor");
            ImVec2 diag = cursor + ImVec2(8, avail.y - 24);
            ImGui::SetCursorScreenPos(diag);
            ImGui::TextDisabled("view %dx%d%s | grid %d", rect.w, rect.h,
                                rect.valid ? "" : " too small", app.showGrid ? 1 : 0);
        }
        if (!rect.valid) {
            ImVec2 warn = cursor + ImVec2(8, 32);
            ImGui::SetCursorScreenPos(warn);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "Viewport too small — enlarge the Viewport panel");
        }
        // Paint brush circle overlay.
        if (app.paintMode && hovered && avail.y > 0) {
            const float eyeDist = distance(app.camera.eye(), app.camera.target);
            float worldPerPixel = 0.01f;
            if (app.camera.orthographic)
                worldPerPixel = app.camera.orthoHeight / avail.y;
            else
                worldPerPixel = 2.0f * eyeDist * std::tan(app.camera.fovY * 0.5f) / avail.y;
            if (worldPerPixel > 1e-9f) {
                const float r = app.brushRadius / worldPerPixel;
                const ImU32 ringCol = app.selectedBone >= 0 ? IM_COL32(80, 170, 255, 255)
                                                            : IM_COL32(255, 90, 70, 255);
                ImGui::GetWindowDrawList()->AddCircle(ImGui::GetIO().MousePos, r, ringCol, 40,
                                                      2.0f);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return rect;
}

}  // namespace m2rig
