// m2rig_cli: headless batch tool (core only, no D3D/ImGui).
// Commands: validate | validate-msm | info | smd2smd | smd2msm.
// Exit codes: 0 ok, 1 usage, 2 IO/parse, 3 validation/export-blocked,
// 4 write failure.
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/app.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/smd.hpp"

namespace {

using namespace m2rig;

int usage() {
    std::printf("m2rig_cli %s - Metin2 Rigging Studio headless tool\n", appVersion());
    std::printf(
        "usage:\n"
        "  m2rig_cli validate <in.smd> [--profile <id>]\n"
        "  m2rig_cli validate-msm <in.msm>\n"
        "  m2rig_cli info <in.smd>\n"
        "  m2rig_cli smd2smd <in.smd> <out.smd>\n"
        "  m2rig_cli smd2msm <in.smd> <out.msm>\n");
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

int cmdSmd2Msm(const std::vector<std::string>& args) {
    if (args.size() < 4) return usage();
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
    if (cmd == "info") return cmdInfo(args);
    if (cmd == "smd2smd") return cmdSmd2Smd(args);
    if (cmd == "smd2msm") return cmdSmd2Msm(args);
    return usage();
}
