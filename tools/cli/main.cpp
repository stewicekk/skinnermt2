// m2rig_cli: headless batch tool (core only, no D3D/ImGui).
// Commands: validate | validate-msm | validate-mse | info | smd2smd |
// smd2msm | autorig | lod | fbx2smd (OPENFBX builds) | gltf2smd (CGLTF builds) |
// smd2gltf (CGLTF builds) |
// gr22smd | orient | learn-from-asset | learn-from-gr2-dir | self-learn-transfer |
// self-learn-autorig | analyze-gr2-dir. (Keep in sync with usage() below.)
// Exit codes: 0 ok, 1 usage, 2 IO/parse, 3 validation/export-blocked,
// 4 write failure.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/app.hpp"
#include "m2rig/coordsys.hpp"
#include "m2rig/mse.hpp"
#ifdef M2RIG_WITH_OPENFBX
#include "m2rig/fbx/fbx_reader.hpp"
#endif
#ifdef M2RIG_WITH_CGLTF
#include "m2rig/gltf/gltf_reader.hpp"
#include "m2rig/gltf/gltf_writer.hpp"
#endif
#include "m2rig/lod.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/self_learning.hpp"
#include "m2rig/gr2_deep_parser.hpp"

namespace {

using namespace m2rig;

int usage() {
    std::printf("m2rig_cli %s - Metin2 Rigging Studio headless tool\n", appVersion());
    std::printf(
        "usage:\n"
        "  m2rig_cli validate <in.smd> [--profile <id>]\n"
        "  m2rig_cli validate-msm <in.msm>\n"
        "  m2rig_cli validate-mse <in.mse|.mde>\n"
        "  m2rig_cli info <in.smd>\n"
        "  m2rig_cli smd2smd <in.smd> <out.smd>\n"
        "  m2rig_cli smd2msm <in.smd> <out.msm>\n"
        "  m2rig_cli autorig <in.smd> <out.smd>\n"
        "  m2rig_cli lod <in.smd> <out.smd> [--ratio <0..1>]\n"
#ifdef M2RIG_WITH_OPENFBX
        "  m2rig_cli fbx2smd <in.fbx> <out.smd>\n"
#endif
#ifdef M2RIG_WITH_CGLTF
        "  m2rig_cli gltf2smd <in.gltf|in.glb> <out.smd>\n"
        "  m2rig_cli smd2gltf <in.smd> <out.glb>\n"
#endif
        "  m2rig_cli gr22smd <in.gr2> <out.smd>\n"
        "  m2rig_cli orient <in.smd>\n"
        "  m2rig_cli learn-from-asset <in.smd> <out.m2learn>\n"
        "  m2rig_cli learn-from-gr2-dir <dir> <out.m2learn>\n"
        "  m2rig_cli self-learn-transfer <source.smd> <target.smd> <out.smd> [--learning-db <db.m2learn>]\n"
        "  m2rig_cli self-learn-autorig <in.smd> <out.smd> [--learning-db <db.m2learn>]\n"
        "  m2rig_cli analyze-gr2-dir <dir> <report.md>\n"
        );
    return 1;
}

struct LoadedModel {
    std::string id;
    Mesh mesh;
    Skeleton skeleton;
    std::vector<SmdFrame> frames;
};

bool loadModel(const std::string& path, const std::string& id, LoadedModel& out,
               std::string& err) {
    auto text = readTextFile(path, id);
    if (!text) {
        err = text.error().message;
        return false;
    }
    auto parsed = parseSmd(text.value(), id);
    if (!parsed) {
        err = parsed.error().message;
        return false;
    }
    auto conv = smdToAsset(parsed.value(), id);
    if (!conv) {
        err = conv.error().message;
        return false;
    }
    out.id = id;
    out.mesh = std::move(conv.value().mesh);
    out.skeleton = std::move(conv.value().skeleton);
    out.frames = std::move(conv.value().frames);
    return true;
}

void runValidation(const LoadedModel& m, const std::string& profileId, ValidationReport& report) {
    validateMeshStructure(m.mesh, m.id, report);
    validateSkeleton(m.skeleton, m.id, report);
    validateMeshWeights(m.mesh, m.skeleton.bones.size(), m.id, report);
    if (const SkeletonProfile* profile = findProfile(profileId); profile) {
        validateAgainstProfile(m.skeleton, *profile, m.id, report);
        validateSocketDeformUse(m.mesh, m.skeleton, *profile, m.id, report);
    } else {
        report.add("APP_NO_PROFILE", ValidationCategory::Skeleton, Severity::Warning,
                   "Unknown skeleton profile '" + profileId + "'.", m.id, "", false);
    }
}

void printReport(const ValidationReport& report) {
    std::printf("%s\n", report.summaryLine().c_str());
    for (const auto& item : report.items()) {
        std::printf("[%s/%s] %s: %s\n", validationCategoryName(item.category),
                    severityName(item.severity), item.id.c_str(), item.message.c_str());
    }
}

int cmdValidate(const std::vector<std::string>& args) {
    if (args.size() < 3) return usage();
    std::string profile = "pc_warrior";
    for (std::size_t i = 3; i + 1 < args.size(); i += 2) {
        if (args[i] == "--profile") profile = args[i + 1];
    }
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    ValidationReport report;
    runValidation(m, profile, report);
    printReport(report);
    return report.exportBlocked() ? 3 : 0;
}

int cmdValidateMsm(const std::vector<std::string>& args) {
    if (args.size() < 3) return usage();
    std::ifstream file(args[2], std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        std::printf("error: cannot open %s\n", args[2].c_str());
        return 2;
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    MsmDocument doc;
    if (!parseMsm(buf.str(), doc)) {
        std::printf("error: MSM parse failed\n");
        return 2;
    }
    std::size_t groups = 0, bones = 0;
    std::function<void(const MsmNode&)> walk = [&](const MsmNode& n) {
        ++groups;
        if (n.type == MsmSectionType::Model) bones += n.children.size();
        for (const auto& c : n.children) walk(c);
    };
    walk(doc.root);
    std::printf("groups: %zu\nmodel_children: %zu\n", groups, bones);
    std::printf("roundtrip: %s\n", msmStringify(doc).empty() ? "empty" : "ok");
    // Same semantic gate as the GUI MSM Inspector.
    ValidationReport report;
    validateMsmDoc(doc, args[2], report);
    printReport(report);
    if (report.exportBlocked()) return 3;
    return 0;
}

static std::string cappedStr(const char* p, std::size_t n) {
    // Fixed-size binary fields may lack a terminator; never read past n.
    std::size_t len = 0;
    while (len < n && p[len] != '\0') ++len;
    return std::string(p, len);
}

static std::string lowerExtOf(const std::string& path) {
    const auto pos = path.find_last_of('.');
    std::string ext = (pos == std::string::npos) ? "" : path.substr(pos);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// validate-mse: parse + report a Metin2 effect file (.mse text or .mde
// binary). Exit 0 when usable, 2 on IO/parse failure, 3 when the document
// carries nothing usable (zero attachments/emitters).
int cmdValidateMse(const std::vector<std::string>& args) {
    if (args.size() < 3) return usage();
    const std::string ext = lowerExtOf(args[2]);
    std::size_t attachments = 0, emitters = 0;
    std::vector<std::string> notes;
    if (ext == ".mde") {
        auto doc = readMdeFile(args[2]);
        if (!doc) {
            std::printf("error: %s\n", doc.error().message.c_str());
            return 2;
        }
        attachments = doc.value().attachments.size();
        emitters = doc.value().emitters.size();
        for (const auto& a : doc.value().attachments) {
            notes.push_back("attachment bone '" + cappedStr(a.boneName, 64) + "'");
        }
        for (const auto& e : doc.value().emitters) {
            notes.push_back("emitter '" + cappedStr(e.name, 64) + "' tex '" +
                            cappedStr(e.texturePath, 256) + "'");
        }
        if (!doc.value().stringTable.empty())
            notes.push_back("string table " + std::to_string(doc.value().stringTable.size()) +
                            " bytes");
    } else {
        auto text = readTextFile(args[2], args[2]);
        if (!text) {
            std::printf("error: %s\n", text.error().message.c_str());
            return 2;
        }
        auto doc = parseMse(text.value(), args[2]);
        if (!doc) {
            std::printf("error: %s\n", doc.error().message.c_str());
            return 2;
        }
        attachments = doc.value().attachments.size();
        for (const auto& a : doc.value().attachments) {
            emitters += a.emitters.size();
            notes.push_back("attachment bone '" + a.boneName + "' (" +
                            std::to_string(a.emitters.size()) + " emitters)");
            for (const auto& e : a.emitters)
                notes.push_back("  emitter '" + e.name + "' type '" + e.type + "' tex '" +
                                e.texturePath + "'");
            if (a.emitters.empty())
                notes.push_back("  WARNING: attachment '" + a.boneName + "' has no emitters");
        }
        if (stringifyMse(doc.value()).empty()) notes.push_back("WARNING: stringify empty");
    }
    std::printf("attachments: %zu\nemitters: %zu\n", attachments, emitters);
    for (const auto& n : notes) std::printf("%s\n", n.c_str());
    if (attachments == 0 || emitters == 0) {
        std::printf("MSE has no usable attachments/emitters.\n");
        return 3;
    }
    return 0;
}

int cmdInfo(const std::vector<std::string>& args) {    if (args.size() < 3) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    std::printf("bones: %zu\n", m.skeleton.bones.size());
    std::printf("verts: %zu\n", m.mesh.vertices.size());
    std::printf("tris: %zu\n", m.mesh.triangleCount());
    std::printf("materials: %zu\n", m.mesh.materials.size());
    std::printf("frames: %zu\n", m.frames.size());
    std::printf("bounds_min: %.4f %.4f %.4f\n", m.mesh.bounds.min.x, m.mesh.bounds.min.y,
                m.mesh.bounds.min.z);
    std::printf("bounds_max: %.4f %.4f %.4f\n", m.mesh.bounds.max.x, m.mesh.bounds.max.y,
                m.mesh.bounds.max.z);
    return 0;
}

int cmdSmd2Smd(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    RepairStats stats = repairMeshWeights(m.mesh, m.skeleton.bones.size());
    ValidationReport report;
    runValidation(m, "pc_warrior", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(m.mesh, m.skeleton, m.frames);
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, m.id); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes, dropped mass %.6f, repaired %zu verts)\n", args[3].c_str(),
                written.value().text.size(), written.value().stats.droppedMass,
                stats.verticesChanged);
    return 0;
}

int cmdSmd2Msm(const std::vector<std::string>& args) {    if (args.size() < 4) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    RepairStats stats = repairMeshWeights(m.mesh, m.skeleton.bones.size());
    (void)stats;
    ValidationReport report;
    runValidation(m, "pc_warrior", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    const std::string text = buildMsmExport(m.mesh, m.skeleton, m.id);
    if (auto w = writeTextFile(args[3], text, m.id); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes)\n", args[3].c_str(), text.size());
    return 0;
}

int cmdAutorig(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    if (m.mesh.vertices.empty() || m.skeleton.bones.empty()) {
        std::printf("error: auto-rig needs a non-empty mesh and skeleton\n");
        return 2;
    }
    // Same deterministic bind as the GUI "Auto-rig from skeleton" button
    // (App::autoRigFromSkeleton): every vertex bound to the nearest bones,
    // standard repair, mass always reported.
    RepairStats repair;
    const AutoRigStats stats =
        autoRigMesh(m.mesh, m.skeleton, 0.02f, kMetin2MaxInfluences, &repair);
    ValidationReport report;
    runValidation(m, "pc_warrior", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(m.mesh, m.skeleton, m.frames);
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, m.id); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes, %s)\n", args[3].c_str(), written.value().text.size(),
                stats.toDisplayString().c_str());
    return 0;
}

int cmdLod(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    float ratio = 0.5f;
    for (std::size_t i = 4; i + 1 < args.size(); i += 2) {
        if (args[i] == "--ratio") ratio = std::strtof(args[i + 1].c_str(), nullptr);
    }
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    LodOptions opt;
    opt.targetRatio = ratio;
    auto lod = decimateMesh(m.mesh, opt);
    if (!lod) {
        std::printf("error: %s\n", lod.error().message.c_str());
        return 2;
    }
    RepairStats stats = repairMeshWeights(m.mesh, m.skeleton.bones.size());
    (void)stats;
    ValidationReport report;
    runValidation(m, "pc_warrior", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(m.mesh, m.skeleton, m.frames);
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, m.id); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes, %s)\n", args[3].c_str(), written.value().text.size(),
                lod.value().toDisplayString().c_str());
    return 0;
}

#ifdef M2RIG_WITH_OPENFBX
int cmdFbx2Smd(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    auto conv = readFbxFile(args[2], "cli");
    if (!conv) {
        std::printf("error: %s\n", conv.error().message.c_str());
        return 2;
    }
    Mesh mesh = std::move(conv.value().mesh);
    Skeleton skeleton = std::move(conv.value().skeleton);
    RepairStats stats = repairMeshWeights(mesh, skeleton.bones.size());
    ValidationReport report;
    validateMeshStructure(mesh, "cli", report);
    validateSkeleton(skeleton, "cli", report);
    validateMeshWeights(mesh, skeleton.bones.size(), "cli", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(mesh, skeleton, {});
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, "cli"); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes, %zu bones, repaired %zu verts)\n", args[3].c_str(),
                written.value().text.size(), skeleton.bones.size(), stats.verticesChanged);
    if (!conv.value().conversionNote.empty())
        std::printf("%s\n", conv.value().conversionNote.c_str());
    return 0;
}
#endif

#ifdef M2RIG_WITH_CGLTF
int cmdGltf2Smd(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    auto conv = readGltfFile(args[2], "cli");
    if (!conv) {
        std::printf("error: %s\n", conv.error().message.c_str());
        return 2;
    }
    Mesh mesh = std::move(conv.value().mesh);
    Skeleton skeleton = std::move(conv.value().skeleton);
    RepairStats stats = repairMeshWeights(mesh, skeleton.bones.size());
    ValidationReport report;
    validateMeshStructure(mesh, "cli", report);
    validateSkeleton(skeleton, "cli", report);
    validateMeshWeights(mesh, skeleton.bones.size(), "cli", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(mesh, skeleton, {});
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, "cli"); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes, %zu bones, repaired %zu verts, import dropped mass %.6f)\n",
                args[3].c_str(), written.value().text.size(), skeleton.bones.size(),
                stats.verticesChanged, conv.value().removedMass);
    if (!conv.value().conversionNote.empty())
        std::printf("%s\n", conv.value().conversionNote.c_str());
    return 0;
}

// smd2gltf: the headless twin of a GUI "Export GLB" path (same canonical
// data, same repair + export gate as cmdGltf2Smd mirrored): SMD -> canonical
// asset -> repair -> export gate -> single-BIN-chunk .glb. Same exit codes.
// Animation samplers are NOT emitted (deferred: the clip lives in the App
// session; bakeClipFrames() SmdFrames output is the follow-up emit source).
int cmdSmd2Gltf(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    RepairStats stats = repairMeshWeights(m.mesh, m.skeleton.bones.size());
    ValidationReport report;
    validateMeshStructure(m.mesh, "cli", report);
    validateSkeleton(m.skeleton, "cli", report);
    validateMeshWeights(m.mesh, m.skeleton.bones.size(), "cli", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    // Caller-passed bind reference (same snapshot the App captures at load:
    // inverseBindTransform per bone, joint order).
    std::vector<Mat4> bindInverse;
    bindInverse.reserve(m.skeleton.bones.size());
    for (const auto& b : m.skeleton.bones) bindInverse.push_back(b.inverseBindTransform);
    // SMD carries no PBR factors: neutral defaults with the client texture
    // path passed through (the writer keeps PNG/JPEG basename-only URIs and
    // leaves DDS factors-only, so round-trips stay valid).
    std::vector<PbrMaterial> pbrs;
    pbrs.reserve(m.mesh.materials.size());
    for (const auto& mat : m.mesh.materials) {
        PbrMaterial pm;
        pm.name = mat.name;
        pm.albedoTexture = mat.texturePath;
        pbrs.push_back(std::move(pm));
    }
    auto res = writeGltfFile(args[3], m.mesh, m.skeleton, bindInverse, pbrs, "cli");
    if (!res) {
        std::printf("error: %s\n", res.error().message.c_str());
        return (res.error().category == "IO") ? 4 : 2;
    }
    std::printf("wrote %s (%zu verts, %zu tris, %zu bones, repaired %zu verts)\n",
                args[3].c_str(), m.mesh.vertices.size(), m.mesh.triangleCount(),
                m.skeleton.bones.size(), stats.verticesChanged);
    if (!res.value().empty()) std::printf("%s\n", res.value().c_str());
    return 0;
}
#endif

// gr22smd: the headless twin of the GUI "Import GR2 via grnreader98" path
// (App::importBridgedFile): grnreader98 -> SMD text -> canonical asset ->
// GR2 profile conversion -> repair -> export gate -> SMD file. Same core,
// same gate, same exit codes — CLI/GUI parity is structural, not promised.
int cmdGr22Smd(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    auto smd = convertGr2ToSmdViaGrnReader(args[2]);
    if (!smd) {
        std::printf("error: %s\n", smd.error().message.c_str());
        return 2;
    }
    auto parsed = parseSmd(smd.value(), args[2]);
    if (!parsed) {
        std::printf("error: %s\n", parsed.error().message.c_str());
        return 2;
    }
    auto conv = smdToAsset(parsed.value(), "cli");
    if (!conv) {
        std::printf("error: %s\n", conv.error().message.c_str());
        return 2;
    }
    Mesh mesh = std::move(conv.value().mesh);
    Skeleton skeleton = std::move(conv.value().skeleton);
    std::vector<SmdFrame> frames = std::move(conv.value().frames);
    const auto& profile = gr2ConversionProfile();
    const std::optional<CoordSys> detected = detectGr2CoordSys(args[2]);
    if (auto r = applyConversionProfile(profile, detected, mesh); !r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    if (auto r = applyConversionProfile(profile, detected, skeleton); !r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    if (auto r = applyConversionProfile(profile, detected, frames); !r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    RepairStats stats = repairMeshWeights(mesh, skeleton.bones.size());
    ValidationReport report;
    validateMeshStructure(mesh, "cli", report);
    validateSkeleton(skeleton, "cli", report);
    validateMeshWeights(mesh, skeleton.bones.size(), "cli", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(mesh, skeleton, frames);
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, "cli"); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    std::printf("wrote %s (%zu bytes, %zu bones, %zu frames, repaired %zu verts)\n",
                args[3].c_str(), written.value().text.size(), skeleton.bones.size(),
                frames.size(), stats.verticesChanged);
    return 0;
}

// orient: automated transform diagnostics over an SMD asset — world-space
// joint layout vs mesh bounds (rotated 90/180°, detached, scaled). Exit 0
// when sane, 3 when blocking findings exist (mirrors validate semantics).
int cmdOrient(const std::vector<std::string>& args) {
    if (args.size() < 3) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    const OrientationReport rep = diagnoseOrientation(m.mesh, m.skeleton);
    std::printf("%s", rep.toDisplayString().c_str());
    return rep.sane() ? 0 : 3;
}

// learn-from-asset: loads an SMD, learns vertex affinities, saves learning database
int cmdLearnFromAsset(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    if (m.mesh.vertices.empty() || m.skeleton.bones.empty()) {
        std::printf("error: model has no mesh or skeleton\n");
        return 2;
    }
    
    Gr2CharacterProfile profile;
    profile.profileId = "pc_warrior";
    
    SelfLearningDatabase db;
    db.recordVertexAffinities(profile, m.mesh, m.skeleton);
    
    if (auto r = db.save(args[3]); !r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 4;
    }
    
    std::printf("Learning database saved to %s\n", args[3].c_str());
    db.printStats();
    return 0;
}

// learn-from-gr2-dir: analyzes a directory of GR2 files and builds learning database
int cmdLearnFromGr2Dir(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    
    SelfLearningDatabase db;
    Gr2BatchAnalyzer analyzer(db);
    
    auto r = analyzer.analyzeDirectory(args[2]);
    if (!r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    r = analyzer.buildInitialDatabase();
    if (!r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    r = db.save(args[3]);
    if (!r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 4;
    }
    
    std::printf("Analyzed GR2 directory, database saved to %s\n", args[3].c_str());
    db.printStats();
    return 0;
}

// self-learn-transfer: performs self-learning weight transfer between two SMD files
int cmdSelfLearnTransfer(const std::vector<std::string>& args) {
    if (args.size() < 5) return usage();
    
    std::string learningDbPath = "";
    for (std::size_t i = 5; i + 1 < args.size(); i += 2) {
        if (args[i] == "--learning-db") learningDbPath = args[i + 1];
    }
    
    // Load source model
    LoadedModel src;
    std::string err;
    if (!loadModel(args[2], "src", src, err)) {
        std::printf("error loading source: %s\n", err.c_str());
        return 2;
    }
    
    // Load target model
    LoadedModel dst;
    if (!loadModel(args[3], "dst", dst, err)) {
        std::printf("error loading target: %s\n", err.c_str());
        return 2;
    }
    
    // Load or create learning database
    SelfLearningDatabase db;
    if (!learningDbPath.empty()) {
        auto r = db.load(learningDbPath);
        if (!r) {
            std::printf("warning: could not load learning DB: %s\n", r.error().message.c_str());
        }
    }
    
    Gr2CharacterProfile srcProfile;
    srcProfile.profileId = "pc_warrior";
    Gr2CharacterProfile dstProfile;
    dstProfile.profileId = "pc_warrior";
    
    SelfLearningTransfer transfer(db);
    auto stats = transfer.transfer(db, src.mesh, src.skeleton, srcProfile,
                                   dst.mesh, dst.skeleton, dstProfile, 3, nullptr);
    if (!stats) {
        std::printf("error: %s\n", stats.error().message.c_str());
        return 2;
    }
    
    // Save learning database
    if (!learningDbPath.empty()) {
        auto r = db.save(learningDbPath);
        if (!r) std::printf("warning: could not save learning DB: %s\n", r.error().message.c_str());
    }
    
    // Save target model
    RepairStats repair;
    repairMeshWeights(dst.mesh, dst.skeleton.bones.size());
    ValidationReport report;
    runValidation(dst, "pc_warrior", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(dst.mesh, dst.skeleton, dst.frames);
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[4], written.value().text, dst.id); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    
    std::printf("wrote %s (%s)\n", args[4].c_str(), stats.value().toDisplayString().c_str());
    return 0;
}

// self-learn-autorig: performs self-learning auto-rig on an SMD file
int cmdSelfLearnAutorig(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    
    std::string learningDbPath = "";
    for (std::size_t i = 4; i + 1 < args.size(); i += 2) {
        if (args[i] == "--learning-db") learningDbPath = args[i + 1];
    }
    
    // Load model
    LoadedModel m;
    std::string err;
    if (!loadModel(args[2], "cli", m, err)) {
        std::printf("error: %s\n", err.c_str());
        return 2;
    }
    if (m.mesh.vertices.empty() || m.skeleton.bones.empty()) {
        std::printf("error: model has no mesh or skeleton\n");
        return 2;
    }
    
    // Load or create learning database
    SelfLearningDatabase db;
    if (!learningDbPath.empty()) {
        auto r = db.load(learningDbPath);
        if (!r) {
            std::printf("warning: could not load learning DB: %s\n", r.error().message.c_str());
        }
    }
    
    Gr2CharacterProfile profile;
    profile.profileId = "pc_warrior";
    
    SelfLearningTransfer transfer(db);
    auto stats = transfer.autoRig(db, m.mesh, m.skeleton, profile, 0.02f, nullptr);
    if (!stats) {
        std::printf("error: %s\n", stats.error().message.c_str());
        return 2;
    }
    
    // Save learning database
    if (!learningDbPath.empty()) {
        auto r = db.save(learningDbPath);
        if (!r) std::printf("warning: could not save learning DB: %s\n", r.error().message.c_str());
    }
    
    // Save model
    RepairStats repair;
    repairMeshWeights(m.mesh, m.skeleton.bones.size());
    ValidationReport report;
    runValidation(m, "pc_warrior", report);
    if (report.exportBlocked()) {
        printReport(report);
        return 3;
    }
    auto written = writeSmd(m.mesh, m.skeleton, m.frames);
    if (!written) {
        std::printf("error: %s\n", written.error().message.c_str());
        return 2;
    }
    if (auto w = writeTextFile(args[3], written.value().text, m.id); !w) {
        std::printf("error: %s\n", w.error().message.c_str());
        return 4;
    }
    
    std::printf("wrote %s (%s)\n", args[3].c_str(), stats.value().toDisplayString().c_str());
    return 0;
}

// analyze-gr2-dir: analyzes a directory of GR2 files and exports a report
int cmdAnalyzeGr2Dir(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
    
    SelfLearningDatabase db;
    Gr2BatchAnalyzer analyzer(db);
    
    auto r = analyzer.analyzeDirectory(args[2]);
    if (!r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    r = analyzer.buildInitialDatabase();
    if (!r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    r = analyzer.exportReport(args[3]);
    if (!r) {
        std::printf("error: %s\n", r.error().message.c_str());
        return 2;
    }
    
    std::printf("GR2 directory analyzed, report saved to %s\n", args[3].c_str());
    db.printStats();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    if (args.size() < 2) return usage();
    const std::string cmd = args[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") return usage();
    if (cmd == "--version") {
        std::printf("m2rig_cli %s\n", ::m2rig::appVersion());
        return 0;
    }
    if (cmd == "validate") return cmdValidate(args);
    if (cmd == "validate-msm") return cmdValidateMsm(args);
    if (cmd == "validate-mse") return cmdValidateMse(args);
    if (cmd == "info") return cmdInfo(args);
    if (cmd == "smd2smd") return cmdSmd2Smd(args);
    if (cmd == "smd2msm") return cmdSmd2Msm(args);
    if (cmd == "autorig") return cmdAutorig(args);
    if (cmd == "lod") return cmdLod(args);
#ifdef M2RIG_WITH_OPENFBX
    if (cmd == "fbx2smd") return cmdFbx2Smd(args);
#endif
#ifdef M2RIG_WITH_CGLTF
    if (cmd == "gltf2smd") return cmdGltf2Smd(args);
    if (cmd == "smd2gltf") return cmdSmd2Gltf(args);
#endif
    if (cmd == "gr22smd") return cmdGr22Smd(args);
    if (cmd == "orient") return cmdOrient(args);
    if (cmd == "learn-from-asset") return cmdLearnFromAsset(args);
    if (cmd == "learn-from-gr2-dir") return cmdLearnFromGr2Dir(args);
    if (cmd == "self-learn-transfer") return cmdSelfLearnTransfer(args);
    if (cmd == "self-learn-autorig") return cmdSelfLearnAutorig(args);
    if (cmd == "analyze-gr2-dir") return cmdAnalyzeGr2Dir(args);
    return usage();
}
