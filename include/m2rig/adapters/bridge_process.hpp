#pragma once
// Shared Win32 bridge-process runner with stdout/stderr capture.
// Used by the GR2/Noesis bridges so external tool output lands in the
// in-app Console instead of vanishing behind SW_HIDE.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace m2rig {

struct BridgeResult {
    bool started = false;
    bool timedOut = false;
    unsigned long exitCode = 1;
    std::filesystem::path logPath;
};

// Runs cmd (mutable buffer required by CreateProcessW), waits up to
// timeoutMs, captures combined stdout/stderr into a temp log file.
// Never throws; check started/timedOut/exitCode.
BridgeResult runBridgeLogged(std::wstring& cmd, std::uint32_t timeoutMs);

// Reads the log (last 200 lines, 500 chars each), pushes lines into the
// Logger (warning level when the run failed), deletes the file, and returns
// the last line for status messages.
std::string pushBridgeLog(const std::filesystem::path& logPath, unsigned long exitCode,
                          bool timedOut);

// Directory of the current executable ({} when it cannot be determined).
// This is the anchor for exe-relative resources; current_path() depends on
// the launch context (debugger OutDir vs double-click vs terminal) and must
// never be the only probe for shipped resources.
std::filesystem::path executableDir();

// Search roots for tool/resource probing, in order: the executable dir,
// then the working dir plus up to three ancestors (dev runs from build/).
std::vector<std::filesystem::path> toolSearchRoots();

}  // namespace m2rig
