// Universal weight extractor: SMD native, FBX/GR2 via Noesis bridge.
#include "m2rig/extractors/universal_weight_extractor.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/logging.hpp"

namespace m2rig {

const char* modelFormatName(ModelFormat fmt) {
    switch (fmt) {
        case ModelFormat::SMD: return "SMD";
        case ModelFormat::FBX: return "FBX";
        case ModelFormat::GR2: return "GR2";
        case ModelFormat::Unknown: return "Unknown";
    }
    return "Unknown";
}

ModelFormat detectModelFormat(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".smd") return ModelFormat::SMD;
    if (ext == ".fbx") return ModelFormat::FBX;
    if (ext == ".gr2") return ModelFormat::GR2;
    return ModelFormat::Unknown;
}

Result<ExtractedMeshData> SmdWeightExtractor::extractWeights(
    const std::filesystem::path& filePath) {
    auto text = readTextFile(filePath.string(), filePath.string());
    if (!text) return Result<ExtractedMeshData>::fail(text.error());
    auto parsed = parseSmd(text.value(), filePath.string());
    if (!parsed) return Result<ExtractedMeshData>::fail(parsed.error());
    auto conv = smdToAsset(parsed.value(), filePath.stem().string());
    if (!conv) return Result<ExtractedMeshData>::fail(conv.error());
    ExtractedMeshData out;
    out.mesh = std::move(conv.value().mesh);
    out.skeleton = std::move(conv.value().skeleton);
    out.frames = std::move(conv.value().frames);
    out.sourceFormat = "SMD";
    out.hasAnimationData = out.frames.size() > 1;
    return Result<ExtractedMeshData>::ok(std::move(out));
}

NoesisBridgeExtractor::NoesisBridgeExtractor(std::filesystem::path noesisCli)
    : noesisCli_(std::move(noesisCli)) {}

bool NoesisBridgeExtractor::supportsFormat(ModelFormat fmt) const {
    return fmt == ModelFormat::GR2 || fmt == ModelFormat::FBX;
}

Result<ExtractedMeshData> NoesisBridgeExtractor::extractWeights(
    const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(filePath))
        return Result<ExtractedMeshData>::fail("File does not exist: " + filePath.string(),
                                               "IMPORT", filePath.string(), "bridge.extract");
    if (!std::filesystem::exists(noesisCli_)) {
        M2RIG_LOG_WARN(std::string("Noesis CLI missing, bridge unavailable: ") +
                       noesisCli_.string());
        return Result<ExtractedMeshData>::fail(
            "EXT_BRIDGE_MISSING: Noesis adapter is not connected. Use direct SMD import.",
            "IMPORT", filePath.string(), "bridge.extract");
    }
    const std::filesystem::path tempSmd =
        std::filesystem::temp_directory_path() / (filePath.stem().string() + "_extracted.smd");
    std::wstring cmd = L"\"" + noesisCli_.wstring() + L"\" ?cmode \"" + filePath.wstring() +
                       L"\" \"" + tempSmd.wstring() + L"\"";
    const BridgeResult br = runBridgeLogged(cmd, 60000);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);
    if (!br.started) {
        return Result<ExtractedMeshData>::fail("Failed to start external converter.", "IMPORT",
                                               filePath.string(), "bridge.extract");
    }
    if (br.timedOut) {
        return Result<ExtractedMeshData>::fail("Converter timed out. Last output: " + lastLine,
                                               "IMPORT", filePath.string(), "bridge.extract");
    }
    if (!std::filesystem::exists(tempSmd)) {
        return Result<ExtractedMeshData>::fail(
            "Weight extraction failed - converter produced no SMD output. Last output: " +
                lastLine,
            "IMPORT", filePath.string(), "bridge.extract");
    }
    SmdWeightExtractor smd;
    auto r = smd.extractWeights(tempSmd);
    std::error_code ec;
    std::filesystem::remove(tempSmd, ec);
    if (!r) return r;
    r.value().sourceFormat =
        filePath.extension() == ".gr2" ? "Granny3D GR2" : "Autodesk FBX";
    M2RIG_LOG_INFO(std::string("Bridge extraction done: ") + filePath.string());
    return r;
}

UniversalWeightExtractor::UniversalWeightExtractor() {
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path noesis;
    for (int level = 0; level < 4 && noesis.empty(); ++level) {
        const std::filesystem::path candidates[] = {
            dir / "noesis" / "Noesis.exe",
            dir / "noesis" / "Noesis64.exe",
            dir / "external" / "noesis" / "Noesis.exe",
        };
        for (const auto& c : candidates) {
            if (std::filesystem::exists(c)) {
                noesis = c;
                break;
            }
        }
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (noesis.empty()) noesis = std::filesystem::current_path() / "noesis" / "Noesis.exe";
    registerExtractor(ModelFormat::SMD, std::make_unique<SmdWeightExtractor>());
    registerExtractor(ModelFormat::GR2,
                      std::make_unique<NoesisBridgeExtractor>(noesis));
    registerExtractor(ModelFormat::FBX,
                      std::make_unique<NoesisBridgeExtractor>(noesis));
}

void UniversalWeightExtractor::registerExtractor(ModelFormat fmt,
                                                 std::unique_ptr<IWeightExtractor> extractor) {
    extractors_.emplace_back(fmt, std::move(extractor));
}

ModelFormat UniversalWeightExtractor::detectFormat(const std::filesystem::path& filePath) {
    return detectModelFormat(filePath);
}

Result<ExtractedMeshData> UniversalWeightExtractor::extractFromFile(
    const std::filesystem::path& filePath) {
    const ModelFormat fmt = detectModelFormat(filePath);
    if (fmt == ModelFormat::Unknown) {
        return Result<ExtractedMeshData>::fail("Unknown or unsupported file format: " +
                                               filePath.string());
    }
    for (const auto& [extFmt, extractor] : extractors_) {
        if (extFmt == fmt && extractor->supportsFormat(fmt))
            return extractor->extractWeights(filePath);
    }
    return Result<ExtractedMeshData>::fail("No active extraction adapter for this format.");
}

}  // namespace m2rig
