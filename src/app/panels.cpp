// Dear ImGui panels: toolbar, asset/scene/skeleton, viewport, properties,
// weights, materials, validation, console, project, export, timeline.
// Wave 34 split: panel bodies live in src/app/panels_*.cpp (contract in
// panels_internal.hpp). This file keeps the shared state/helpers and the
// dock/menu orchestration (buildDefaultDockLayout, renderScene,
// drawAllPanels).
#include "panels_internal.hpp"

namespace m2rig {

// --- shared state (extern-declared in panels_internal.hpp) -------------
// Live dock layout: file-scope (NOT function-static) on purpose - both
// Reset paths must force a rebuild without restart. The 14 App::UISettings
// panel flags are live-read every frame in drawAllPanels, so presets apply
// instantly; the dock *structure* rebuilds via buildDefaultDockLayout()
// (declared below, defined just before drawAllPanels). Viewport
// compositing (ImGuiWindowFlags_NoBackground + offscreen texture in
// drawViewportPanel) is untouched by design.
void* g_mainWindow = nullptr;
bool g_dockBuilt = false;
void requestDockRebuild() { g_dockBuilt = false; }
void markDockBuilt() { g_dockBuilt = true; }

void setMainWindowHandle(void* hwnd) { g_mainWindow = hwnd; }

// --- shared helpers (declared in panels_internal.hpp) ------------------
std::vector<GpuVertex> boneSegments(const Skeleton& skel, const App& app,
                                    float jointSize) {
    std::vector<GpuVertex> segs;
    auto colorFor = [&](const Bone& b) -> Vec3 {
        if (app.isBoneSelected(b.id) || static_cast<int>(b.id) == app.selectedBone)
            return {app.boneSelectedColor[0], app.boneSelectedColor[1], app.boneSelectedColor[2]};
        if (static_cast<int>(b.id) == app.hoveredBone) return {app.boneHoveredColor[0], app.boneHoveredColor[1], app.boneHoveredColor[2]};
        if (app.isBoneLocked(b.id)) return {app.boneLockedColor[0], app.boneLockedColor[1], app.boneLockedColor[2]};
        if (!app.boneVisible(b.id)) return {app.boneHiddenColor[0], app.boneHiddenColor[1], app.boneHiddenColor[2]};
        // Check if bone is a parent of selected bone
        if (app.selectedBone >= 0) {
            const Bone* selBone = skel.findById(static_cast<std::uint32_t>(app.selectedBone));
            if (selBone) {
                // Check if this bone is a parent of selected
                std::uint32_t parentId = selBone->parentId;
                while (parentId != kNoParent) {
                    if (parentId == b.id) {
                        return {app.boneParentColor[0], app.boneParentColor[1], app.boneParentColor[2]};
                    }
                    const Bone* pb = skel.findById(parentId);
                    if (!pb) break;
                    parentId = pb->parentId;
                }
                // Check if this bone is a child of selected
                for (std::uint32_t childId : selBone->children) {
                    if (childId == b.id) {
                        return {app.boneChildColor[0], app.boneChildColor[1], app.boneChildColor[2]};
                    }
                }
            }
        }
        if (b.name == "equip_left" || b.name == "equip_right" || b.name == "stip")
            return {app.boneSocketColor[0], app.boneSocketColor[1], app.boneSocketColor[2]};
        return {app.boneDefaultColor[0], app.boneDefaultColor[1], app.boneDefaultColor[2]};
    };
    for (const auto& b : skel.bones) {
        if (!app.boneVisible(b.id)) continue;
        const Vec3 c{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                     b.globalTransform.m[3][2]};
        const Vec3 col = colorFor(b);
        GpuVertex v0{};
        v0.position = c;
        v0.normal = {0, 1, 0};
        v0.color[0] = col.x;
        v0.color[1] = col.y;
        v0.color[2] = col.z;
        v0.color[3] = 1.0f;
        // Root bones (and orphans) have no parent segment, but they still get
        // a joint marker so a root-only skeleton is never invisible.
        if (b.parentId != kNoParent) {
            if (const Bone* p = skel.findById(static_cast<std::uint32_t>(b.parentId))) {
                if (!app.boneVisible(static_cast<std::uint32_t>(b.parentId))) {
                    // Hidden parent: joint marker only, no segment.
                } else {
                    GpuVertex v1 = v0;
                    v1.position = {p->globalTransform.m[3][0], p->globalTransform.m[3][1],
                                   p->globalTransform.m[3][2]};
                    segs.push_back(v1);
                    segs.push_back(v0);
                }
            }
        }
        // Joint cross (X/Y/Z ticks) so joints read at any angle.
        if (app.boneShowJointCrosses) {
            // jointSize is screen-aware (see renderScene); default fits the sample armor.
            const float t = jointSize > 1e-9f ? jointSize : app.boneJointSize;
            const Vec3 axes[3] = {{t, 0, 0}, {0, t, 0}, {0, 0, t}};
            for (const Vec3& ax : axes) {
                GpuVertex j0 = v0, j1 = v0;
                j0.position = c + ax * -1.0f;
                j1.position = c + ax;
                segs.push_back(j0);
                segs.push_back(j1);
            }
        }
        // Bone label (rendered separately via ImGui overlay)
    }
    return segs;
}

void drawSkeletonTree(App& app, std::int32_t boneId, int depth) {
    LoadedAsset* asset = app.currentAsset();
    if (!asset) return;
    const Bone* b = asset->skeleton.findById(static_cast<std::uint32_t>(boneId));
    if (!b) return;
    if (depth == 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (b->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (app.isBoneSelected(b->id) || app.selectedBone == boneId)
        flags |= ImGuiTreeNodeFlags_Selected;
    char label[256];
    std::snprintf(label, sizeof(label), "%s%s%s  [%zu]", app.isBoneLocked(b->id) ? "[L] " : "",
                  app.boneVisible(b->id) ? "" : "[H] ", b->name.c_str(),
                  app.boneInfluenceCount(b->id));
    const bool open = ImGui::TreeNodeEx(label, flags);
    if (ImGui::IsItemClicked()) {
        // Plain click replaces, Ctrl+click toggles into the multi-select.
        const bool additive = ImGui::GetIO().KeyCtrl != 0;
        if (additive && app.isBoneSelected(b->id) && app.selectedBones.size() > 1) {
            app.selectedBones.erase(b->id);
            app.selectedBone = static_cast<int>(*app.selectedBones.rbegin());
        } else {
            app.selectBone(b->id, additive);
        }
        if (LoadedAsset* sa = app.currentAsset()) sa->gpuDirty = true;
    }
    if (open) {
        for (std::uint32_t child : b->children)
            drawSkeletonTree(app, static_cast<std::int32_t>(child), depth + 1);
        ImGui::TreePop();
    }
}

// --- panels --------------------------------------------------------------

#ifdef M2RIG_WITH_GIZMO
// Bone manipulator for the selected bone (WORLD space). Runs whenever the
// viewport panel has an area — deliberately NOT gated on hover: hover-gating
// made the gizmo vanish under popups/menus and froze the drag-end
// bookkeeping (undo snapshot, validation, status) when the pointer left the
// panel mid-drag. Input arbitration (orbit/pick/paint defer to the gizmo)
// still uses the IsOver/IsUsing outputs.
void updateBoneGizmo(App& app, const ImVec2& cursor, const ImVec2& avail, bool& gizmoUsing,
                     bool& gizmoOver) {
    static bool wasUsing = false;
    static bool wasBlocked = false;
    if (avail.x <= 0.0f || avail.y <= 0.0f) {
        // Degenerate panel (docking transition): never feed ImGuizmo a
        // zero-size rect (aspect 0 -> infinite projection).
        gizmoUsing = false;
        gizmoOver = false;
        wasUsing = false;
        wasBlocked = false;
        return;
    }
    LoadedAsset* ga = app.currentAsset();
    Bone* mb = (ga && app.selectedBone >= 0)
                   ? ga->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone))
                   : nullptr;
    // Bulk gizmo: when on and >1 bone is selected, operate on the whole
    // selection (median pivot) instead of just the primary bone.
    const bool bulk = app.gizmoApplyToSelection && app.selectedBones.size() > 1;
    if (!mb && !bulk) {
        wasUsing = false;
        wasBlocked = false;
        return;
    }
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(cursor.x, cursor.y, avail.x, avail.y);
    ImGuizmo::SetOrthographic(app.camera.orthographic);
    const bool parentSpace = app.gizmoSpace == GizmoSpace::Parent;
    Mat4 parentG = Mat4::identity();
    Mat4 drawBefore;
    if (bulk) {
        // Multi-bone: pivot at the median of the selected bones' world positions.
        auto pivot = computeSelectionPivot(ga->skeleton, app.selectedBones);
        if (!pivot) {
            wasUsing = false;
            wasBlocked = false;
            return;
        }
        if (mb && mb->parentId != kNoParent) {
            if (const Bone* pb = ga->skeleton.findById(static_cast<std::uint32_t>(mb->parentId)))
                parentG = pb->globalTransform;
        }
        if (parentSpace) {
            drawBefore = parentAlignedDrawMatrix(parentG, *pivot);
        } else if (app.gizmoSpace == GizmoSpace::Local) {
            // Primary bone's world orientation relocated to the pivot.
            drawBefore = mb ? mb->globalTransform : Mat4::identity();
            drawBefore.m[3][0] = pivot->x;
            drawBefore.m[3][1] = pivot->y;
            drawBefore.m[3][2] = pivot->z;
        } else {
            drawBefore = Mat4::identity();
            drawBefore.m[3][0] = pivot->x;
            drawBefore.m[3][1] = pivot->y;
            drawBefore.m[3][2] = pivot->z;
        }
    } else {
        if (!mb) {
            wasUsing = false;
            wasBlocked = false;
            return;
        }
        if (mb->parentId != kNoParent) {
            if (const Bone* pb = ga->skeleton.findById(static_cast<std::uint32_t>(mb->parentId)))
                parentG = pb->globalTransform;
        }
        const Vec3 boneWorldPos{mb->globalTransform.m[3][0], mb->globalTransform.m[3][1],
                                mb->globalTransform.m[3][2]};
        // Parent space: handles aligned to the parent orientation at the joint
        // (LOCAL mode on a parent-aligned draw matrix). World/Local: the bone's
        // own world matrix in WORLD/LOCAL mode. Output is always a world-space
        // matrix; the core decomposition below maps it back to locals.
        drawBefore =
            parentSpace ? parentAlignedDrawMatrix(parentG, boneWorldPos) : mb->globalTransform;
    }
    const Mat4 vv = app.camera.viewMatrix();
    const Mat4 pp = app.camera.projMatrix(avail.x / avail.y);
    float view[16], proj[16], mtx[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            view[c * 4 + r] = vv.m[r][c];
            proj[c * 4 + r] = pp.m[r][c];
            mtx[c * 4 + r] = drawBefore.m[r][c];
        }
    }
    // Optional step snapping (world units / degrees / scale steps).
    float snapVals[3] = {0, 0, 0};
    float* snapPtr = nullptr;
    if (app.gizmoSnap) {
        if (app.gizmoOp == GizmoOp::Translate) {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapTranslate;
            snapPtr = snapVals;
        } else if (app.gizmoOp == GizmoOp::Rotate) {
            snapVals[0] = app.snapRotateDeg;
            snapPtr = snapVals;
        } else {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapScale;
            snapPtr = snapVals;
        }
    }
    
    // Enable plane handles and center handle for translate/scale
    // ImGuizmo automatically shows plane handles (squares between axes) for translate
    // and the center cube for all operations.
    ImGuizmo::Manipulate(view, proj,
                         app.gizmoOp == GizmoOp::Translate ? ImGuizmo::TRANSLATE
                         : app.gizmoOp == GizmoOp::Rotate  ? ImGuizmo::ROTATE
                                                           : ImGuizmo::SCALE,
                         parentSpace || app.gizmoSpace == GizmoSpace::Local ? ImGuizmo::LOCAL
                                                                            : ImGuizmo::WORLD,
                         mtx, nullptr, snapPtr);
    gizmoUsing = ImGuizmo::IsUsing();
    gizmoOver = ImGuizmo::IsOver();
    const bool locked = mb && app.isBoneLocked(mb->id);
    if (gizmoUsing && !wasUsing) {
        if (locked) {
            wasBlocked = true;
            app.setStatus("Gizmo blocked: bone is locked.", "warning");
        } else {
            wasBlocked = false;
            app.pushUndoSnapshot(bulk ? "gizmo selection" : "gizmo " + (mb ? mb->name : ""));
        }
    }
    if (gizmoUsing) {
        if (locked) {
            // Status already set on drag start; avoid per-frame spam.
        } else {
            Mat4 outW;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) outW.m[r][c] = mtx[c * 4 + r];
            const LocalEditOp leOp = app.gizmoOp == GizmoOp::Rotate ? LocalEditOp::Rotate
                                     : app.gizmoOp == GizmoOp::Scale ? LocalEditOp::Scale
                                                                     : LocalEditOp::Translate;
            if (bulk) {
                // Bulk: world delta from the draw before/after pair, applied to
                // every selected bone through its own parent frame (locked
                // bones skipped inside applyBulkDelta).
                const Mat4 deltaWorld = outW * drawBefore.inverseGeneral();
                const std::size_t edited =
                    applyBulkDelta(ga->skeleton, app.selectedBones, deltaWorld, leOp);
                if (auto rr = rebuildSkeletonRuntime(ga->skeleton); !rr)
                    app.setStatus("Gizmo update failed: " + rr.error().message, "error");
                ga->gpuDirty = true;
                ga->dirty = true;
                (void)edited;
            } else if (mb) {
                const BoneLocalEdit current{mb->localPosition, mb->localRotationEuler,
                                            mb->localScale};
                if (!parentSpace) {
                    // World/Local: decompose the manipulated world matrix through
                    // the single core implementation (shared with tests).
                    const BoneLocalEdit edit = decomposeWorldToLocal(parentG, outW, leOp, current);
                    mb->localPosition = edit.position;
                    mb->localRotationEuler = edit.rotationEuler;
                    mb->localScale = edit.scale;
                } else {
                    // Parent space: unified before/after draw-matrix delta (shared
                    // core, replaces the former per-op ad-hoc branches).
                    const BoneLocalEdit edit =
                        decomposeParentDelta(parentG, drawBefore, outW, leOp, current);
                    mb->localPosition = edit.position;
                    mb->localRotationEuler = edit.rotationEuler;
                    mb->localScale = edit.scale;
                }
                if (auto rr = rebuildSkeletonRuntime(ga->skeleton); !rr)
                    app.setStatus("Gizmo update failed: " + rr.error().message, "error");
                ga->gpuDirty = true;
                ga->dirty = true;
            }
        }
    }
    if (!gizmoUsing && wasUsing) {
        if (wasBlocked) {
            app.setStatus("Gizmo blocked: bone is locked.", "warning");
        } else {
            app.runValidation();
            app.setStatus("Gizmo edit applied.", "success");
        }
        wasBlocked = false;
    }
    wasUsing = gizmoUsing;
}

// Mesh-level manipulator for the selected submesh (WORLD space about the
// submesh centroid). Same ImGuizmo setup as updateBoneGizmo; the drag
// accumulates into submeshTransforms[selectedSubmesh] and is baked into
// mesh.vertices by applySubmeshTransform on drag end (meshBackup undo).
void updateMeshGizmo(App& app, const ImVec2& cursor, const ImVec2& avail, bool& gizmoUsing,
                     bool& gizmoOver) {
    static bool wasUsing = false;
    if (avail.x <= 0.0f || avail.y <= 0.0f) {
        // Degenerate panel (docking transition): never feed ImGuizmo a
        // zero-size rect (same guard as updateBoneGizmo).
        gizmoUsing = false;
        gizmoOver = false;
        wasUsing = false;
        return;
    }
    LoadedAsset* ga = app.currentAsset();
    if (!ga || app.selectedSubmesh < 0 ||
        static_cast<std::size_t>(app.selectedSubmesh) >= ga->mesh.subMeshes.size()) {
        wasUsing = false;
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(app.selectedSubmesh);
    const SubMesh& sm = ga->mesh.subMeshes[idx];
    // Pivot: centroid of the submesh's unique referenced vertices (the index
    // range references shared vertices multiple times; a per-index average
    // would bias the centroid toward them).
    Vec3 pivot{0, 0, 0};
    std::size_t counted = 0;
    std::set<std::uint32_t> visited;
    for (std::size_t i = sm.startIndex; i < sm.startIndex + sm.indexCount && i < ga->mesh.indices.size();
         ++i) {
        const std::uint32_t vi = ga->mesh.indices[i];
        if (vi >= ga->mesh.vertices.size()) continue;
        if (!visited.insert(vi).second) continue;
        pivot += ga->mesh.vertices[vi].position;
        ++counted;
    }
    if (counted > 0) pivot = pivot / static_cast<float>(counted);
    App::SubmeshTransform& tf = app.submeshTransforms[idx];
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(cursor.x, cursor.y, avail.x, avail.y);
    ImGuizmo::SetOrthographic(app.camera.orthographic);
    // Draw matrix: the submesh transform about its centroid (compose order =
    // scale, rotate, translate — the same order applySubmeshTransform bakes).
    const Mat4 drawBefore = Mat4::compose(pivot + tf.position, tf.rotationEuler, tf.scale);
    const Mat4 vv = app.camera.viewMatrix();
    const Mat4 pp = app.camera.projMatrix(avail.x / avail.y);
    float view[16], proj[16], mtx[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            view[c * 4 + r] = vv.m[r][c];
            proj[c * 4 + r] = pp.m[r][c];
            mtx[c * 4 + r] = drawBefore.m[r][c];
        }
    }
    // Optional step snapping (same rules as the bone gizmo).
    float snapVals[3] = {0, 0, 0};
    float* snapPtr = nullptr;
    if (app.gizmoSnap) {
        if (app.gizmoOp == GizmoOp::Translate) {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapTranslate;
            snapPtr = snapVals;
        } else if (app.gizmoOp == GizmoOp::Rotate) {
            snapVals[0] = app.snapRotateDeg;
            snapPtr = snapVals;
        } else {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapScale;
            snapPtr = snapVals;
        }
    }
    ImGuizmo::Manipulate(view, proj,
                         app.gizmoOp == GizmoOp::Translate ? ImGuizmo::TRANSLATE
                         : app.gizmoOp == GizmoOp::Rotate  ? ImGuizmo::ROTATE
                                                           : ImGuizmo::SCALE,
                         ImGuizmo::WORLD, mtx, nullptr, snapPtr);
    gizmoUsing = ImGuizmo::IsUsing();
    gizmoOver = ImGuizmo::IsOver();
    if (gizmoUsing) {
        Mat4 outW;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) outW.m[r][c] = mtx[c * 4 + r];
        // Decompose the draw-matrix delta into the submesh transform (same
        // core decomposition the bone gizmo uses; identity parent = world).
        const LocalEditOp leOp = app.gizmoOp == GizmoOp::Rotate ? LocalEditOp::Rotate
                                 : app.gizmoOp == GizmoOp::Scale ? LocalEditOp::Scale
                                                                 : LocalEditOp::Translate;
        const BoneLocalEdit current{tf.position, tf.rotationEuler, tf.scale};
        const BoneLocalEdit edit =
            decomposeParentDelta(Mat4::identity(), drawBefore, outW, leOp, current);
        if (leOp == LocalEditOp::Translate) {
            // drawBefore translation = pivot + position: recover the offset.
            tf.position = {edit.position.x - pivot.x, edit.position.y - pivot.y,
                           edit.position.z - pivot.z};
        } else if (leOp == LocalEditOp::Rotate) {
            tf.rotationEuler = edit.rotationEuler;
        } else {
            tf.scale = edit.scale;
        }
    }
    if (!gizmoUsing && wasUsing) {
        // Drag end: snapshot the pre-drag mesh (meshBackup undo path), then
        // bake the accumulated transform into the vertices.
        app.pushMeshImportSnapshot("mesh gizmo", ga->mesh);
        app.applySubmeshTransform(idx);
        app.runValidation();
        app.setStatus("Mesh gizmo edit applied.", "success");
    }
    wasUsing = gizmoUsing;
}

// Vertex-level manipulator for the selected vertices (WORLD space about the
// selection centroid). Same ImGuizmo setup as updateMeshGizmo; unlike the
// mesh gizmo the drag updates the selected vertices DIRECTLY (live preview)
// from the drag-start originals, so the stroke never compounds and the handle
// tracks the centroid. The pre-drag mesh is snapshotted at drag start (the
// meshBackup undo path) — it must be captured BEFORE the direct updates
// mutate the vertices, or undo would restore the post-drag state. The
// transform offset resets on drag end so the next drag starts fresh.
void updateVertexGizmo(App& app, const ImVec2& cursor, const ImVec2& avail, bool& gizmoUsing,
                       bool& gizmoOver) {
    static bool wasUsing = false;
    static Vec3 pivot{0, 0, 0};
    static std::map<std::uint32_t, Vec3> originals;
    if (avail.x <= 0.0f || avail.y <= 0.0f) {
        // Degenerate panel (docking transition): never feed ImGuizmo a
        // zero-size rect (same guard as updateBoneGizmo).
        gizmoUsing = false;
        gizmoOver = false;
        wasUsing = false;
        originals.clear();
        app.vertexTransform = App::VertexTransform{};
        return;
    }
    LoadedAsset* ga = app.currentAsset();
    if (!ga || app.editMode != EditMode::Edit || app.selectedVertices.empty()) {
        wasUsing = false;
        return;
    }
    // Count valid selected vertices (MESH_BAD_INDEX guard); bail when none.
    std::size_t valid = 0;
    for (std::uint32_t vi : app.selectedVertices) {
        if (vi < ga->mesh.vertices.size()) ++valid;
    }
    if (valid == 0) {
        wasUsing = false;
        return;
    }
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(cursor.x, cursor.y, avail.x, avail.y);
    ImGuizmo::SetOrthographic(app.camera.orthographic);
    // Draw matrix: the vertex transform about the selection centroid
    // (compose order = scale, rotate, translate — the same order the live
    // update below applies). pivot/originals are captured at drag start and
    // stay fixed for the whole stroke.
    const Mat4 drawBefore = Mat4::compose(pivot + app.vertexTransform.position,
                                          app.vertexTransform.rotationEuler,
                                          app.vertexTransform.scale);
    const Mat4 vv = app.camera.viewMatrix();
    const Mat4 pp = app.camera.projMatrix(avail.x / avail.y);
    float view[16], proj[16], mtx[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            view[c * 4 + r] = vv.m[r][c];
            proj[c * 4 + r] = pp.m[r][c];
            mtx[c * 4 + r] = drawBefore.m[r][c];
        }
    }
    // Optional step snapping (same rules as the bone/mesh gizmos).
    float snapVals[3] = {0, 0, 0};
    float* snapPtr = nullptr;
    if (app.gizmoSnap) {
        if (app.gizmoOp == GizmoOp::Translate) {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapTranslate;
            snapPtr = snapVals;
        } else if (app.gizmoOp == GizmoOp::Rotate) {
            snapVals[0] = app.snapRotateDeg;
            snapPtr = snapVals;
        } else {
            snapVals[0] = snapVals[1] = snapVals[2] = app.snapScale;
            snapPtr = snapVals;
        }
    }
    ImGuizmo::Manipulate(view, proj,
                         app.gizmoOp == GizmoOp::Translate ? ImGuizmo::TRANSLATE
                         : app.gizmoOp == GizmoOp::Rotate  ? ImGuizmo::ROTATE
                                                           : ImGuizmo::SCALE,
                         ImGuizmo::WORLD, mtx, nullptr, snapPtr);
    gizmoUsing = ImGuizmo::IsUsing();
    gizmoOver = ImGuizmo::IsOver();
    if (gizmoUsing && !wasUsing) {
        // Drag start: capture the pivot (centroid) and every selected
        // vertex's original position, then snapshot the pre-drag mesh for
        // undo. The live update below recomputes positions from these
        // originals, so the stroke never compounds.
        pivot = Vec3{0, 0, 0};
        originals.clear();
        for (std::uint32_t vi : app.selectedVertices) {
            if (vi >= ga->mesh.vertices.size()) continue;
            const Vec3& p = ga->mesh.vertices[vi].position;
            originals[vi] = p;
            pivot += p;
        }
        pivot = pivot / static_cast<float>(originals.size());
        app.pushMeshImportSnapshot("vertex gizmo", ga->mesh);
    }
    if (gizmoUsing) {
        Mat4 outW;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) outW.m[r][c] = mtx[c * 4 + r];
        // Decompose the draw-matrix delta into the vertex transform (same
        // core decomposition the mesh gizmo uses; identity parent = world).
        const LocalEditOp leOp = app.gizmoOp == GizmoOp::Rotate ? LocalEditOp::Rotate
                                 : app.gizmoOp == GizmoOp::Scale ? LocalEditOp::Scale
                                                                 : LocalEditOp::Translate;
        const BoneLocalEdit current{app.vertexTransform.position, app.vertexTransform.rotationEuler,
                                    app.vertexTransform.scale};
        const BoneLocalEdit edit =
            decomposeParentDelta(Mat4::identity(), drawBefore, outW, leOp, current);
        if (leOp == LocalEditOp::Translate) {
            // drawBefore translation = pivot + position: recover the offset.
            app.vertexTransform.position = {edit.position.x - pivot.x, edit.position.y - pivot.y,
                                            edit.position.z - pivot.z};
        } else if (leOp == LocalEditOp::Rotate) {
            app.vertexTransform.rotationEuler = edit.rotationEuler;
        } else {
            app.vertexTransform.scale = edit.scale;
        }
        // Live update: outW IS the new draw matrix (compose(pivot + position,
        // rotationEuler, scale) for every op), so each selected vertex is
        // recomputed from its drag-start original through it. The centroid
        // maps exactly onto the handle, and positions never compound.
        for (std::uint32_t vi : app.selectedVertices) {
            auto it = originals.find(vi);
            if (it == originals.end()) continue;
            if (vi >= ga->mesh.vertices.size()) continue;
            ga->mesh.vertices[vi].position = outW.transformPoint(it->second - pivot);
        }
        ga->gpuDirty = true;
        ga->dirty = true;
    }
    if (!gizmoUsing && wasUsing) {
        // Drag end: reset the offset (next drag starts fresh from the current
        // positions) and report. The pre-drag meshBackup snapshot was pushed
        // at drag start, so undo restores the whole stroke.
        app.vertexTransform = App::VertexTransform{};
        originals.clear();
        app.runValidation();
        app.setStatus("Vertex gizmo edit applied.", "success");
    }
    wasUsing = gizmoUsing;
}
#endif

// Dock structure shared by first-run setup and both live Reset paths.
// (Kept separate from the panel-visibility flags: those are live-read every
// frame, this rebuilds the node tree. Viewport compositing — NoBackground +
// offscreen texture — is not touched here.)
void buildDefaultDockLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    // NOTE: no PassthruCentralNode here on purpose: DockSpace lives in
    // the internal enum (imgui_internal.h) while PassthruCentralNode is
    // public (imgui.h) — OR-ing them is a C5054 type mismatch. The
    // documented pattern is the NoBackground-on-host in the Wave-37 dock
    // mirror in drawAllPanels;
    // the docked Viewport window itself stays transparent via
    // ImGuiWindowFlags_NoBackground (load-bearing, see drawViewportPanel).
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
    // Wave 36 split ORDER: top strip first, then the FULL-WIDTH Timeline
    // strip (split BEFORE left/right so it spans the whole window), then the
    // left/right columns, then the center-bottom Output/Tools row above the
    // strip. imgui.ini migration: an existing saved layout keeps its old
    // tree — the new structure applies on first run or via Reset Viewport
    // Layout / View > Reset Dock Layout (both live paths call this function,
    // no restart).
    ImGuiID dockMain = dockspaceId;
    ImGuiID dockTop =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Up, 0.075f, nullptr, &dockMain);
    ImGuiID dockTimeline =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.10f, nullptr, &dockMain);
    ImGuiID dockLeft =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.17f, nullptr, &dockMain);
    ImGuiID dockRight =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.35f, nullptr, &dockMain);
    ImGuiID dockBottom =
        ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.24f, nullptr, &dockMain);
    // Split right into Properties (60%) + Workflow (40%)
    ImGuiID dockRightProps = dockRight;
    ImGuiID dockRightWorkflow =
        ImGui::DockBuilderSplitNode(dockRightProps, ImGuiDir_Right, 0.4f, nullptr, &dockRightProps);
    // Split the center-bottom row into Output (60%) + Tools (40%)
    ImGuiID dockBottomOut = dockBottom;
    ImGuiID dockBottomTools =
        ImGui::DockBuilderSplitNode(dockBottomOut, ImGuiDir_Right, 0.4f, nullptr, &dockBottomOut);

    ImGui::DockBuilderDockWindow("Toolbar", dockTop);
    ImGui::DockBuilderDockWindow("Assets", dockLeft);
    ImGui::DockBuilderDockWindow("Scene", dockLeft);
    ImGui::DockBuilderDockWindow("Skeleton", dockLeft);
    ImGui::DockBuilderDockWindow("Viewport", dockMain);
    // Right Properties column
    ImGui::DockBuilderDockWindow("Bone", dockRightProps);
    ImGui::DockBuilderDockWindow("Weights", dockRightProps);
    ImGui::DockBuilderDockWindow("Materials", dockRightProps);
    ImGui::DockBuilderDockWindow("Bone Display", dockRightProps);
    ImGui::DockBuilderDockWindow("Gizmo", dockRightProps);
    ImGui::DockBuilderDockWindow("Viewport Settings", dockRightProps);
    // Right Workflow column
    ImGui::DockBuilderDockWindow("Export", dockRightWorkflow);
    ImGui::DockBuilderDockWindow("Project", dockRightWorkflow);
    ImGui::DockBuilderDockWindow("Settings", dockRightWorkflow);
    // Center-bottom Output
    ImGui::DockBuilderDockWindow("Validation", dockBottomOut);
    ImGui::DockBuilderDockWindow("Console", dockBottomOut);
    // Full-width Timeline strip (Wave 36) + Tools next to Output
    ImGui::DockBuilderDockWindow("Timeline", dockTimeline);
    ImGui::DockBuilderDockWindow("Tools", dockBottomTools);
    ImGui::DockBuilderFinish(dockspaceId);
}

void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport);

// Renders the 3D scene into the viewport rect (called between ImGui frame
// setup and ImGui::Render, after drawAllPanels determined the rect).
void renderScene(App& app, Renderer& renderer, const ViewportRect& rect) {
    const float* const cv = theme::viewportClearF();  // single source (ui_model token)
    const float clear[4] = {cv[0], cv[1], cv[2], cv[3]};
    if (!rect.valid) {
        static bool loggedInvalid = false;
        if (!loggedInvalid) {
            loggedInvalid = true;
            Logger::instance().warning("renderScene: viewport rect invalid, skipping D3D pass.", "app");
        }
        // Keep the swap-chain RTV bound for the subsequent ImGui pass even
        // while docking/resize produces a transiently invalid viewport.
        renderer.bindBackbuffer();
        renderer.clearBackbuffer(clear);
        return;
    }
    if (!renderer.beginScenePass(rect.x, rect.y, rect.w, rect.h, clear)) {
        renderer.endScenePass();
        return;
    }
    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 view = app.camera.viewMatrix();
    renderer.setSceneView(view);
    const Mat4 vp = view * app.camera.projMatrix(aspect);
    // Single shared scene path (no duplicated grid/mesh/bone logic): the
    // offscreen viewport and this backbuffer fallback render identically.
    float modelRadius = 2.8f;
    if (const LoadedAsset* ga = app.currentAsset()) {
        if (!ga->mesh.bounds.empty) {
            const float r = ga->mesh.bounds.radius();
            if (r > 0.01f) modelRadius = r;
        }
    }
    drawSceneContents(app, renderer, vp, rect, modelRadius);
    renderer.endScenePass();
}

void drawAllPanels(App& app, Renderer& renderer, ViewportRect& outViewport,
                   const std::filesystem::path& projectsDir, bool& showRecovery) {
    // Autosave tick (wall clock; only when something is dirty).
    app.tickAutosave(projectsDir, ImGui::GetTime());
    // Async bridge import completion (UI thread applies the result).
    app.pollBridgeImport();
    if (showRecovery) {
        ImGui::OpenPopup("Crash recovery");
        showRecovery = false;
    }
    if (ImGui::BeginPopupModal("Crash recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("The previous session did not exit cleanly.");
        ImGui::TextDisabled("Restore the autosaved workspace?");
        if (ImGui::Button("Restore autosave")) {
            if (auto r = app.loadWorkspaceFile((projectsDir / "autosave.m2rig").string()); !r)
                app.setStatus("Recovery failed: " + r.error().message, "error");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            std::error_code ec;
            std::filesystem::remove(projectsDir / "autosave.m2rig", ec);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // Wave 37: the menu bar height feeds the dockspace reserve below so the
    // bar no longer overlays the first ~20px of docked windows.
    float menuBarHeight = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        menuBarHeight = ImGui::GetWindowHeight();
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Load sample armor")) {
                if (auto r = app.loadSampleArmor(); !r)
                    app.setStatus("Sample load failed: " + r.error().message, "error");
            }
            if (ImGui::MenuItem("Import SMD...")) doImportSmd(app);
            ImGui::BeginDisabled(app.bridgeBusy);
            if (ImGui::MenuItem("Import FBX/GR2 (bridge)...")) doImportBridged(app);
            if (ImGui::MenuItem("Load GR2 from Data/Models...")) doImportGr2FromModels(app);
            if (ImGui::MenuItem("Import glTF...")) doImportGltf(app);
            ImGui::EndDisabled();
            if (ImGui::MenuItem("Import mesh (keep skeleton)...")) doImportMeshOntoSkeleton(app);
            // Export items need a loaded asset (Wave 37: honest disabled state,
            // exportActionEnabled is the shared gate the panel/palette use too).
            // GR2 -> FBX is a file conversion and stays available without one.
            const bool canExport = exportActionEnabled(app.currentAsset() != nullptr);
            auto exportItem = [&](const char* label, auto&& fn) {
                ImGui::BeginDisabled(!canExport);
                if (ImGui::MenuItem(label)) fn();
                ImGui::EndDisabled();
            };
            exportItem("Export SMD...", [&] { doExportSmd(app); });
            exportItem("Export MSM...", [&] { doExportMsm(app); });
            exportItem("Export GR2 (bridge)...", [&] { doExportGr2(app); });
            exportItem("Export FBX (Noesis)...", [&] { doExportFbx(app); });
            exportItem("Export ANI...", [&] { doExportAni(app); });
            if (ImGui::MenuItem("GR2 -> FBX (Noesis)...")) doExportGr2ToFbx(app);
            exportItem("Export LOD SMD...", [&] { doExportLod(app, app.prefs.lodRatio); });
            exportItem("Export all loaded (SMD+MSM)...", [&] {
                if (auto r = app.exportAllBatch(); !r)
                    app.setStatus("Batch failed: " + r.error().message, "error");
            });
            ImGui::Separator();
            if (ImGui::MenuItem("Save workspace...")) doSaveWorkspace(app);
            if (ImGui::MenuItem("Load workspace...")) doLoadWorkspace(app);
            ImGui::Separator();
            ImGui::BeginDisabled(!app.canUndo());
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) app.undo();
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!app.canRedo());
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) app.redo();
            ImGui::EndDisabled();
            if (ImGui::MenuItem(labelValidateMenu())) app.runValidation();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            auto flag = [&](const char* label, const char* shortcut, bool* v) {
                if (ImGui::MenuItem(label, shortcut, v)) {
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
            };
            flag("Grid", "G", &app.showGrid);
            flag("Bones", "B", &app.showBones);
            flag(labelXray(), "X", &app.xrayBones);
            // W is now the gizmo-move hotkey (Blender convention); the wire
            // overlay toggle itself stays here, just without a key hint.
            flag("Wire overlay", nullptr, &app.showWireOverlay);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("W is now the gizmo-move hotkey (Blender); wire overlay is toolbar-only");
            if (ImGui::MenuItem("Textured", "T", &app.textured)) {
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            ImGui::MenuItem("PBR shading", nullptr, &app.usePbr);
            ImGui::MenuItem("Paint mode", "P", &app.paintMode);
            if (ImGui::MenuItem("Deform preview", "D", &app.previewDeform)) {
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            ImGui::Separator();
            const char* modes[] = {"Solid",     "Wireframe", "Solid + Wire", "Normals",
                                   "Height",    "Weights",   "UV"};
            for (int i = 0; i < 7; ++i) {
                char shortcut[2] = {static_cast<char>('1' + i), '\0'};
                if (ImGui::MenuItem(modes[i], shortcut, app.viewMode == static_cast<ViewMode>(i))) {
                    app.viewMode = static_cast<ViewMode>(i);
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
            }
            ImGui::Separator();
            ImGui::MenuItem("Orthographic", nullptr, &app.camera.orthographic);
            ImGui::BeginDisabled(app.currentAsset() == nullptr);
            if (ImGui::MenuItem("Frame all", "F")) {
                frameWholeModel(app, false);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Fit the whole model in view (F)");
            ImGui::MenuItem("VSync", nullptr, &app.vsync);
            ImGui::Separator();
            if (ImGui::BeginMenu("Panels")) {
                ImGui::MenuItem("Bone", nullptr, &app.uiSettings.showBonePanel);
                ImGui::MenuItem("Weights", nullptr, &app.uiSettings.showWeightsPanel);
                ImGui::MenuItem("Materials", nullptr, &app.uiSettings.showMaterialsPanel);
                ImGui::MenuItem("Bone Display", nullptr, &app.uiSettings.showBoneDisplayPanel);
                ImGui::MenuItem("Gizmo", nullptr, &app.uiSettings.showGizmoPanel);
                ImGui::MenuItem("Viewport Settings", nullptr,
                                &app.uiSettings.showViewportSettingsPanel);
                ImGui::MenuItem("Export", nullptr, &app.uiSettings.showExportPanel);
                ImGui::MenuItem("Project", nullptr, &app.uiSettings.showProjectPanel);
                ImGui::MenuItem("Settings", nullptr, &app.uiSettings.showSettingsPanel);
                ImGui::MenuItem("Validation", nullptr, &app.uiSettings.showValidationPanel);
                ImGui::MenuItem("Console", nullptr, &app.uiSettings.showConsolePanel);
                ImGui::MenuItem("Timeline", nullptr, &app.uiSettings.showTimelinePanel);
                ImGui::MenuItem("Tools", nullptr, &app.uiSettings.showSystemPanel);
                ImGui::MenuItem("MSM Inspector tab", nullptr,
                                &app.uiSettings.showMSMInspectorPanel);
                ImGui::MenuItem("MSE Effects tab", nullptr, &app.uiSettings.mseTabEnabled);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout")) {
                const ImGuiID resetId = ImGui::GetID("M2RigDockSpace");
                std::error_code ec;
                std::filesystem::remove(ImGui::GetIO().IniFilename ? ImGui::GetIO().IniFilename : "", ec);
                // Live rebuild via the file-scope flag (this menu runs before
                // the DockSpace below, so the rebuild lands in the same frame).
                requestDockRebuild();
                buildDefaultDockLayout(resetId);
                markDockBuilt();
                app.setStatus("Layout reset — layout settings cleared, rebuilt live", "success");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Shortcuts...")) ImGui::OpenPopup("Shortcuts");
            if (ImGui::MenuItem("About...")) ImGui::OpenPopup("About");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    // Global shortcuts must not steal keys from active ImGui controls.
    const ImGuiIO& shortcutIo = ImGui::GetIO();
    const bool appOwnsKeyboard = !shortcutIo.WantTextInput && !shortcutIo.WantCaptureKeyboard;
    if (appOwnsKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            frameWholeModel(app, false);
        }
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) app.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) app.redo();
        // Command palette: explicit user intent under the same appOwnsKeyboard
        // arbiter as every other global (never steals keys from text inputs).
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_K)) ImGui::OpenPopup("cmd_palette");
        if (!ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_G)) app.showGrid = !app.showGrid;
            if (ImGui::IsKeyPressed(ImGuiKey_B)) app.showBones = !app.showBones;
            if (ImGui::IsKeyPressed(ImGuiKey_X)) app.xrayBones = !app.xrayBones;
            if (ImGui::IsKeyPressed(ImGuiKey_P)) app.paintMode = !app.paintMode;
            // Blender-style gizmo op hotkeys: W = move, E = rotate, R = scale.
            // Same appOwnsKeyboard guard as every other global above. W used to
            // toggle wire overlay; that toggle stays reachable via the View
            // menu / toolbar / Display panel / viewport popover, but the W key
            // itself now means gizmo-translate (Blender convention).
            if (ImGui::IsKeyPressed(ImGuiKey_W)) app.gizmoOp = GizmoOp::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) app.gizmoOp = GizmoOp::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) app.gizmoOp = GizmoOp::Scale;
            // Tab toggles Object/Edit mode (Blender convention). Same
            // appOwnsKeyboard guard as every other global above, so ImGui
            // widget navigation keeps Tab while a control is focused.
            if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
                app.toggleEditMode();
                app.setStatus(app.editMode == EditMode::Edit ? "Edit mode: click vertices to select (Ctrl+click multi-select)"
                                                             : "Object mode: click bones/submeshes",
                               "info");
            }
            if (ImGui::IsKeyPressed(ImGuiKey_T)) {
                app.textured = !app.textured;
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_D)) {
                app.previewDeform = !app.previewDeform;
                if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
            }
            for (int i = 0; i < 7; ++i) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i))) {
                    app.viewMode = static_cast<ViewMode>(i);
                    if (LoadedAsset* a = app.currentAsset()) a->gpuDirty = true;
                }
            }
            // Brush size keys (industry standard): [ / ].
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false))
                app.brushRadius = std::max(0.05f, app.brushRadius - 0.05f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false))
                app.brushRadius = std::min(2.5f, app.brushRadius + 0.05f);
        }
        // Arrow-key nudge for the selected bone (command palette closed so
        // the two never fight; Shift = larger step). Once per press (no auto-
        // repeat) so undo stays one entry per nudge.
        if (app.selectedBone >= 0 && !ImGui::IsPopupOpen("cmd_palette")) {
            Vec3 nudge{0, 0, 0};
            const float step = ImGui::GetIO().KeyShift ? 0.1f : 0.02f;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) nudge.y += step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) nudge.y -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) nudge.x -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) nudge.x += step;
            if (nudge.x != 0.0f || nudge.y != 0.0f) {
                if (LoadedAsset* na = app.currentAsset()) {
                    if (Bone* nb = na->skeleton.findById(static_cast<std::uint32_t>(app.selectedBone))) {
                        if (!app.isBoneLocked(nb->id)) {
                            app.pushUndoSnapshot("nudge " + nb->name);
                            nb->localPosition.x += nudge.x;
                            nb->localPosition.y += nudge.y;
                            rebuildSkeletonRuntime(na->skeleton);
                            na->gpuDirty = true;
                            na->dirty = true;
                        }
                    }
                }
            }
        }
    }
    drawCommandPalette(app);
    if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Metin2 Rigging Studio v%s (native C++20, D3D11 + ImGui)", appVersion());
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Diagnostics")) {
            Gr2BridgeConfig bcfg = defaultGr2BridgeConfig();
            ImGui::Text("Noesis bridge: %s",
                        std::filesystem::exists(bcfg.noesisCliPath) ? bcfg.noesisCliPath.string().c_str()
                                                                    : "not found");
            ImGui::Text("grnreader98: %s",
                        findGrnReader().empty() ? "not found" : findGrnReader().string().c_str());
            ImGui::TextDisabled("GR2 native emit: NOT supported directly (bridge only).");
        }
        ImGui::Separator();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("shortcuts", 2)) {
            const char* rows[][2] = {
                {"F", "Frame all"},         {"Space", "Play / pause timeline"},
                {"Left / Right", "Step frame"}, {"Ctrl+Z / Ctrl+Y", "Undo / redo"},
                {"Ctrl/Shift+drag", "Paint weights (needs bone)"}, {"Drag", "Orbit camera"},
                {"Right/Middle drag", "Pan"}, {"Wheel", "Zoom"},
                {"Click (no Ctrl)", "Select bone"},   {"G / B / X / P / D", "View toggles"},
                {"W / E / R", "Gizmo move / rotate / scale (Blender)"},
                {"1-7", "View modes"},      {"T", "Textured"},
                {"Shift+drag", "Box-select bones"}, {"Ctrl+click", "Additive bone select"},
                {"[ / ]", "Brush size"}, {"Arrows", "Nudge selected bone (Shift = big)"},
                {"Paint needs", "Paint mode + selected bone + Ctrl/Shift+drag"}};
            for (const auto& row : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", row[0]);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row[1]);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const ImGuiID dockspaceId = ImGui::GetID("M2RigDockSpace");
    constexpr ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    const char* imguiIni = ImGui::GetIO().IniFilename;
    const bool hasSavedLayout = imguiIni != nullptr && std::filesystem::exists(imguiIni);
    // File-scope g_dockBuilt (not function-static) so both Reset paths can
    // force this same rebuild live. First run with no imgui.ini builds once.
    if (!g_dockBuilt && !hasSavedLayout) {
        buildDefaultDockLayout(dockspaceId);
        markDockBuilt();
    }
    // Wave 37 (status-bar overlap fix): mirrors DockSpaceOverViewport from the
    // pinned ImGui (same host-window submission) with an explicit reserve —
    // the main menu bar owns the top menuBarHeight px and the status bar owns
    // the bottom kStatusBarHeight px, so neither overlays the first/last rows
    // of docked panels. PassthruCentralNode stays ONLY on this DockSpace call
    // (load-bearing — see buildDefaultDockLayout); the host derives
    // NoBackground from the same flag, exactly like the stock helper.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 dockPos(vp->WorkPos.x, vp->WorkPos.y + menuBarHeight);
    ImVec2 dockSize(vp->WorkSize.x,
                    vp->WorkSize.y - menuBarHeight - kStatusBarHeight);
    if (dockSize.y < 80.0f) dockSize.y = 80.0f;  // never collapse the dock area
    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus;
    // Ternary (not if): dockspaceFlags is constexpr — an if would trip C4127
    // under /W4 /WX.
    hostFlags |= (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
                     ? ImGuiWindowFlags_NoBackground
                     : 0;
    // Same host label as the stock helper (keeps imgui.ini window entries
    // stable for existing users).
    char hostLabel[32];
    std::snprintf(hostLabel, sizeof(hostLabel), "WindowOverViewport_%08X", vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(hostLabel, nullptr, hostFlags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(dockspaceId, dockSize, dockspaceFlags);
    ImGui::End();

    if (ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoCollapse)) drawToolbar(app);
    ImGui::End();

    // Left column - Assets, Scene, Skeleton as separate tabbed windows
    if (ImGui::Begin("Assets")) drawAssetsPanel(app);
    ImGui::End();

    if (ImGui::Begin("Scene")) drawScenePanel(app);
    ImGui::End();

    if (ImGui::Begin("Skeleton")) drawSkeletonPanel(app);
    ImGui::End();

    outViewport = drawViewportPanel(app, renderer);

    // Right Properties column
    if (app.uiSettings.showBonePanel) {
        if (ImGui::Begin("Bone")) drawBoneProperties(app);
        ImGui::End();
    }
    if (app.uiSettings.showWeightsPanel) {
        if (ImGui::Begin("Weights")) drawWeightPanel(app, renderer);
        ImGui::End();
    }
    if (app.uiSettings.showMaterialsPanel) {
        if (ImGui::Begin("Materials")) drawMaterialPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showBoneDisplayPanel) {
        if (ImGui::Begin("Bone Display")) drawBoneDisplayPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showGizmoPanel) {
        if (ImGui::Begin("Gizmo")) drawGizmoPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showViewportSettingsPanel) {
        if (ImGui::Begin("Viewport Settings")) drawViewportSettingsPanel(app);
        ImGui::End();
    }

    // Right Workflow column
    if (app.uiSettings.showExportPanel) {
        if (ImGui::Begin("Export")) drawExportPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showProjectPanel) {
        if (ImGui::Begin("Project")) drawProjectPanel(app, projectsDir);
        ImGui::End();
    }
    if (app.uiSettings.showSettingsPanel) {
        if (ImGui::Begin("Settings")) drawSettingsPanel(app);
        ImGui::End();
    }

    // Bottom Output
    if (app.uiSettings.showValidationPanel) {
        if (ImGui::Begin("Validation")) drawValidationPanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showConsolePanel) {
        if (ImGui::Begin("Console")) drawConsolePanel();
        ImGui::End();
    }

    // Bottom Tools
    if (app.uiSettings.showTimelinePanel) {
        if (ImGui::Begin("Timeline")) drawTimelinePanel(app);
        ImGui::End();
    }
    if (app.uiSettings.showSystemPanel) {
        if (ImGui::Begin("Tools")) {
            if (ImGui::BeginTabBar("ToolsTabs")) {
                if (ImGui::BeginTabItem("System")) {
                    drawSystemPanel(app);
                    ImGui::EndTabItem();
                }
                if (app.uiSettings.showMSMInspectorPanel && ImGui::BeginTabItem("MSM Inspector")) {
                    drawMsmInspector(app);
                    ImGui::EndTabItem();
                }
                if (app.uiSettings.mseTabEnabled && ImGui::BeginTabItem("MSE Effects")) {
                    drawMseEffects(app);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }
    }

    // Status bar and toasts (rendered after all panels, on top)
    drawStatusBar(app, renderer, outViewport);
    drawToasts(app);
}

}  // namespace m2rig
