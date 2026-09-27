// G4 routing matrix test (headless): pins resolveDrawPath, the pure
// ViewMode x usePbr x wantTextured x hasTexture x skinned routing table that
// drawSceneContents (src/app/panels.cpp) dispatches on. resolveDrawPath lives
// in m2rig_core (declared in include/m2rig/app.hpp, defined in
// src/app_state.cpp) so this links with no exe-only symbols.
#include <cstdio>
#include <filesystem>

#include "expect.hpp"
#include "m2rig/app.hpp"

using namespace m2rig;

// Independent re-derivation of the documented contract (not a copy of the
// implementation's control flow): debug ramps first, Wireframe second,
// solid-mode PBR/texture combinations last.
static DrawPath expectedDrawPath(ViewMode mode, bool usePbr, bool wantTextured) {
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

M2RIG_TEST(app, resolve_draw_path_routing_matrix) {
    int failures = 0;
    // Full matrix: 7 modes x usePbr x wantTextured x hasTexture x skinned.
    // hasTexture and skinned are orthogonal by contract (renderer fallback /
    // Skinned draw suffix), so they must never change the resolved path.
    for (int m = 0; m < 7; ++m) {
        const auto mode = static_cast<ViewMode>(m);
        for (int p = 0; p < 2; ++p) {
            for (int t = 0; t < 2; ++t) {
                const DrawPath want = expectedDrawPath(mode, p != 0, t != 0);
                for (int h = 0; h < 2; ++h) {
                    for (int s = 0; s < 2; ++s) {
                        const DrawPath got = resolveDrawPath(mode, p != 0, t != 0, h != 0, s != 0);
                        if (!(got == want)) {
                            printf("    FAIL app.resolve_draw_path_routing_matrix: mode=%d pbr=%d "
                                   "textured=%d hasTexture=%d skinned=%d: got %s, want %s\n",
                                   m, p, t, h, s, drawPathName(got), drawPathName(want));
                            ++failures;
                        }
                    }
                }
            }
        }
    }
    // Names stay valid for every enumerator (failure output readability).
    CHECK_TRUE(drawPathName(DrawPath::SolidUntextured) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::SolidTextured) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::FlatDebug) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::WireBlinn) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::PbrSolid) != nullptr);
    CHECK_TRUE(drawPathName(DrawPath::PbrTextured) != nullptr);
    return failures;
}

// Mesh-level gizmo state: submesh selection + the destructive vertex bake.
// applySubmeshTransform applies submeshTransforms[idx] (scale, rotate,
// translate — the Mat4::compose order) to every vertex the submesh
// references, then resets the offset so the bake is exactly-once.
M2RIG_TEST(app, submesh_selection_and_transform_bake) {
    int failures = 0;
    App app;
    LoadedAsset la;
    la.id = "mesh-test";
    la.mesh.vertices.resize(4);
    la.mesh.vertices[0].position = {0, 0, 0};
    la.mesh.vertices[1].position = {1, 0, 0};
    la.mesh.vertices[2].position = {0, 1, 0};
    la.mesh.vertices[3].position = {0, 0, 1};
    la.mesh.indices = {0, 1, 2, 0, 1, 3};
    SubMesh sm;
    sm.name = "sub0";
    sm.materialIndex = 0;
    sm.startIndex = 0;
    sm.indexCount = 6;
    la.mesh.subMeshes = {sm};
    app.assets["mesh-test"] = std::move(la);
    app.current = "mesh-test";

    // Selection: default none, select, clear via -1 and via clearSubmeshSelection.
    CHECK_TRUE(app.selectedSubmesh == -1);
    app.selectSubmesh(0);
    CHECK_TRUE(app.selectedSubmesh == 0);
    app.selectSubmesh(-1);
    CHECK_TRUE(app.selectedSubmesh == -1);
    app.selectSubmesh(0);
    app.clearSubmeshSelection();
    CHECK_TRUE(app.selectedSubmesh == -1);

    // Bake: translate(1,2,3) + scale(2,1,1), no rotation.
    // v' = R*S*v + t: (1,0,0) -> (2,0,0) -> (3,2,3).
    app.selectSubmesh(0);
    App::SubmeshTransform tf;
    tf.position = {1, 2, 3};
    tf.rotationEuler = {0, 0, 0};
    tf.scale = {2, 1, 1};
    app.submeshTransforms[0] = tf;
    app.applySubmeshTransform(0);
    const auto& v = app.assets["mesh-test"].mesh.vertices;
    CHECK_NEAR(v[0].position.x, 1.0f, 1e-4f);
    CHECK_NEAR(v[0].position.y, 2.0f, 1e-4f);
    CHECK_NEAR(v[0].position.z, 3.0f, 1e-4f);
    CHECK_NEAR(v[1].position.x, 3.0f, 1e-4f);
    CHECK_NEAR(v[1].position.y, 2.0f, 1e-4f);
    CHECK_NEAR(v[1].position.z, 3.0f, 1e-4f);
    CHECK_NEAR(v[2].position.x, 1.0f, 1e-4f);
    CHECK_NEAR(v[2].position.y, 3.0f, 1e-4f);
    CHECK_NEAR(v[2].position.z, 3.0f, 1e-4f);
    // The offset is reset after the bake (exactly-once).
    CHECK_TRUE(app.submeshTransforms.empty());
    // A second bake is a no-op (no compounding).
    app.applySubmeshTransform(0);
    CHECK_NEAR(v[1].position.x, 3.0f, 1e-4f);
    CHECK_TRUE(app.assets["mesh-test"].gpuDirty);

    // Rotation: 90 deg about Z maps (x,y,z) -> (-y,x,z) (row-vector convention).
    App::SubmeshTransform rt;
    rt.position = {0, 0, 0};
    rt.rotationEuler = {0, 0, kPi / 2.0f};
    rt.scale = {1, 1, 1};
    app.submeshTransforms[0] = rt;
    app.applySubmeshTransform(0);
    // v[1] was (3,2,3) after the first bake.
    CHECK_NEAR(v[1].position.x, -2.0f, 1e-4f);
    CHECK_NEAR(v[1].position.y, 3.0f, 1e-4f);
    CHECK_NEAR(v[1].position.z, 3.0f, 1e-4f);

    // Out-of-range submesh index is a guarded no-op.
    app.applySubmeshTransform(99);
    CHECK_TRUE(app.submeshTransforms.empty());
    return failures;
}

// Asset switch clears the submesh selection (per-asset session state, like
// selectedBones / hiddenSubmeshes).
M2RIG_TEST(app, submesh_selection_cleared_on_asset_switch) {
    int failures = 0;
    App app;
    if (!app.loadSampleArmor().succeeded()) return failures + 1;
    app.selectSubmesh(0);
    CHECK_TRUE(app.selectedSubmesh == 0);
    // Re-loading the sample installs a fresh asset and must clear the
    // selection (same rule as selectedBones).
    if (!app.loadSampleArmor().succeeded()) return failures + 1;
    CHECK_TRUE(app.selectedSubmesh == -1);
    CHECK_TRUE(app.submeshTransforms.empty());
    return failures;
}

// Wave 37: mseTabEnabled must round-trip through user_prefs.json like every
// other UI flag (it lived in a panels_msm.cpp global before and was never
// persisted). Save/load are App methods in the core (src/app_state.cpp).
M2RIG_TEST(app, prefs_round_trip_mse_tab_enabled) {
    int failures = 0;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "m2rig_test_prefs_mse";
    fs::remove_all(dir, ec);
    // Flipped flag persists...
    {
        App a;
        a.uiSettings.mseTabEnabled = false;
        const ResultVoid saved = a.savePreferences(dir);
        CHECK_TRUE(saved.succeeded());
    }
    // ...and loads back into a fresh App (default true).
    {
        App b;
        CHECK_TRUE(b.uiSettings.mseTabEnabled);
        const ResultVoid loaded = b.loadPreferences(dir);
        CHECK_TRUE(loaded.succeeded());
        CHECK_FALSE(b.uiSettings.mseTabEnabled);
    }
    // Default (true) round-trips too: flipped receiver recovers the file value.
    {
        App c;
        CHECK_TRUE(c.savePreferences(dir).succeeded());
        App d;
        d.uiSettings.mseTabEnabled = false;
        CHECK_TRUE(d.loadPreferences(dir).succeeded());
        CHECK_TRUE(d.uiSettings.mseTabEnabled);
    }
    fs::remove_all(dir, ec);
    return failures;
}

// Edit mode: the enum default + the Tab toggle (App::toggleEditMode; the
// hotkey itself lives in the ImGui shortcut layer and is covered by the
// build, same as the W/E/R gizmo hotkeys).
M2RIG_TEST(app, edit_mode_toggle) {
    int failures = 0;
    App app;
    // Default is Object mode (bone/submesh selection).
    CHECK_TRUE(app.editMode == EditMode::Object);
    app.toggleEditMode();
    CHECK_TRUE(app.editMode == EditMode::Edit);
    app.toggleEditMode();
    CHECK_TRUE(app.editMode == EditMode::Object);
    return failures;
}

// Vertex selection (Edit mode): selectVertex replace/additive + clear,
// mirroring the submesh selection contract. gpuDirty is set on every
// selection change so the viewport redraws the dot overlay.
M2RIG_TEST(app, vertex_selection) {
    int failures = 0;
    App app;
    LoadedAsset la;
    la.id = "vert-test";
    la.mesh.vertices.resize(4);
    la.mesh.vertices[0].position = {0, 0, 0};
    la.mesh.vertices[1].position = {1, 0, 0};
    la.mesh.vertices[2].position = {0, 1, 0};
    la.mesh.vertices[3].position = {0, 0, 1};
    app.assets["vert-test"] = std::move(la);
    app.current = "vert-test";

    // Default: nothing selected.
    CHECK_TRUE(app.selectedVertices.empty());
    // Plain select replaces.
    app.selectVertex(1, false);
    CHECK_TRUE(app.selectedVertices.size() == 1);
    CHECK_TRUE(app.selectedVertices.count(1) != 0);
    // Additive select adds without dropping the rest.
    app.selectVertex(2, true);
    CHECK_TRUE(app.selectedVertices.size() == 2);
    CHECK_TRUE(app.selectedVertices.count(1) != 0);
    CHECK_TRUE(app.selectedVertices.count(2) != 0);
    app.selectVertex(3, true);
    CHECK_TRUE(app.selectedVertices.size() == 3);
    // Clear.
    app.clearVertexSelection();
    CHECK_TRUE(app.selectedVertices.empty());
    // gpuDirty is set by a selection change.
    app.selectVertex(0, false);
    CHECK_TRUE(app.selectedVertices.count(0) != 0);
    CHECK_TRUE(app.assets["vert-test"].gpuDirty);
    return failures;
}

// Asset switch clears the vertex selection (per-asset session state, like
// selectedBones / selectedSubmesh).
M2RIG_TEST(app, vertex_selection_cleared_on_asset_switch) {
    int failures = 0;
    App app;
    if (!app.loadSampleArmor().succeeded()) return failures + 1;
    app.selectVertex(0, false);
    CHECK_TRUE(app.selectedVertices.size() == 1);
    // Re-loading the sample installs a fresh asset and must clear the
    // selection (same rule as selectedBones/selectedSubmesh).
    if (!app.loadSampleArmor().succeeded()) return failures + 1;
    CHECK_TRUE(app.selectedVertices.empty());
    return failures;
}
