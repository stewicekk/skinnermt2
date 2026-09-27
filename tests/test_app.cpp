// G4 routing matrix test (headless): pins resolveDrawPath, the pure
// ViewMode x usePbr x wantTextured x hasTexture x skinned routing table that
// drawSceneContents (src/app/panels.cpp) dispatches on. resolveDrawPath lives
// in m2rig_core (declared in include/m2rig/app.hpp, defined in
// src/app_state.cpp) so this links with no exe-only symbols.
#include <cstdio>
#include <filesystem>

#include "expect.hpp"
#include "m2rig/app.hpp"
#include "m2rig/gr2_deep_parser.hpp"

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

// --- Native GR2 export (Gr2Writer) -----------------------------------------

// Minimal clean asset for the GR2 export tests: 2 translation-only bones (so
// the parser's matrix->TRS decomposition is exact), a 4-vertex quad with full
// root weights, one material and two animation frames. The profile is unknown
// on purpose (APP_NO_PROFILE is a Warning, so the export gate passes without
// depending on profile bone lists).
static LoadedAsset makeGr2TestAsset() {
    LoadedAsset la;
    la.id = "gr2-test";
    la.profileId = "gr2_test_unknown_profile";

    std::vector<BoneDefinition> defs;
    defs.push_back({"Bip01", kNoParent, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}});
    defs.push_back({"Bip01 Spine", 0, {0, 1, 0}, {0, 0, 0}, {1, 1, 1}});
    la.skeleton = buildSkeleton("gr2_test_skel", defs).value();

    la.mesh.name = "gr2_test_mesh";
    la.mesh.vertices.resize(4);
    la.mesh.vertices[0].position = {0, 0, 0};
    la.mesh.vertices[1].position = {1, 0, 0};
    la.mesh.vertices[2].position = {1, 1, 0};
    la.mesh.vertices[3].position = {0, 1, 0};
    for (auto& v : la.mesh.vertices) {
        v.normal = {0, 0, 1};
        v.uv0 = {0, 0};
        v.influences.push_back({0, 1.0f});
    }
    la.mesh.indices = {0, 1, 2, 0, 2, 3};
    SubMesh sm;
    sm.name = "sub0";
    sm.materialIndex = 0;
    sm.startIndex = 0;
    sm.indexCount = 6;
    la.mesh.subMeshes.push_back(sm);
    MaterialRef mat;
    mat.name = "armor_body.dds";
    mat.texturePath = "armor_body.dds";
    la.mesh.materials.push_back(mat);

    SmdFrame f0;
    f0.time = 0;
    f0.poses = {SmdBonePose{0, {0, 0, 0}, {0, 0, 0}}, SmdBonePose{1, {0, 1, 0}, {0, 0, 0}}};
    SmdFrame f1;
    f1.time = 1;
    f1.poses = {SmdBonePose{0, {0, 0, 0}, {0, 0, 0}}, SmdBonePose{1, {0, 2, 0}, {0, 0, 0}}};
    la.animFrames = {f0, f1};
    return la;
}

M2RIG_TEST(app, export_gr2_native_no_asset_fails) {
    int failures = 0;
    namespace fs = std::filesystem;
    App app;  // no asset loaded
    const fs::path dir = fs::temp_directory_path() / "m2rig_test_gr2_noasset";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path out = dir / "test.gr2";

    const ResultVoid r = app.exportGr2Native(out.string());
    CHECK_FALSE(r.succeeded());
    CHECK_TRUE(!r.error().message.empty());
    // Fail-closed: no partial file is written.
    CHECK_FALSE(fs::exists(out));

    fs::remove_all(dir, ec);
    return failures;
}

M2RIG_TEST(app, export_gr2_native_writes_valid_container) {
    int failures = 0;
    namespace fs = std::filesystem;
    App app;
    LoadedAsset la = makeGr2TestAsset();
    app.assets[la.id] = la;
    app.current = la.id;

    const fs::path dir = fs::temp_directory_path() / "m2rig_test_gr2_native";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path out = dir / "test.gr2";

    const ResultVoid r = app.exportGr2Native(out.string());
    if (!r.succeeded()) {
        printf("    FAIL app.export_gr2_native_writes_valid_container: export failed: %s\n",
               r.error().message.c_str());
        ++failures;
        fs::remove_all(dir, ec);
        return failures;
    }

    // File exists and starts with the Metin2 GR2 magic (29 DE 6C C0 LE).
    std::ifstream in(out, std::ios::binary);
    CHECK_TRUE(in.is_open());
    unsigned char magic[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(magic), 4);
    in.close();
    CHECK_EQ((int)magic[0], 0x29);
    CHECK_EQ((int)magic[1], 0xDE);
    CHECK_EQ((int)magic[2], 0x6C);
    CHECK_EQ((int)magic[3], 0xC0);

    fs::remove_all(dir, ec);
    return failures;
}

M2RIG_TEST(app, export_gr2_native_round_trip) {
    int failures = 0;
    namespace fs = std::filesystem;
    App app;
    LoadedAsset la = makeGr2TestAsset();
    app.assets[la.id] = la;
    app.current = la.id;

    const fs::path dir = fs::temp_directory_path() / "m2rig_test_gr2_rt";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path out = dir / "test.gr2";

    const ResultVoid r = app.exportGr2Native(out.string());
    if (!r.succeeded()) {
        printf("    FAIL app.export_gr2_native_round_trip: export failed: %s\n",
               r.error().message.c_str());
        ++failures;
        fs::remove_all(dir, ec);
        return failures;
    }

    // Parse the file back with the reverse-engineered parser.
    Gr2DeepParser parser;
    auto parsed = parser.parse(out.string());
    if (!parsed.succeeded()) {
        printf("    FAIL app.export_gr2_native_round_trip: parse failed: %s\n",
               parsed.error().message.c_str());
        ++failures;
        fs::remove_all(dir, ec);
        return failures;
    }
    const Gr2ParseResult& pr = parsed.value();

    // Header: magic + totalSize matches the file on disk.
    CHECK_EQ((int)pr.header.magic, (int)GR2_METIN2_MAGIC);
    CHECK_TRUE(pr.header.totalSize == static_cast<std::uint32_t>(fs::file_size(out)));
    // Skeleton + Mesh + MeshBinding + Material + Animation sections.
    CHECK_TRUE(pr.header.sectionCount >= 5);

    // Skeleton round-trip (2 translation-only bones).
    CHECK_TRUE(pr.hasSkeleton);
    CHECK_EQ((int)pr.bones.size(), 2);
    CHECK_TRUE(pr.bones[0].name == "Bip01");
    CHECK_EQ((int)pr.bones[0].parentIndex, (int)kNoParent);
    CHECK_TRUE(pr.bones[1].name == "Bip01 Spine");
    CHECK_EQ((int)pr.bones[1].parentIndex, 0);
    CHECK_NEAR(pr.bones[0].position.x, 0.0f, 1e-6f);
    CHECK_NEAR(pr.bones[1].position.y, 1.0f, 1e-6f);
    CHECK_NEAR(pr.bones[1].rotation.x, 0.0f, 1e-6f);
    CHECK_NEAR(pr.bones[1].scale.x, 1.0f, 1e-6f);
    // The raw local matrix round-trips exactly (16 floats in, 16 out).
    CHECK_NEAR(pr.bones[1].localTransform.m[3][1], 1.0f, 1e-6f);

    // Mesh round-trip (4-vert quad, 2 triangles).
    CHECK_TRUE(pr.hasMesh);
    CHECK_EQ((int)pr.meshes.size(), 1);
    const Gr2Mesh& m = pr.meshes[0];
    CHECK_TRUE(m.name == "gr2_test_mesh");
    CHECK_EQ((int)m.vertexCount, 4);
    CHECK_EQ((int)m.triangleCount, 2);
    CHECK_NEAR(m.positions[1].x, 1.0f, 1e-6f);
    CHECK_NEAR(m.positions[2].y, 1.0f, 1e-6f);
    CHECK_NEAR(m.normals[0].z, 1.0f, 1e-6f);
    CHECK_NEAR(m.uvs[3].x, 0.0f, 1e-6f);
    CHECK_EQ((int)m.indices.size(), 6);
    CHECK_EQ((int)m.indices[0], 0);
    CHECK_EQ((int)m.indices[2], 2);

    // MeshBinding section written and recognized (weights live there).
    CHECK_TRUE(pr.hasWeights);

    // Material round-trip.
    CHECK_EQ((int)pr.materials.size(), 1);
    CHECK_TRUE(pr.materials[0].name == "armor_body.dds");
    CHECK_TRUE(pr.materials[0].texturePath == "armor_body.dds");

    // Animation round-trip (2 frames, 2 bones; frame 1 poses bone 1 at y=2).
    CHECK_TRUE(pr.hasAnimation);
    CHECK_EQ((int)pr.animations.size(), 1);
    const Gr2Animation& anim = pr.animations[0];
    CHECK_EQ((int)anim.boneFrames.size(), 2);
    CHECK_EQ((int)anim.boneFrames[0].size(), 2);
    CHECK_NEAR(anim.boneFrames[0][1].m[3][1], 1.0f, 1e-5f);
    CHECK_NEAR(anim.boneFrames[1][1].m[3][1], 2.0f, 1e-5f);

    fs::remove_all(dir, ec);
    return failures;
}

// newWorkspace (toolbar "New"): clears ALL assets and resets per-asset session
// state (selection, locks, hidden, solo, undo/redo, box-select, timeline)
// while preserving UI/view preferences. Regression: the method the toolbar
// calls must exist and produce a clean, empty session.
M2RIG_TEST(app, new_workspace_clears_assets_and_state) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    CHECK_EQ(app.assets.size(), static_cast<std::size_t>(1));
    CHECK_TRUE(app.current == "sample-warrior-armor");

    // Dirty the session state newWorkspace must reset.
    app.selectedBone = 0;
    app.selectedBones.insert(0);
    app.hiddenBones.insert(0);
    app.soloBone = 0;
    app.selectedSubmesh = 0;
    app.selectedVertices.insert(0);
    app.lockedBones.insert(0);
    app.hiddenSubmeshes.insert(0);
    app.assets["sample-warrior-armor"].submeshPbr[0] = {0.5f, 0.5f, 0.5f};

    CHECK_TRUE(app.newWorkspace().succeeded());
    CHECK_EQ(app.assets.size(), static_cast<std::size_t>(0));
    CHECK_TRUE(app.current.empty());
    CHECK_EQ(app.selectedBone, -1);
    CHECK_TRUE(app.selectedBones.empty());
    CHECK_TRUE(app.hiddenBones.empty());
    CHECK_EQ(app.soloBone, -1);
    CHECK_EQ(app.selectedSubmesh, -1);
    CHECK_TRUE(app.selectedVertices.empty());
    CHECK_TRUE(app.lockedBones.empty());
    CHECK_TRUE(app.hiddenSubmeshes.empty());
    CHECK_FALSE(app.canUndo());
    CHECK_FALSE(app.canRedo());
    return failures;
}

// MaterialRef UV transform defaults are the identity (offset 0, scale 1,
// rotation 0, wrap 0) so existing assets render byte-identically, and the
// per-submesh PBR override map starts empty. Regression: the old panel used
// submeshPbr[idx] (operator[]), which value-initialized a {0,0,0} override the
// moment the Materials panel opened — re-shading submesh 0 (roughness 0 =
// mirror, ao 0 = black) under the asset-level PBR.
M2RIG_TEST(app, material_uv_defaults_and_submesh_pbr_empty) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    const LoadedAsset& a = app.assets.at("sample-warrior-armor");
    CHECK_FALSE(a.mesh.materials.empty());
    for (const auto& m : a.mesh.materials) {
        CHECK_NEAR(m.uvOffset[0], 0.0f, 1e-6f);
        CHECK_NEAR(m.uvOffset[1], 0.0f, 1e-6f);
        CHECK_NEAR(m.uvScale[0], 1.0f, 1e-6f);
        CHECK_NEAR(m.uvScale[1], 1.0f, 1e-6f);
        CHECK_NEAR(m.uvRotation, 0.0f, 1e-6f);
        CHECK_NEAR(m.wrapMode, 0.0f, 1e-6f);
    }
    CHECK_TRUE(a.submeshPbr.empty());
    return failures;
}

// Wave 1 regression: SelfLearningDatabase::load on an empty/comment-only file
// must not crash (null-pointer dereference at self_learning.cpp:95).
M2RIG_TEST(app, self_learning_db_empty_file_no_crash) {
    int failures = 0;
    App app;
    // Create a comment-only .m2learn file (no ENTRY: lines)
    std::filesystem::path tmp = std::filesystem::temp_directory_path();
    tmp /= "test_empty.m2learn";
    { std::ofstream f(tmp); f << "# comment only\n# no entries\n"; }
    // Must not crash — load returns ok (empty database is valid)
    auto r = app.loadLearningDatabase(tmp.string());
    CHECK_TRUE(r.succeeded());
    std::filesystem::remove(tmp);
    return failures;
}

// Wave 1 regression: DQS blending with multiple influences must not crash
// and must return a finite result (the old code had a dead branch where
// `first` was never cleared inside the inner if, causing incorrect blending
// for 2+ influences — the fix restructured the loop to properly accumulate).
M2RIG_TEST(app, dqs_blend_multi_influence_no_crash) {
    int failures = 0;
    Skeleton skel;
    Bone b0; b0.id = 0; b0.name = "root"; b0.parentId = -1;
    b0.localPosition = Vec3(0,0,0); b0.localRotationEuler = Vec3(0,0,0); b0.localScale = Vec3(1,1,1);
    Bone b1; b1.id = 1; b1.name = "child"; b1.parentId = 0;
    b1.localPosition = Vec3(0,0,0); b1.localRotationEuler = Vec3(0,0,1.5707963f); b1.localScale = Vec3(1,1,1);
    skel.bones = {b0, b1};
    rebuildSkeletonRuntime(skel);
    std::vector<Mat4> bindInv;
    for (const auto& b : skel.bones) bindInv.push_back(b.inverseBindTransform);
    auto palette = buildDqsPalette(skel, bindInv);
    std::vector<BoneInfluence> infs = {{0, 0.5f}, {1, 0.5f}};
    Vec3 result = deformVertexDqs(Vec3(1,0,0), infs, palette);
    // Must return a finite result (not NaN/Inf)
    CHECK_TRUE(std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z));
    return failures;
}
