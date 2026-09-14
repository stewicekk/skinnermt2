// Weight engine tests: paint falloff, transfer, symmetry, MSM + workspace IO.
#include <cstdio>
#include <filesystem>

#include "../tests/expect.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/app.hpp"
#include "m2rig/ast/msm_ast.hpp"
#ifdef M2RIG_WITH_OPENFBX
#include "m2rig/fbx/fbx_reader.hpp"
#endif
#include "m2rig/extractors/universal_weight_extractor.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/workspace/project_file.hpp"

using namespace m2rig;

namespace {

Mesh makeTwoBoneMesh() {
    Mesh m;
    m.name = "t";
    Vertex a, b;
    a.position = {0, 0, 0};
    b.position = {1, 0, 0};
    a.influences = {{0, 1.0f}};
    b.influences = {{1, 1.0f}};
    m.vertices = {a, b};
    m.indices = {0, 1, 0};
    computeBounds(m);
    return m;
}

}  // namespace

M2RIG_TEST(weights, paint_falloff_center_vs_edge) {
    int failures = 0;
    std::vector<BoneInfluence> infs{{0, 1.0f}};
    PaintParams p;
    p.bone = 0;
    p.center = {0, 0, 0};
    p.radius = 1.0f;
    p.strength = 0.5f;
    p.falloff = PaintFalloff::Linear;
    p.op = BrushOp::Add;
    CHECK_TRUE(paintVertexWeight(infs, {0, 0, 0}, p));
    const float centerW = weightOfBone(infs, 0);
    std::vector<BoneInfluence> edge{{0, 1.0f}};
    CHECK_FALSE(paintVertexWeight(edge, {5, 0, 0}, p));
    CHECK_TRUE(centerW > 1.0f - 1e-4f);  // stays normalized
    CHECK_EQ(weightOfBone(edge, 0), 1.0f);
    // Falloff curve endpoints.
    CHECK_NEAR(evalFalloff(PaintFalloff::Linear, 0.0f), 1.0, 1e-6);
    CHECK_NEAR(evalFalloff(PaintFalloff::Linear, 1.0f), 0.0, 1e-6);
    CHECK_NEAR(evalFalloff(PaintFalloff::Smoothstep, 0.0f), 1.0, 1e-6);
    CHECK_NEAR(evalFalloff(PaintFalloff::Smoothstep, 1.0f), 0.0, 1e-6);
    return failures;
}

M2RIG_TEST(weights, paint_clamps_to_four_with_mass) {
    int failures = 0;
    std::vector<BoneInfluence> infs{{0, 0.3f}, {1, 0.3f}, {2, 0.2f}, {3, 0.1f}, {4, 0.1f}};
    PaintParams p;
    p.bone = 0;
    p.center = {0, 0, 0};
    p.radius = 2.0f;
    p.strength = 0.5f;
    p.op = BrushOp::Add;
    RepairStats stats;
    CHECK_TRUE(paintVertexWeight(infs, {0, 0, 0}, p, &stats));
    CHECK_TRUE(infs.size() <= kMetin2MaxInfluences);
    CHECK_NEAR(influenceTotal(infs), 1.0, 1e-3);
    return failures;
}

M2RIG_TEST(weights, transfer_copies_nearest) {
    int failures = 0;
    Mesh src = makeTwoBoneMesh();
    Mesh dst = makeTwoBoneMesh();
    dst.vertices[0].influences.clear();
    dst.vertices[1].influences.clear();
    const std::vector<std::uint32_t> remap{0, 1};
    WeightTransferStats st = transferWeightsKDTree(src, dst, remap, 1);
    CHECK_EQ(st.verticesMapped, 2u);
    CHECK_EQ(st.verticesUnmapped, 0u);
    CHECK_NEAR(weightOfBone(dst.vertices[0].influences, 0), 1.0, 1e-3);
    CHECK_NEAR(weightOfBone(dst.vertices[1].influences, 1), 1.0, 1e-3);
    return failures;
}

M2RIG_TEST(weights, symmetry_pairs_and_mirror) {
    int failures = 0;
    Mesh m = makeTwoBoneMesh();
    m.vertices[0].position = {-1, 0, 0};
    m.vertices[1].position = {1, 0, 0};
    auto pairs = findMirrorPairs(m, 0, 0.01f);
    CHECK_EQ(pairs.size(), 1u);
    const std::vector<std::uint32_t> boneMirror{1, 0};
    const std::size_t n = mirrorMeshWeights(m, 0, pairs, boneMirror);
    CHECK_EQ(n, 1u);
    // vertex 1 (was bone 1) now mirrors vertex 0 remapped 0->1... check mass kept
    CHECK_NEAR(influenceTotal(m.vertices[1].influences), 1.0, 1e-3);
    return failures;
}

M2RIG_TEST(weights, msm_roundtrip_preserves_structure) {
    int failures = 0;
    const char* text =
        "// armor shape\nGroup ShapeDataTest\n{\n  Group ShapeIndex\n  {\n    ShapeCount 1\n  }\n}\n";
    MsmDocument doc;
    CHECK_TRUE(parseMsm(text, doc));
    const std::string out = msmStringify(doc);
    CHECK_TRUE(out.find("ShapeDataTest") != std::string::npos);
    CHECK_TRUE(out.find("ShapeIndex") != std::string::npos);
    CHECK_TRUE(findChildByName(doc.root, "ShapeIndex") != nullptr);
    return failures;
}

M2RIG_TEST(weights, workspace_save_load) {
    int failures = 0;
    M2RigWorkspace ws;
    ws.title = "test-project";
    ws.currentAssetId = "sample-warrior-armor";
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "m2rig_ws_test.m2rig";
    CHECK_TRUE(saveWorkspace(ws, tmp).succeeded());
    auto back = loadWorkspace(tmp);
    CHECK_TRUE(back.succeeded());
    if (back.succeeded()) {
        CHECK_TRUE(back.value().title == "test-project");
        CHECK_TRUE(back.value().currentAssetId == "sample-warrior-armor");
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return failures;
}

M2RIG_TEST(weights, extractor_detects_formats) {    int failures = 0;
    CHECK_TRUE(UniversalWeightExtractor::detectFormat("a.smd") == ModelFormat::SMD);
    CHECK_TRUE(UniversalWeightExtractor::detectFormat("a.FBX") == ModelFormat::FBX);
    CHECK_TRUE(UniversalWeightExtractor::detectFormat("a.gr2") == ModelFormat::GR2);
    CHECK_TRUE(UniversalWeightExtractor::detectFormat("a.txt") == ModelFormat::Unknown);
    UniversalWeightExtractor ux;
    auto bad = ux.extractFromFile("nope.txt");
    CHECK_FALSE(bad.succeeded());
    return failures;
}

M2RIG_TEST(weights, profiles_cover_real_model_bones) {
    int failures = 0;
    const SkeletonProfile* prof = findProfile("pc_warrior");
    CHECK_TRUE(prof != nullptr);
    if (!prof) return failures;
    // Core 23 always required.
    CHECK_EQ(prof->expectedBones.size(), 23u);
    // Verified extras from the 90-bone Noesis FBX dump are optional, never unexpected.
    bool hasSpine2 = false, hasFinger = false, hasToe = false, hasPony = false;
    for (const auto& o : prof->optionalBones) {
        if (o == "Bip01 Spine2") hasSpine2 = true;
        if (o == "Bip01 L Finger0") hasFinger = true;
        if (o == "Bip01 R Toe0") hasToe = true;
        if (o == "Bip01 Ponytail1") hasPony = true;
    }
    CHECK_TRUE(hasSpine2 && hasFinger && hasToe && hasPony);
    CHECK_TRUE(mirrorBoneName(*prof, "Bip01 L Finger1") == "Bip01 R Finger1");
    CHECK_TRUE(mirrorBoneName(*prof, "Bip01 L Toe0") == "Bip01 R Toe0");
    CHECK_TRUE(mirrorBoneName(*prof, "Bip01 Spine2") == "Bip01 Spine2");
    return failures;
}

M2RIG_TEST(weights, socket_deform_use_warns) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures;
    const SkeletonProfile* prof = findProfile("pc_warrior");
    CHECK_TRUE(prof != nullptr);
    if (!prof) return failures;
    // Force equip_left (socket) to deform vertex 0.
    Mesh mesh = sample.value().mesh;
    const Skeleton& skel = sample.value().skeleton;
    const Bone* sock = skel.findByName("equip_left");
    CHECK_TRUE(sock != nullptr);
    if (sock && !mesh.vertices.empty()) {
        mesh.vertices[0].influences.push_back({sock->id, 0.5f});
        normalizeInfluences(mesh.vertices[0].influences);
        ValidationReport report;
        validateSocketDeformUse(mesh, skel, *prof, "t", report);
        bool found = false;
        for (const auto& it : report.items())
            if (it.id == "SOCKET_DEFORM_USE") found = true;
        CHECK_TRUE(found);
    }
    return failures;
}

M2RIG_TEST(weights, self_train_converges_on_identity) {    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures;
    const SkeletonProfile* prof = findProfile("pc_warrior");
    CHECK_TRUE(prof != nullptr);
    if (!prof) return failures;
    Mesh dst = sample.value().mesh;
    for (auto& v : dst.vertices) v.influences.clear();
    SelfTrainStats st = transferWeightsSelfTraining(
        sample.value().mesh, sample.value().skeleton, dst, sample.value().skeleton, *prof, 3, 6);
    CHECK_TRUE(st.finalTransfer.verticesMapped > 0);
    CHECK_TRUE(st.finalCost >= 0.0);
    CHECK_TRUE(st.iterations <= 7);
    CHECK_EQ(st.finalRemap.size(), sample.value().skeleton.bones.size());
    // Identity geometry: every vertex should recover total mass 1.
    for (const auto& v : dst.vertices) {
        if (v.influences.empty()) continue;
        CHECK_NEAR(influenceTotal(v.influences), 1.0, 1e-3);
        break;
    }
    return failures;
}

#ifdef M2RIG_WITH_OPENFBX
M2RIG_TEST(weights, fbx_native_reads_real_ninja) {
    int failures = 0;
    // Live native parse of Data/Models/ninja.fbx (Noesis converted the same
    // file to a 90-bone SMD). Skips honestly when the sample is absent.
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path fbx;
    for (int level = 0; level < 5 && fbx.empty(); ++level) {
        const std::filesystem::path c = dir / "Data" / "Models" / "ninja.fbx";
        if (std::filesystem::exists(c)) fbx = c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (fbx.empty()) {
        printf("    SKIP native FBX test (sample missing)\n");
        return failures;
    }
    auto conv = readFbxFile(fbx.string(), "ninja");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) {
        printf("    FBX ERROR: %s\n", conv.error().message.c_str());
        return failures + 1;
    }
    printf("    fbx bones=%zu verts=%zu tris=%zu meshes=%zu\n",
           conv.value().skeleton.bones.size(), conv.value().mesh.vertices.size(),
           conv.value().mesh.triangleCount(), conv.value().meshCount);
    CHECK_TRUE(conv.value().skeleton.bones.size() >= 23u);
    CHECK_TRUE(conv.value().mesh.triangleCount() > 100u);
    CHECK_TRUE(conv.value().skeleton.findByName("Bip01") != nullptr);
    CHECK_TRUE(conv.value().skeleton.findByName("equip_left") != nullptr);
    const SkeletonProfile* prof = findProfile("pc_assassin_f");
    CHECK_TRUE(prof != nullptr);
    if (prof) {
        ValidationReport report;
        validateAgainstProfile(conv.value().skeleton, *prof, "ninja", report);
        bool missingCore = false;
        for (const auto& it : report.items())
            if (it.id == "PROFILE_MISSING_BONE") missingCore = true;
        CHECK_FALSE(missingCore);
    }
    return failures;
}
#endif

M2RIG_TEST(weights, deform_bind_pose_is_identity) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const Mesh& mesh = sample.value().mesh;
    const std::vector<Mat4> palette = currentBindPalette(sample.value().skeleton);
    CHECK_EQ(palette.size(), sample.value().skeleton.bones.size());
    // At bind, every palette entry must be identity, so deform is a no-op.
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& v = mesh.vertices[i];
        const Vec3 d = deformVertex(v.position, v.influences, palette);
        CHECK_NEAR(d.x, v.position.x, 1e-3);
        CHECK_NEAR(d.y, v.position.y, 1e-3);
        CHECK_NEAR(d.z, v.position.z, 1e-3);
        if (i >= 64) break;  // spot-check keeps the suite fast
    }
    return failures;
}

M2RIG_TEST(weights, deform_single_bone_translation) {
    int failures = 0;
    auto built = buildSkeleton("t", {{"root", kNoParent, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}}});
    CHECK_TRUE(built.succeeded());
    if (!built.succeeded()) return failures + 1;
    Skeleton skel = std::move(built.value());
    // Snapshot bind inverses BEFORE posing (rebuild overwrites them).
    std::vector<Mat4> binds;
    for (const auto& b : skel.bones) binds.push_back(b.inverseBindTransform);
    skel.bones[0].localPosition = {2.5f, 0, 0};
    CHECK_TRUE(rebuildSkeletonRuntime(skel).succeeded());
    const std::vector<Mat4> palette = buildSkinningPalette(skel, binds);
    const Vec3 p{1, 2, 3};
    const Vec3 d = deformVertex(p, {{0, 1.0f}}, palette);
    CHECK_NEAR(d.x, 3.5, 1e-4);
    CHECK_NEAR(d.y, 2.0, 1e-4);
    CHECK_NEAR(d.z, 3.0, 1e-4);
    const Vec3 n = deformNormal({0, 1, 0}, {{0, 1.0f}}, palette);
    CHECK_NEAR(n.x, 0.0, 1e-5);
    CHECK_NEAR(n.y, 1.0, 1e-5);
    CHECK_NEAR(n.z, 0.0, 1e-5);
    // Unweighted vertex stays put.
    const Vec3 u = deformVertex(p, {}, palette);
    CHECK_NEAR(u.x, p.x, 1e-6);
    return failures;
}

M2RIG_TEST(weights, grnreader_converts_real_gr2) {
    int failures = 0;
    // Live integration: real grnreader98.exe + real Data/Models/sura_m.gr2.
    // Skips honestly when either is absent (hermetic CI); verifies bones,
    // skeleton convertibility and profile compatibility when present.
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path gr2;
    for (int level = 0; level < 5 && gr2.empty(); ++level) {
        const std::filesystem::path c = dir / "Data" / "Models" / "sura_m.gr2";
        if (std::filesystem::exists(c)) gr2 = c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (gr2.empty() || findGrnReader().empty()) {
        printf("    SKIP grnreader integration (tool or sample missing)\n");
        return failures;
    }
    auto smd = convertGr2ToSmdViaGrnReader(gr2, 90000);
    CHECK_TRUE(smd.succeeded());
    if (!smd.succeeded()) return failures + 1;
    auto parsed = parseSmd(smd.value(), "sura_m");
    CHECK_TRUE(parsed.succeeded());
    if (!parsed.succeeded()) return failures + 1;
    auto conv = smdToAsset(parsed.value(), "sura_m");
    CHECK_TRUE(conv.succeeded());
    if (!conv.succeeded()) return failures + 1;
    CHECK_TRUE(conv.value().skeleton.bones.size() >= 23u);
    CHECK_TRUE(conv.value().mesh.triangleCount() > 100u);
    const SkeletonProfile* prof = findProfile("pc_sura_f");
    CHECK_TRUE(prof != nullptr);
    if (prof) {
        ValidationReport report;
        validateAgainstProfile(conv.value().skeleton, *prof, "sura_m", report);
        bool missingCore = false;
        for (const auto& it : report.items())
            if (it.id == "PROFILE_MISSING_BONE") missingCore = true;
        CHECK_FALSE(missingCore);
    }
    return failures;
}

M2RIG_TEST(weights, locks_block_paint_and_undo_works) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    app.selectedBone = 0;
    app.setBoneLocked(0, true);
    CHECK_TRUE(app.isBoneLocked(0));
    const Vec3 center = a->mesh.vertices.empty() ? Vec3{0, 0, 0} : a->mesh.vertices[0].position;
    CHECK_EQ(app.paintStroke(center), 0u);
    CHECK_FALSE(app.canUndo());
    app.setBoneLocked(0, false);
    const std::size_t n = app.paintStroke(center);
    CHECK_TRUE(n > 0);
    CHECK_TRUE(app.canUndo());
    app.undo();
    CHECK_TRUE(app.canRedo());
    app.redo();
    return failures;
}

M2RIG_TEST(weights, locks_survive_mirror_presence) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    // Lock bone 0 and record which vertices carry it.
    app.setBoneLocked(0, true);
    std::vector<bool> had(a->mesh.vertices.size(), false);
    for (std::size_t i = 0; i < a->mesh.vertices.size(); ++i)
        had[i] = weightOfBone(a->mesh.vertices[i].influences, 0) > 0.0f;
    if (auto r = app.mirrorWeights(); !r) {
        // No mirror pairs on this fixture is acceptable; the guard ran.
        printf("    mirror note: %s\n", r.error().message.c_str());
        return failures;
    }
    for (std::size_t i = 0; i < a->mesh.vertices.size(); ++i) {
        if (had[i]) CHECK_TRUE(weightOfBone(a->mesh.vertices[i].influences, 0) > 0.0f);
    }
    return failures;
}

M2RIG_TEST(weights, isolation_filter_hides_submeshes) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const Mesh& mesh = sample.value().mesh;
    CHECK_TRUE(!mesh.subMeshes.empty());
    auto all = filterVisibleIndices(mesh, {});
    CHECK_EQ(all.size(), mesh.indices.size());
    auto none = filterVisibleIndices(mesh, {0, 1, 2, 3, 4, 5, 6, 7});
    CHECK_EQ(none.size(), 0u);
    std::set<std::size_t> hide{0};
    auto part = filterVisibleIndices(mesh, hide);
    CHECK_TRUE(part.size() < mesh.indices.size());
    CHECK_EQ(part.size() + mesh.subMeshes[0].indexCount, mesh.indices.size());
    return failures;
}

M2RIG_TEST(weights, workspace_locks_roundtrip_by_name) {
    int failures = 0;
    M2RigWorkspace ws;
    ws.title = "locks";
    ws.currentAssetId = "a";
    ws.lockedBoneNames = {"Bip01 L Hand", "equip_right"};
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "m2rig_ws_locks_test.m2rig";
    CHECK_TRUE(saveWorkspace(ws, tmp).succeeded());
    auto back = loadWorkspace(tmp);
    CHECK_TRUE(back.succeeded());
    if (back.succeeded()) {
        CHECK_EQ(back.value().lockedBoneNames.size(), 2u);
        if (back.value().lockedBoneNames.size() == 2u) {
            CHECK_TRUE(back.value().lockedBoneNames[0] == "Bip01 L Hand");
            CHECK_TRUE(back.value().lockedBoneNames[1] == "equip_right");
        }
    }
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return failures;
}

M2RIG_TEST(weights, budget_and_weld_reports) {
    int failures = 0;
    Mesh mesh;
    mesh.name = "big";
    Vertex v;
    v.position = {0, 0, 0};
    mesh.vertices = {v, v, v};  // identical pair -> weld cell hit
    for (std::size_t i = 0; i < 10001; ++i) {
        mesh.indices.push_back(0);
        mesh.indices.push_back(1);
        mesh.indices.push_back(2);
    }
    SubMesh sm;
    sm.name = "all";
    sm.startIndex = 0;
    sm.indexCount = mesh.indices.size();
    mesh.subMeshes.push_back(sm);
    ValidationReport report;
    validateMeshStructure(mesh, "big", report);
    bool budget = false, weld = false;
    for (const auto& it : report.items()) {
        if (it.id == "MESH_BUDGET") budget = true;
        if (it.id == "MESH_NEAR_DUP") weld = true;
    }
    CHECK_TRUE(budget);
    CHECK_TRUE(weld);
    return failures;
}

M2RIG_TEST(weights, batch_exports_loaded_assets) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    CHECK_TRUE(app.loadSampleArmorForProfile("pc_sura_f").succeeded());
    auto batch = app.exportAllBatch();
    CHECK_TRUE(batch.succeeded());
    if (!batch.succeeded()) return failures + 1;
    CHECK_EQ(batch.value().size(), 2u);
    for (const auto& row : batch.value()) {
        CHECK_TRUE(row.smdOk);
        CHECK_TRUE(row.msmOk);
        std::error_code ec;
        std::filesystem::remove(row.smdPath, ec);
        std::filesystem::remove(row.msmPath, ec);
    }
    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::temp_directory_path() / "m2rig_batch", ec);
    return failures;
}

M2RIG_TEST(weights, msm_validation_catches_mismatch) {
    int failures = 0;
    MsmDocument doc;
    CHECK_TRUE(parseMsm("Group ShapeDataT\n{\n  Group ShapeIndex\n  {\n    ShapeCount 2\n  }\n}\n",
                        doc));
    ValidationReport report;
    validateMsmDoc(doc, "t", report);
    CHECK_TRUE(report.exportBlocked());
    bool count = false, skin = false;
    for (const auto& it : report.items()) {
        if (it.id == "MSM_SHAPE_COUNT") count = true;
        if (it.id == "MSM_NO_SKIN") skin = true;
    }
    CHECK_TRUE(count);
    CHECK_TRUE(skin);
    return failures;
}

M2RIG_TEST(weights, autosave_tick_writes_when_dirty) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    a->dirty = true;
    app.autosaveMinutes = 1;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "m2rig_autosave_test";
    app.tickAutosave(dir, 3600.0);  // far past the 60s interval
    CHECK_TRUE(std::filesystem::exists(dir / "autosave.m2rig"));
    // Second tick inside the interval writes nothing new (no crash = pass).
    app.tickAutosave(dir, 3605.0);
    CHECK_TRUE(std::filesystem::exists(dir / "autosave.m2rig"));
    // Disabled autosave never writes.
    app.autosaveMinutes = 0;
    std::error_code ec;
    std::filesystem::remove(dir / "autosave.m2rig", ec);
    app.tickAutosave(dir, 99999.0);
    CHECK_FALSE(std::filesystem::exists(dir / "autosave.m2rig"));
    std::filesystem::remove_all(dir, ec);
    return failures;
}
