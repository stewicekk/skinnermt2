#include "m2rig/adapters/gr2_adapter.hpp"
#include "m2rig/adapters/bridge_process.hpp"
#include "m2rig/logging.hpp"
#include "m2rig/smd.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <windows.h>

namespace m2rig {

Gr2ExportStatus exportGr2ViaBridge(
    const Mesh& mesh,
    const Skeleton& skeleton,
    const std::filesystem::path& outputPath,
    const Gr2BridgeConfig& config,
    std::string& errorMessage) {
    
    // Unique temp name unless the caller explicitly keeps it for inspection:
    // concurrent exports must never share (and mistake) one _temp.smd.
    static std::atomic<unsigned> exportCounter{0};
    std::filesystem::path tempSmd;
    if (config.keepTempSmd) {
        tempSmd = outputPath.parent_path() / (outputPath.stem().string() + "_temp.smd");
    } else {
        tempSmd = std::filesystem::temp_directory_path() /
                  (outputPath.stem().string() + "_temp_" +
                   std::to_string(::GetCurrentProcessId()) + "_" +
                   std::to_string(exportCounter.fetch_add(1)) + ".smd");
    }

    // Probe the tool BEFORE writing the staging SMD: the old order wrote the
    // tmp file first and leaked it on this early return.
    if (!std::filesystem::exists(config.noesisCliPath)) {
        errorMessage = "Noesis CLI not found at: " + config.noesisCliPath.string() +
                       ". Configure Gr2BridgeConfig::noesisCliPath or add to PATH.";
        return Gr2ExportStatus::NotSupportedDirectly;
    }

    auto smdResult = writeSmd(mesh, skeleton);
    if (!smdResult) {
        errorMessage = "SMD write failed: " + smdResult.error().message;
        return Gr2ExportStatus::Failed;
    }

    {
        std::ofstream smdFile(tempSmd, std::ios::out);
        if (!smdFile.is_open()) {
            errorMessage = "Cannot write temporary SMD file: " + tempSmd.string();
            return Gr2ExportStatus::Failed;
        }
        smdFile << smdResult.value().text;
        smdFile.close();
    }

    std::wstring cmd = L"\"" + config.noesisCliPath.wstring() + L"\" ?cmode";

    std::wstring smdArg = L"\"" + tempSmd.wstring() + L"\"";
    std::wstring gr2Arg = L"\"" + outputPath.wstring() + L"\"";
    
    std::wstring fullArgs = smdArg + L" " + gr2Arg;
    if (!config.noesisArgs.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, config.noesisArgs.c_str(), -1, nullptr, 0);
        if (wlen > 1) {
            std::wstring wargs(static_cast<std::size_t>(wlen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, config.noesisArgs.c_str(), -1, wargs.data(), wlen);
            fullArgs += L" " + wargs;
        }
    }
    cmd += L" " + fullArgs;

    M2RIG_LOG_INFO(std::string("Invoking GR2 bridge for: ") + outputPath.string());

    const BridgeResult br = runBridgeLogged(cmd, config.timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);

    if (!br.started) {
        errorMessage = "Failed to create external bridge process.";
        if (!config.keepTempSmd && std::filesystem::exists(tempSmd))
            std::filesystem::remove(tempSmd);
        return Gr2ExportStatus::Failed;
    }

    if (br.timedOut) {
        errorMessage = "External bridge process timed out after " +
                       std::to_string(config.timeoutMs) + "ms. Last output: " + lastLine;
        if (!config.keepTempSmd && std::filesystem::exists(tempSmd))
            std::filesystem::remove(tempSmd);
        return Gr2ExportStatus::Failed;
    }

    if (br.exitCode != 0) {
        errorMessage = "External bridge process exited with code " + std::to_string(br.exitCode) +
                       ". Last output: " + lastLine;
        if (!config.keepTempSmd && std::filesystem::exists(tempSmd))
            std::filesystem::remove(tempSmd);
        return Gr2ExportStatus::NotSupportedDirectly;
    }

    if (std::filesystem::exists(outputPath)) {
        M2RIG_LOG_INFO("GR2 export via bridge succeeded: " + outputPath.string());
        if (!config.keepTempSmd && std::filesystem::exists(tempSmd))
            std::filesystem::remove(tempSmd);
        return Gr2ExportStatus::Success;
    }

    errorMessage = "Bridge process succeeded but output file not created: " + outputPath.string();
    if (!config.keepTempSmd && std::filesystem::exists(tempSmd))
        std::filesystem::remove(tempSmd);
    return Gr2ExportStatus::Failed;
}

Gr2ExportStatus extractWeightsFromGr2ViaBridge(
    const std::filesystem::path& gr2Path,
    const Gr2BridgeConfig& config,
    ExtractedGr2Weights& outWeights) {
    
    (void)config;

    if (!std::filesystem::exists(gr2Path)) {
        outWeights.error = "GR2 file not found: " + gr2Path.string();
        return Gr2ExportStatus::Failed;
    }

    outWeights.success = false;
    outWeights.error = "Native GR2 weight extraction not supported. "
                       "Use the Noesis bridge or export SMD instead.";
    return Gr2ExportStatus::NotSupportedDirectly;
}

Gr2BridgeConfig defaultGr2BridgeConfig() {
    Gr2BridgeConfig config;
    // Probe order: executable dir first (installed layout), then the working
    // dir plus ancestors (dev runs from build/). Noesis ships both bitnesses.
    for (const auto& dir : toolSearchRoots()) {
        const std::filesystem::path candidates[] = {
            dir / "noesis" / "Noesis.exe",
            dir / "noesis" / "Noesis64.exe",
            dir / "external" / "noesis" / "Noesis.exe",
        };
        for (const auto& c : candidates) {
            if (std::filesystem::exists(c)) {
                config.noesisCliPath = c;
                break;
            }
        }
        if (!config.noesisCliPath.empty()) break;
    }
    if (config.noesisCliPath.empty())
        config.noesisCliPath =
            std::filesystem::current_path() / "noesis" / "Noesis.exe";
    config.timeoutMs = 30000;
    config.keepTempSmd = false;
    return config;
}

// Noesis-based GR2 to FBX conversion with rotation fix for Metin2 coordinate system
// Uses Noesis with -rotate 90 0 0 to fix upside-down models (Z-up to Y-up)
Result<std::string> convertGr2ToFbxViaNoesis(const std::filesystem::path& gr2Path,
                                             const std::filesystem::path& outputFbxPath,
                                             const Gr2BridgeConfig& config) {
    if (!std::filesystem::exists(config.noesisCliPath)) {
        return Result<std::string>::fail("Noesis CLI not found: " + config.noesisCliPath.string(),
                                         "IMPORT", gr2Path.string(), "noesis.gr2tofbx");
    }
    if (!std::filesystem::exists(gr2Path)) {
        return Result<std::string>::fail("GR2 file not found: " + gr2Path.string(),
                                         "IMPORT", gr2Path.string(), "noesis.gr2tofbx");
    }

    std::error_code ec;
    std::filesystem::create_directories(outputFbxPath.parent_path(), ec);

    // Noesis command: ?cmode input.gr2 output.fbx -rotate 90 0 0
    // -rotate 90 0 0 fixes the Z-up to Y-up coordinate system for Metin2
    std::wstring cmd = L"\"" + config.noesisCliPath.wstring() + L"\" ?cmode \"" +
                       gr2Path.wstring() + L"\" \"" + outputFbxPath.wstring() + L"\" -rotate 90 0 0";
    if (!config.noesisArgs.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, config.noesisArgs.c_str(), -1, nullptr, 0);
        if (wlen > 1) {
            std::wstring wargs(static_cast<std::size_t>(wlen - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, config.noesisArgs.c_str(), -1, wargs.data(), wlen);
            cmd += L" " + wargs;
        }
    }

    M2RIG_LOG_INFO("Converting GR2 to FBX via Noesis: " + gr2Path.string() + " -> " + outputFbxPath.string());

    const BridgeResult br = runBridgeLogged(cmd, config.timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);

    if (!br.started) {
        return Result<std::string>::fail("Failed to start Noesis process.", "IMPORT",
                                         gr2Path.string(), "noesis.gr2tofbx");
    }
    if (br.timedOut) {
        return Result<std::string>::fail("Noesis timed out. Last output: " + lastLine, "IMPORT",
                                         gr2Path.string(), "noesis.gr2tofbx");
    }
    if (br.exitCode != 0) {
        return Result<std::string>::fail(
            "Noesis exited with code " + std::to_string(br.exitCode) +
                ". Last output: " + lastLine,
            "IMPORT", gr2Path.string(), "noesis.gr2tofbx");
    }
    if (!std::filesystem::exists(outputFbxPath)) {
        return Result<std::string>::fail(
            "Noesis produced no FBX output. Last output: " + lastLine,
            "IMPORT", gr2Path.string(), "noesis.gr2tofbx");
    }

    M2RIG_LOG_INFO("GR2 to FBX conversion via Noesis succeeded: " + outputFbxPath.string());
    return Result<std::string>::ok(outputFbxPath.string());
}

bool isValidGr2Container(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    unsigned char magic[4] = {0, 0, 0, 0};
    file.read(reinterpret_cast<char*>(magic), 4);
    if (file.gcount() < 4) return false;
    // V1: "gr2\0". V2 (real Metin2 Granny 2.x variant, verified on 31
    // Data/Models/*.gr2): 29 DE 6C C0 (analyzer also documents 29 DE C0).
    if (magic[0] == 'g' && magic[1] == 'r' && magic[2] == '2') return true;
    if (magic[0] == 0x29 && magic[1] == 0xDE) return true;
    return false;
}

std::filesystem::path findGrnReader() {
    for (const auto& dir : toolSearchRoots()) {
        const std::filesystem::path c =
            dir / "Data" / "resources" / "Convert gr2 to mesh" /
            "grnreader98.v1.4.0.3.debug (1)" / "grnreader98.exe";
        if (std::filesystem::exists(c)) return c;
        // Also check without version suffix
        const std::filesystem::path c2 =
            dir / "Data" / "resources" / "Convert gr2 to mesh" /
            "grnreader98.exe";
        if (std::filesystem::exists(c2)) return c2;
    }
    // Check the explicit path from the repo root
    const std::filesystem::path explicitPath =
        std::filesystem::current_path() / "Data" / "resources" / "Convert gr2 to mesh" /
        "grnreader98.v1.4.0.3.debug (1)" / "grnreader98.exe";
    if (std::filesystem::exists(explicitPath)) return explicitPath;
    return {};
}

Result<std::string> convertGr2ToSmdViaGrnReader(const std::filesystem::path& gr2Path,
                                               std::uint32_t timeoutMs) {
    const std::filesystem::path exe = findGrnReader();
    if (exe.empty()) {
        return Result<std::string>::fail(
            "grnreader98.exe not found under Data/resources/Convert gr2 to mesh/.",
            "IMPORT", gr2Path.string(), "grnreader.convert");
    }
    if (!std::filesystem::exists(gr2Path)) {
        return Result<std::string>::fail("GR2 file does not exist: " + gr2Path.string(),
                                         "IMPORT", gr2Path.string(), "grnreader.convert");
    }
    if (!isValidGr2Container(gr2Path)) {
        return Result<std::string>::fail("File is not a recognized GR2 container: " +
                                             gr2Path.string(),
                                         "FORMAT", gr2Path.string(), "grnreader.convert");
    }
    // grnreader98 writes "<input>.smd" next to its input, so stage a copy in
    // temp to keep the source tree clean (non-destructive by design).
    std::error_code ec;
    const std::filesystem::path stageDir =
        std::filesystem::temp_directory_path() / "m2rig_grnreader";
    std::filesystem::create_directories(stageDir, ec);
    // The tool writes <input>.smd beside its input.  A unique staged name is
    // required: imports can run concurrently and a stale output must never be
    // mistaken for a successful conversion.
    static std::atomic<unsigned> counter{0};
    const std::filesystem::path staged =
        stageDir / (gr2Path.stem().string() + "_" +
                    std::to_string(::GetCurrentProcessId()) + "_" +
                    std::to_string(counter.fetch_add(1)) + gr2Path.extension().string());
    const std::filesystem::path outSmd = staged.string() + ".smd";
    std::filesystem::remove(outSmd, ec);
    std::filesystem::copy_file(gr2Path, staged,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return Result<std::string>::fail("Cannot stage GR2 copy: " + ec.message(), "IMPORT",
                                         gr2Path.string(), "grnreader.convert");
    }
    // Batch flags (observed tool behavior, Wave 22): -a exports all submeshes
    // + weights + skeleton without prompts; -t negates some skeleton values
    // (orientation tweak). Exact tool-side semantics are unverified against
    // grnreader98 docs — the orientation gate (diagnoseOrientation +
    // m2rig_cli orient) is the safety net, not flag lore.
    std::wstring cmd = L"\"" + exe.wstring() + L"\" \"" + staged.wstring() + L"\" -a -t";
    const BridgeResult br = runBridgeLogged(cmd, timeoutMs);
    const std::string lastLine = pushBridgeLog(br.logPath, br.exitCode, br.timedOut);
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove(outSmd, ignored);
        std::filesystem::remove(staged, ignored);
    };
    if (!br.started) {
        cleanup();
        return Result<std::string>::fail("Failed to start grnreader98.", "IMPORT",
                                         gr2Path.string(), "grnreader.convert");
    }
    if (br.timedOut) {
        cleanup();
        return Result<std::string>::fail(
            "grnreader98 timed out. Last output: " + lastLine, "IMPORT", gr2Path.string(),
            "grnreader.convert");
    }
    if (br.exitCode != 0) {
        cleanup();
        return Result<std::string>::fail(
            "grnreader98 exited with code " + std::to_string(br.exitCode) +
                ". Last output: " + lastLine,
            "IMPORT", gr2Path.string(), "grnreader.convert");
    }
    if (!std::filesystem::exists(outSmd)) {
        cleanup();
        return Result<std::string>::fail(
            "grnreader98 produced no SMD output (unsupported GR2 variant?). Last output: " +
                lastLine,
            "IMPORT", gr2Path.string(), "grnreader.convert");
    }
    std::ostringstream buf;
    {
        std::ifstream in(outSmd, std::ios::in | std::ios::binary);
        if (!in.is_open()) {
            cleanup();
            return Result<std::string>::fail("Cannot read grnreader98 SMD output.", "IMPORT",
                                             gr2Path.string(), "grnreader.convert");
        }
        buf << in.rdbuf();
    }  // close before remove (Windows file locking)
    cleanup();
    if (buf.str().empty()) {
        return Result<std::string>::fail("grnreader98 produced an empty SMD output.", "FORMAT",
                                         gr2Path.string(), "grnreader.convert");
    }
    M2RIG_LOG_INFO(std::string("grnreader98 conversion done: ") + gr2Path.string());
    return Result<std::string>::ok(buf.str());
}

}  // namespace m2rig