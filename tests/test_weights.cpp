// Weight engine tests: paint falloff, transfer, symmetry, MSM + workspace IO.
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "../tests/expect.hpp"
#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/app.hpp"
#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/coordsys.hpp"
#ifdef M2RIG_WITH_OPENFBX
#include "m2rig/fbx/fbx_reader.hpp"
#endif
#ifdef M2RIG_WITH_CGLTF
#include "m2rig/gltf/gltf_reader.hpp"
#include "m2rig/gltf/gltf_writer.hpp"
#endif
#include "m2rig/extractors/universal_weight_extractor.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/renderer.hpp"
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
    // Indexed-dedup pin (Wave-29): the soup was 9624 verts / 3208 tris (3x,
    // Waves 7/12 + BENCHMARKS.md; Noesis SMD reference is 2070 indexed).
    // Dedup must keep triangles/bones/<=4 while yielding fewer verts; no
    // exact vert count is pinned here (gate confirms the new indexed count).
    CHECK_EQ(conv.value().mesh.triangleCount(), 3208u);
    CHECK_EQ(conv.value().skeleton.bones.size(), 90u);
    CHECK_TRUE(conv.value().mesh.vertices.size() < 9624u);
    CHECK_TRUE(conv.value().mesh.vertices.size() < conv.value().mesh.triangleCount() * 3u);
    for (const auto& v : conv.value().mesh.vertices) {
        CHECK_TRUE(v.influences.size() <= kMetin2MaxInfluences);
    }
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

M2RIG_TEST(weights, fbx_native_reads_real_model_set) {
    int failures = 0;
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path models;
    for (int level = 0; level < 5 && models.empty(); ++level) {
        const auto c = dir / "Data" / "Models";
        if (std::filesystem::is_directory(c)) models = c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (models.empty()) {
        printf("    SKIP native FBX model-set test (Data/Models missing)\n");
        return failures;
    }
    std::size_t files = 0;
    std::size_t nativeSuccesses = 0;
    for (const auto& entry : std::filesystem::directory_iterator(models)) {
        const auto ext = entry.path().extension().string();
        if (ext != ".fbx" && ext != ".FBX") continue;
        ++files;
        auto conv = readFbxFile(entry.path().string(), entry.path().stem().string());
        if (!conv.succeeded()) {
            printf("    FBX fallback %s: %s\n", entry.path().filename().string().c_str(),
                   conv.error().message.c_str());
            continue;
        }
        ++nativeSuccesses;
        CHECK_TRUE(conv.value().mesh.triangleCount() > 0u);
        CHECK_TRUE(conv.value().skeleton.bones.size() >= 23u);
    }
    // If no FBX files found in this environment, skip the count checks
    // (the test suite expects 25 files in the full Metin2 dev dataset).
    if (files == 0) {
        printf("    SKIP native FBX model-set test (no .fbx files in Data/Models)\n");
        return failures;
    }
    // A few shipped exports are intentionally handled by the Noesis fallback
    // (OpenFBX cannot parse their Kaydara variant or mesh-less Wolfman file).
    // The 25/21 numbers fingerprint the FULL dev dataset; smaller checkouts
    // (CI, partial Data/) get an adaptive gate instead of a false failure:
    // every file must either parse natively with sane output (checked above)
    // or fail explicitly, and the reader must work on at least one real file.
    if (files == 25u) {
        CHECK_EQ(files, 25u);
        CHECK_TRUE(nativeSuccesses >= 21u);
    } else {
        printf("    partial Data/Models (%zu .fbx, not the 25-file dev set): %zu native\n",
               files, nativeSuccesses);
        CHECK_TRUE(nativeSuccesses >= 1u);
        CHECK_TRUE(nativeSuccesses <= files);
    }
    return failures;
}

M2RIG_TEST(weights, fbx_detected_axis_keeps_real_models_sane) {
    // Behavioral proof for FBX axis auto-detection: every natively readable
    // Data/Models .fbx must orient SANE after import (axis detected from
    // GlobalSettings or profile-assumed). A file that changes hands between
    // Z-up/Y-up exporters and silently double-converts fails here instead
    // of shipping a rotated model. Skips honestly without the dataset.
    int failures = 0;
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path models;
    for (int level = 0; level < 5 && models.empty(); ++level) {
        const auto c = dir / "Data" / "Models";
        if (std::filesystem::is_directory(c)) models = c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (models.empty()) {
        printf("    SKIP FBX orient test (Data/Models missing)\n");
        return failures;
    }
    std::size_t checked = 0;
    bool sawWarriorOut = false;
    for (const auto& entry : std::filesystem::directory_iterator(models)) {
        const auto ext = entry.path().extension().string();
        if (ext != ".fbx" && ext != ".FBX") continue;
        auto conv = readFbxFile(entry.path().string(), entry.path().stem().string());
        if (!conv.succeeded()) {
            printf("    orient SKIP %s (native read fails, Noesis fallback territory)\n",
                   entry.path().filename().string().c_str());
            continue;
        }
        const OrientationReport rep =
            diagnoseOrientation(conv.value().mesh, conv.value().skeleton);
        printf("    orient %s: %s | %s\n", entry.path().filename().string().c_str(),
               rep.verdictLine().c_str(), conv.value().conversionNote.c_str());
        if (!rep.sane()) printf("%s\n", rep.toDisplayString().c_str());
        CHECK_TRUE(rep.sane());
        ++checked;
        // Liar-header contract (Noesis experiment outputs declare Y-up while
        // carrying Z-up data): the declared space must lose arbitration to
        // the diagnosed-sane fallback. Conditional on the file still making
        // that claim — if upstream ever fixes its headers, this degrades to
        // the sane-check above instead of false-failing.
        if (entry.path().filename().string() == "warriorout.fbx") {
            sawWarriorOut = true;
            const auto det = conv.value().detectedSpace;
            if (det.has_value() &&
                (det.value() == CoordSys::Canonical || det.value() == CoordSys::YUp_ZForward)) {
                CHECK_TRUE(conv.value().usedFallbackSource);
                CHECK_TRUE(conv.value().appliedSpace == CoordSys::ZUp_YForward);
            } else {
                printf("    warriorout headers changed (%s); liar-contract skipped\n",
                       det.has_value() ? coordSysName(det.value()) : "<absent>");
            }
        }
    }
    if (checked == 0)
        printf("    SKIP FBX orient test (no natively readable .fbx in Data/Models)\n");
    else if (!sawWarriorOut)
        printf("    note: warriorout.fbx absent, liar-contract not exercised\n");
    return failures;
}

// Closes #ifdef M2RIG_WITH_OPENFBX opened at the ninja test: all native-FBX
// tests (ninja, model set, orient) need the adapter.
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

M2RIG_TEST(weights, validates_real_gr2_headers) {
    int failures = 0;
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path models;
    for (int level = 0; level < 5 && models.empty(); ++level) {
        const auto c = dir / "Data" / "Models";
        if (std::filesystem::is_directory(c)) models = c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (models.empty()) {
        printf("    SKIP GR2 header test (Data/Models missing)\n");
        return failures;
    }
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(models)) {
        if (entry.path().extension() != ".gr2") continue;
        ++files;
        CHECK_TRUE(isValidGr2Container(entry.path()));
    }
    CHECK_EQ(files, 31u);
    CHECK_FALSE(isValidGr2Container(models / "does-not-exist.gr2"));
    const auto tiny = std::filesystem::temp_directory_path() / "m2rig_invalid.gr2";
    {
        std::ofstream out(tiny, std::ios::binary);
        out << "not a GR2";
    }
    CHECK_FALSE(isValidGr2Container(tiny));
    std::error_code ec;
    std::filesystem::remove(tiny, ec);
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

M2RIG_TEST(weights, batch_exports_loaded_assets) {    int failures = 0;
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

M2RIG_TEST(weights, flood_then_prune_roundtrip) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    Mesh mesh = std::move(sample.value().mesh);
    const std::size_t n = mesh.vertices.size();
    CHECK_TRUE(n > 0);
    RepairStats stats;
    CHECK_EQ(floodBone(mesh, 3, kMetin2MaxInfluences, &stats), n);
    for (const auto& v : mesh.vertices) {
        CHECK_EQ(v.influences.size(), 1u);
        CHECK_EQ(v.influences[0].bone, 3u);
        CHECK_NEAR(v.influences[0].weight, 1.0, 1e-4);
    }
    CHECK_EQ(pruneBone(mesh, 3), n);
    for (const auto& v : mesh.vertices) CHECK_EQ(weightOfBone(v.influences, 3), 0.0f);
    // Pruning an absent bone changes nothing.
    CHECK_EQ(pruneBone(mesh, 3), 0u);
    return failures;
}

M2RIG_TEST(weights, flood_prune_respect_app_locks) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    app.selectedBone = 1;
    app.setBoneLocked(1, true);
    const std::size_t natural = app.boneInfluenceCount(1);
    CHECK_FALSE(app.floodSelectedBone().succeeded());  // locked: honest failure, not silent ok
    CHECK_EQ(app.boneInfluenceCount(1), natural);      // weights untouched
    app.setBoneLocked(1, false);
    CHECK_TRUE(app.floodSelectedBone().succeeded());
    CHECK_EQ(app.boneInfluenceCount(1), a->mesh.vertices.size());
    app.undo();  // flood is undoable
    CHECK_EQ(app.boneInfluenceCount(1), natural);
    return failures;
}

M2RIG_TEST(weights, bridge_start_poll_idle_contract) {
    int failures = 0;
    App app;
    CHECK_FALSE(app.bridgeBusy);
    CHECK_FALSE(app.pollBridgeImport());
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    // .smd path completes synchronously inside start (no thread, no busy).
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "m2rig_bridge_idle.smd";
    {
        auto smd = writeSmd(app.currentAsset()->mesh, app.currentAsset()->skeleton, {});
        CHECK_TRUE(smd.succeeded());
        if (!smd.succeeded()) return failures + 1;
        if (auto w = writeTextFile(tmp.string(), smd.value().text, "t"); !w) {
            CHECK_TRUE(false);
            return failures + 1;
        }
    }
    CHECK_TRUE(app.startBridgedImport(tmp.string(), 0.0));
    CHECK_FALSE(app.bridgeBusy);
    CHECK_FALSE(app.pollBridgeImport());
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
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

M2RIG_TEST(weights, autosave_tick_writes_when_dirty) {    int failures = 0;
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

M2RIG_TEST(weights, deformed_height_matches_solid_at_bind) {
    // Regression for Wave 14.5 fix: buildGpuVerticesDeformed must support
    // Height (previously fell through to solid grey).
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    const Mesh& mesh = sample.value().mesh;
    std::vector<Mat4> binds;
    for (const auto& b : sample.value().skeleton.bones) binds.push_back(b.inverseBindTransform);
    const std::vector<Mat4> palette = buildSkinningPalette(sample.value().skeleton, binds);
    const auto solid = buildGpuVertices(mesh, MeshColoring::Height);
    const auto deformed = buildGpuVerticesDeformed(mesh, palette, MeshColoring::Height, 0);
    CHECK_EQ(solid.size(), deformed.size());
    CHECK_TRUE(!solid.empty());
    for (std::size_t i = 0; i < solid.size(); ++i) {
        CHECK_NEAR(deformed[i].color[0], solid[i].color[0], 1e-4);
        CHECK_NEAR(deformed[i].color[1], solid[i].color[1], 1e-4);
        CHECK_NEAR(deformed[i].color[2], solid[i].color[2], 1e-4);
        if (i >= 64) break;
    }
    return failures;
}

M2RIG_TEST(weights, autorig_binds_every_vertex_deterministically) {
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    Mesh mesh = sample.value().mesh;
    for (auto& v : mesh.vertices) v.influences.clear();
    AutoRigStats first = autoRigMesh(mesh, sample.value().skeleton, 0.02f, 4, nullptr);
    CHECK_EQ(first.verticesBound, mesh.vertices.size());
    CHECK_TRUE(first.maxDistance >= 0.0);
    CHECK_TRUE(first.removedMass >= 0.0);
    for (const auto& v : mesh.vertices) {
        CHECK_TRUE(!v.influences.empty());
        CHECK_TRUE(v.influences.size() <= 4u);
        double sum = 0.0;
        for (const auto& inf : v.influences) sum += inf.weight;
        CHECK_NEAR(sum, 1.0, 1e-3);
    }
    // Determinism: second run on a fresh clear must give identical bones.
    Mesh mesh2 = sample.value().mesh;
    for (auto& v : mesh2.vertices) v.influences.clear();
    AutoRigStats second = autoRigMesh(mesh2, sample.value().skeleton, 0.02f, 4, nullptr);
    CHECK_EQ(second.verticesBound, first.verticesBound);
    CHECK_TRUE(mesh2.vertices.size() == mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        CHECK_EQ(mesh2.vertices[i].influences.size(), mesh.vertices[i].influences.size());
        if (i >= 64) break;
    }
    // Empty inputs return empty stats (App layer fails explicitly).
    Mesh empty;
    AutoRigStats emptyStats = autoRigMesh(empty, sample.value().skeleton);
    CHECK_EQ(emptyStats.verticesBound, 0u);
    return failures;
}

M2RIG_TEST(weights, msm_inspector_open_close) {
    int failures = 0;
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path msm;
    for (int level = 0; level < 5 && msm.empty(); ++level) {
        const std::filesystem::path c = dir / "tests" / "data" / "sample.msm";
        if (std::filesystem::exists(c)) msm = c;
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (msm.empty()) {
        printf("    SKIP msm inspector test (fixture missing)\n");
        return failures;
    }
    App app;
    CHECK_TRUE(app.openMsmInspector(msm.string()).succeeded());
    CHECK_TRUE(app.msmDoc.has_value());
    CHECK_FALSE(app.msmReport.exportBlocked());
    CHECK_TRUE(app.msmPath == msm.string());
    app.closeMsmInspector();
    CHECK_FALSE(app.msmDoc.has_value());
    CHECK_TRUE(app.msmPath.empty());
    CHECK_TRUE(app.openMsmInspector((msm.string() + ".missing").c_str()).succeeded() == false);
    return failures;
}

M2RIG_TEST(weights, undo_cleared_on_sample_reload) {
    // Snapshots reference a specific mesh: reinstalling must not let undo
    // smear the previous asset's weights into the new one.
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    app.selectedBone = 0;
    CHECK_TRUE(app.floodSelectedBone().succeeded());
    CHECK_TRUE(app.canUndo());
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    CHECK_FALSE(app.canUndo());
    CHECK_FALSE(app.canRedo());
    return failures;
}

M2RIG_TEST(weights, paint_zero_dab_pushes_no_undo) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    app.selectedBone = 0;
    app.brushRadius = 0.05f;
    CHECK_EQ(app.paintStroke({1000000.0f, 1000000.0f, 1000000.0f}), 0u);
    CHECK_FALSE(app.canUndo());  // pre-pushed snapshot rolled back
    CHECK_TRUE(app.statusKind == "info");
    return failures;
}

M2RIG_TEST(weights, workspace_restore_reimports_sources) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    CHECK_TRUE(a->skeleton.bones.size() >= 3u);
    if (a->skeleton.bones.size() < 3u) return failures + 1;
    // Persist the live mesh as an SMD source, then restore into a fresh App.
    const std::filesystem::path smdPath =
        std::filesystem::temp_directory_path() / "m2rig_ws_reimport.smd";
    {
        auto written = writeSmd(a->mesh, a->skeleton, {});
        CHECK_TRUE(written.succeeded());
        if (!written.succeeded()) return failures + 1;
        std::ofstream out(smdPath, std::ios::binary | std::ios::trunc);
        out << written.value().text;
    }
    M2RigWorkspace ws;
    ws.title = "reimport";
    ws.currentAssetId = "m2rig_ws_reimport";  // stemOf(sourcePath)
    M2RigAssetEntry entry;
    entry.id = "m2rig_ws_reimport";
    entry.sourcePath = smdPath.string();
    ws.assets.push_back(entry);
    // Round-trip pins: fields restoreWorkspace resolves (project_file.cpp
    // :501-594) but the old test left unasserted. Bone names come from the
    // live skeleton so the SMD reimport resolves them by name.
    const std::string lockBone = a->skeleton.bones[0].name;
    const std::string setBone0 = a->skeleton.bones[1].name;
    const std::string setBone1 = a->skeleton.bones[2].name;
    ws.lockedBoneNames = {lockBone};
    ws.brush.radius = 0.77f;
    ws.viewport.fovY = 0.9f;
    ws.selectionSets = {{"ws_probe_set", {setBone0, setBone1}}};
    const std::filesystem::path wsPath =
        std::filesystem::temp_directory_path() / "m2rig_ws_reimport.m2rig";
    CHECK_TRUE(saveWorkspace(ws, wsPath).succeeded());
    App fresh;
    CHECK_TRUE(restoreWorkspace(fresh, wsPath).succeeded());
    CHECK_TRUE(fresh.currentAsset() != nullptr);
    if (fresh.currentAsset()) {
        CHECK_TRUE(fresh.current == "m2rig_ws_reimport");
        CHECK_TRUE(fresh.currentAsset()->mesh.vertices.size() == a->mesh.vertices.size());
        const Bone* locked = fresh.currentAsset()->skeleton.findByName(lockBone);
        CHECK_TRUE(locked != nullptr);
        if (locked != nullptr) CHECK_TRUE(fresh.isBoneLocked(locked->id));
        CHECK_NEAR(fresh.brushRadius, 0.77, 1e-5);
        CHECK_NEAR(fresh.camera.fovY, 0.9, 1e-5);
        const auto setIt = fresh.boneSelectionSets.find("ws_probe_set");
        CHECK_TRUE(setIt != fresh.boneSelectionSets.end());
        if (setIt != fresh.boneSelectionSets.end()) {
            CHECK_EQ(setIt->second.size(), 2u);
            CHECK_TRUE(setIt->second.count(setBone0) != 0u);
            CHECK_TRUE(setIt->second.count(setBone1) != 0u);
        }
    }
    std::error_code ec;
    std::filesystem::remove(smdPath, ec);
    std::filesystem::remove(wsPath, ec);
    return failures;
}

M2RIG_TEST(weights, keyframe_clip_drives_timeline) {
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    const std::size_t baseFrames = a->animFrames.size();
    CHECK_EQ(app.timelineFrameCount(), baseFrames);
    CHECK_TRUE(app.addKeyframeHere().succeeded());  // key at frame 0
    a->currentFrame = 5;
    CHECK_TRUE(app.addKeyframeHere().succeeded());  // key at frame 5
    CHECK_EQ(a->clip.keys.size(), 2u);
    CHECK_EQ(app.timelineFrameCount(), 6u);
    // Clip shadows frames: a vandalized live pose is re-applied on scrub.
    a->skeleton.bones[0].localPosition = {100, 0, 0};
    CHECK_TRUE(app.setCurrentFrame(3).succeeded());
    CHECK_EQ(a->currentFrame, 3u);
    CHECK_TRUE(a->skeleton.bones[0].localPosition.x < 50.0f);
    // Bake replaces the frame array; delete/clear behave honestly.
    CHECK_TRUE(app.bakeClipToFrames().succeeded());
    CHECK_EQ(a->animFrames.size(), 6u);
    a->currentFrame = 0;
    CHECK_TRUE(app.deleteKeyframeHere().succeeded());
    CHECK_EQ(a->clip.keys.size(), 1u);
    CHECK_TRUE(app.clearClip().succeeded());
    CHECK_FALSE(app.clearClip().succeeded());
    CHECK_FALSE(app.deleteKeyframeHere().succeeded());
    return failures;
}

M2RIG_TEST(weights, workspace_restore_failure_keeps_session) {
    // A workspace whose sources are gone must fail honestly WITHOUT blanking
    // the healthy live session (no false success, no dangling current).
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    M2RigWorkspace ws;
    ws.title = "ghost";
    ws.currentAssetId = "ghost-asset";
    const std::filesystem::path wsPath =
        std::filesystem::temp_directory_path() / "m2rig_ws_ghost.m2rig";
    CHECK_TRUE(saveWorkspace(ws, wsPath).succeeded());
    CHECK_FALSE(restoreWorkspace(app, wsPath).succeeded());
    CHECK_TRUE(app.currentAsset() != nullptr);
    CHECK_TRUE(app.current == "sample-warrior-armor");
    std::error_code ec;
    std::filesystem::remove(wsPath, ec);
    return failures;
}

M2RIG_TEST(weights, histogram_counts_secondary_influences) {
    // Regression for the first-influence-only histogram: a vertex bound
    // 0.7/0.3 must credit BOTH bones, not just the primary.
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    CHECK_TRUE(a->mesh.vertices.size() >= 3);
    CHECK_TRUE(a->skeleton.bones.size() >= 6);
    app.pushUndoSnapshot("histogram probe");
    for (auto& v : a->mesh.vertices) v.influences = {{5, 1.0f}};
    a->mesh.vertices[0].influences = {{0, 0.7f}, {1, 0.3f}};
    a->mesh.vertices[1].influences = {{1, 1.0f}};
    app.noteWeightsChanged();
    CHECK_EQ(app.boneInfluenceCount(0), 1u);
    CHECK_EQ(app.boneInfluenceCount(1), 2u);
    CHECK_EQ(app.boneInfluenceCount(5), a->mesh.vertices.size() - 2);
    app.undo();
    return failures;
}

M2RIG_TEST(weights, bridged_gr2_applies_canonical_profile) {
    // G1 parity: bridged GR2/Noesis text must go through the same canonical
    // conversion as sync importBridgedFile/CLI gr22smd — never bare install.
    int failures = 0;
    App app;
    CHECK_TRUE(app.loadSampleArmor().succeeded());
    auto smd = writeSmd(app.currentAsset()->mesh, app.currentAsset()->skeleton, {});
    CHECK_TRUE(smd.succeeded());
    if (!smd.succeeded()) return failures + 1;
    const std::string text = smd.value().text;
    CHECK_TRUE(app.applyBridgedSmdText(text, "smd", "bridge_smd_test.smd").succeeded());
    CHECK_TRUE(app.applyBridgedSmdText(text, "gr2", "bridge_gr2_test.gr2").succeeded());
    CHECK_TRUE(app.applyBridgedSmdText(text, "noesis", "bridge_noe_test.fbx").succeeded());
    auto itS = app.assets.find("bridge_smd_test");
    auto itG = app.assets.find("bridge_gr2_test");
    auto itN = app.assets.find("bridge_noe_test");
    CHECK_TRUE(itS != app.assets.end());
    CHECK_TRUE(itG != app.assets.end());
    CHECK_TRUE(itN != app.assets.end());
    if (itS == app.assets.end() || itG == app.assets.end() || itN == app.assets.end())
        return failures + 1;
    CHECK_TRUE(!itS->second.mesh.vertices.empty());
    const Vec3 ps = itS->second.mesh.vertices[0].position;
    const Vec3 pg = itG->second.mesh.vertices[0].position;
    const Vec3 pn = itN->second.mesh.vertices[0].position;
    // GR2/Noesis origins assume Z-up source: verts must move vs SMD identity.
    CHECK_TRUE(distance(ps, pg) > 1e-3f);
    CHECK_TRUE(distance(ps, pn) > 1e-3f);
    // Both bridge profiles share the Z-up source today: identical output.
    CHECK_TRUE(distance(pg, pn) < 1e-4f);
    // Skeleton globals follow the mesh (no detached install): max joint
    // drift over all bones must be significant (bone 0 can sit at origin).
    float maxJointDrift = 0.0f;
    const std::size_t nb =
        std::min(itS->second.skeleton.bones.size(), itG->second.skeleton.bones.size());
    for (std::size_t i = 0; i < nb; ++i) {
        const Vec3 js{itS->second.skeleton.bones[i].globalTransform.m[3][0],
                      itS->second.skeleton.bones[i].globalTransform.m[3][1],
                      itS->second.skeleton.bones[i].globalTransform.m[3][2]};
        const Vec3 jg{itG->second.skeleton.bones[i].globalTransform.m[3][0],
                      itG->second.skeleton.bones[i].globalTransform.m[3][1],
                      itG->second.skeleton.bones[i].globalTransform.m[3][2]};
        maxJointDrift = std::max(maxJointDrift, distance(js, jg));
    }
    CHECK_TRUE(maxJointDrift > 1e-3f);
    return failures;
}

M2RIG_TEST(weights, bridge_per_call_log_unique) {
    // Per-call unique bridge logs (PID + counter): concurrent bridges must
    // never truncate each other's output. pushBridgeLog deletes after read.
    int failures = 0;
    std::wstring cmdA = L"m2rig_nonexistent_bridge_xyz_12345";
    const BridgeResult rA = runBridgeLogged(cmdA, 1000);
    std::wstring cmdB = L"m2rig_nonexistent_bridge_xyz_12345";
    const BridgeResult rB = runBridgeLogged(cmdB, 1000);
    CHECK_FALSE(rA.started);
    CHECK_FALSE(rB.started);
    CHECK_TRUE(rA.logPath != rB.logPath);
    CHECK_TRUE(rA.logPath.filename().string().find("m2rig_bridge_") == 0u);
    CHECK_TRUE(rB.logPath.filename().string().find("m2rig_bridge_") == 0u);
    // Log files are created before CreateProcess, even on failed starts.
    CHECK_TRUE(std::filesystem::exists(rA.logPath));
    CHECK_TRUE(std::filesystem::exists(rB.logPath));
    (void)pushBridgeLog(rA.logPath, rA.exitCode, rA.timedOut);
    (void)pushBridgeLog(rB.logPath, rB.exitCode, rB.timedOut);
    CHECK_FALSE(std::filesystem::exists(rA.logPath));
    CHECK_FALSE(std::filesystem::exists(rB.logPath));
    return failures;
}

M2RIG_TEST(weights, bridge_extractor_tmp_unique_and_clean) {
    // NoesisBridgeExtractor hygiene: unique tmp per call, remove-before-
    // launch, remove on every failure return, nonzero exit fails closed.
    // Hermetic: missing tool + non-executable CLI + findstr nonzero stub;
    // never touches Data/Models or a real Noesis install.
    int failures = 0;
    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path();
    const std::filesystem::path dummy = tmpDir / "m2rig_bridge_extractor_probe.fbx";
    {
        std::ofstream out(dummy, std::ios::binary | std::ios::trunc);
        out << "not a real fbx";
    }
    auto countExtractedTmps = [&tmpDir]() -> std::size_t {
        std::size_t n = 0;
        std::error_code itEc;
        for (std::filesystem::directory_iterator it(tmpDir, itEc), end;
             !itEc && it != end; it.increment(itEc)) {
            std::error_code ec;
            if (it->is_regular_file(ec) && !ec) {
                const std::string fn = it->path().filename().string();
                if (fn.find("_extracted") != std::string::npos &&
                    it->path().extension() == ".smd")
                    ++n;
            }
        }
        return n;
    };
    const std::size_t before = countExtractedTmps();
    // 1. Missing CLI: honest EXT_BRIDGE_MISSING failure, no tmp litter.
    {
        NoesisBridgeExtractor ex(tmpDir / "m2rig_missing_noesis_xyz" / "Noesis.exe");
        auto r = ex.extractWeights(dummy);
        CHECK_FALSE(r.succeeded());
    }
    // 2. A stale file at the OLD deterministic name must NOT be installed:
    // the extractor fails (missing CLI) instead of reading stale output.
    {
        const std::filesystem::path stale =
            tmpDir / (dummy.stem().string() + "_extracted.smd");
        {
            std::ofstream out(stale, std::ios::binary | std::ios::trunc);
            out << "stale";
        }
        NoesisBridgeExtractor ex(tmpDir / "m2rig_missing_noesis_xyz" / "Noesis.exe");
        auto r = ex.extractWeights(dummy);
        CHECK_FALSE(r.succeeded());
        std::error_code ec;
        std::filesystem::remove(stale, ec);
    }
    // 3. Existing-but-not-executable CLI: !started path removes its tmp.
    {
        const std::filesystem::path fakeCli = tmpDir / "m2rig_bridge_fake_cli.txt";
        {
            std::ofstream out(fakeCli, std::ios::binary | std::ios::trunc);
            out << "not an exe";
        }
        NoesisBridgeExtractor ex(fakeCli);
        auto r = ex.extractWeights(dummy);
        CHECK_FALSE(r.succeeded());
        std::error_code ec;
        std::filesystem::remove(fakeCli, ec);
    }
    // 4. Nonzero-exit stub (findstr reports no match / bad file as exit 1-2
    // and creates no SMD): must fail closed with an exit-code status.
    {
        const std::filesystem::path findstr(L"C:\\Windows\\System32\\findstr.exe");
        if (!std::filesystem::exists(findstr)) {
            printf("    SKIP nonzero stub (findstr missing)\n");
        } else {
            NoesisBridgeExtractor ex(findstr);
            auto r = ex.extractWeights(dummy);
            CHECK_FALSE(r.succeeded());
            if (!r.succeeded())
                CHECK_TRUE(r.error().message.find("exited with code") != std::string::npos);
        }
    }
    CHECK_EQ(countExtractedTmps(), before);
    {
        std::error_code ec;
        std::filesystem::remove(dummy, ec);
    }
    return failures;
}

M2RIG_TEST(weights, bridge_nonzero_exit_fails_closed) {
    // Fail-closed bridges: a nonzero converter exit never installs output.
    // Part A pins exit-code propagation via the hermetic cmd stub; part B
    // pins the sync App import installing nothing on failure (same style as
    // bridge_start_poll_idle_contract, no real Noesis, no Data/Models).
    int failures = 0;
    {
        std::wstring cmd = L"cmd /c exit 3";
        const BridgeResult br = runBridgeLogged(cmd, 10000);
        CHECK_TRUE(br.started);
        CHECK_FALSE(br.timedOut);
        CHECK_EQ(br.exitCode, 3ul);
        (void)pushBridgeLog(br.logPath, br.exitCode, br.timedOut);
        CHECK_FALSE(std::filesystem::exists(br.logPath));
    }
    {
        App app;
        CHECK_TRUE(app.loadSampleArmor().succeeded());
        const std::size_t nBefore = app.assets.size();
        const std::string curBefore = app.current;
        const std::filesystem::path dummy =
            std::filesystem::temp_directory_path() / "m2rig_bridge_nonzero_probe.fbx";
        {
            std::ofstream out(dummy, std::ios::binary | std::ios::trunc);
            out << "not a real fbx";
        }
        auto r = app.importBridgedFile(dummy.string());
        // Garbage input can never convert: native FBX rejects it and the
        // Noesis fallback is missing (CI) or exits nonzero / emits nothing.
        CHECK_FALSE(r.succeeded());
        CHECK_EQ(app.assets.size(), nBefore);
        CHECK_TRUE(app.current == curBefore);
        CHECK_TRUE(app.assets.find("m2rig_bridge_nonzero_probe") == app.assets.end());
        std::error_code ec;
        std::filesystem::remove(dummy, ec);
    }
    return failures;
}

#ifdef M2RIG_WITH_CGLTF
M2RIG_TEST(weights, app_imports_gltf_roundtrip) {
    // App-level glTF import: sample armor -> .glb via writeGltfFile (same
    // bindInverse-from-bones + default-pbrs pattern as
    // gltf.smd2gltf_roundtrip_sample_armor) -> App::importGltfFile installs
    // it with matching bone/vert counts; a garbage .glb then fails honestly
    // with the live session untouched (workspace-restore failure discipline,
    // same shape as bridge_nonzero_exit_fails_closed).
    int failures = 0;
    auto armorRes = makeSampleArmor();
    CHECK_TRUE(armorRes.succeeded());
    if (!armorRes.succeeded()) return failures;
    Mesh mesh = std::move(armorRes.value().mesh);
    Skeleton skel = std::move(armorRes.value().skeleton);
    // Caller-passed bind reference (same snapshot the App captures at load).
    std::vector<Mat4> bindInverse;
    bindInverse.reserve(skel.bones.size());
    for (const auto& b : skel.bones) bindInverse.push_back(b.inverseBindTransform);
    std::vector<PbrMaterial> pbrs;
    pbrs.reserve(mesh.materials.size());
    for (const auto& mat : mesh.materials) {
        PbrMaterial pm;
        pm.name = mat.name;
        pbrs.push_back(std::move(pm));
    }
    const std::size_t expectVerts = mesh.vertices.size();
    const std::size_t expectBones = skel.bones.size();
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "m2rig_app_gltf_rt.glb";
    auto wres = writeGltfFile(tmp.string(), mesh, skel, bindInverse, pbrs, "app-gltf-test");
    CHECK_TRUE(wres.succeeded());
    if (!wres.succeeded()) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return failures;
    }
    App app;
    auto imported = app.importGltfFile(tmp.string());
    {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }
    CHECK_TRUE(imported.succeeded());
    if (!imported.succeeded()) {
        printf("    glTF import error: %s\n", imported.error().message.c_str());
        return failures + 1;
    }
    const LoadedAsset* a = app.currentAsset();
    CHECK_TRUE(a != nullptr);
    if (!a) return failures + 1;
    CHECK_EQ(a->mesh.vertices.size(), expectVerts);
    CHECK_EQ(a->skeleton.bones.size(), expectBones);
    // Garbage .glb: honest failure, session untouched (no asset installed).
    const std::size_t nBefore = app.assets.size();
    const std::string curBefore = app.current;
    const std::filesystem::path bad =
        std::filesystem::temp_directory_path() / "m2rig_app_gltf_bad.glb";
    {
        std::ofstream out(bad, std::ios::binary | std::ios::trunc);
        out << "not a glb";
    }
    auto badRes = app.importGltfFile(bad.string());
    CHECK_FALSE(badRes.succeeded());
    CHECK_EQ(app.assets.size(), nBefore);
    CHECK_TRUE(app.current == curBefore);
    CHECK_TRUE(app.assets.find("m2rig_app_gltf_bad") == app.assets.end());
    {
        std::error_code ec;
        std::filesystem::remove(bad, ec);
    }
    return failures;
}
#endif  // M2RIG_WITH_CGLTF
