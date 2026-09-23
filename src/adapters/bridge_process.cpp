// Shared Win32 bridge-process runner with output capture.
#include "m2rig/adapters/bridge_process.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <fstream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "m2rig/logging.hpp"

namespace m2rig {

BridgeResult runBridgeLogged(std::wstring& cmd, std::uint32_t timeoutMs) {
    BridgeResult r;
    // Unique log per call (PID + atomic counter, same recipe as the bridge
    // tmp files): concurrent bridges must never truncate each other's output
    // (CREATE_ALWAYS on a PID-only name did exactly that). pushBridgeLog
    // deletes the file after reading, so per-call names are also litter-free.
    static std::atomic<unsigned> logCounter{0};
    r.logPath = std::filesystem::temp_directory_path() /
                ("m2rig_bridge_" + std::to_string(::GetCurrentProcessId()) + "_" +
                 std::to_string(logCounter.fetch_add(1)) + ".log");
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hLog = ::CreateFileW(r.logPath.wstring().c_str(), FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = (hLog == INVALID_HANDLE_VALUE) ? nullptr : hLog;
    si.hStdError = si.hStdOutput;
    si.hStdInput = nullptr;
    const BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (hLog != INVALID_HANDLE_VALUE) ::CloseHandle(hLog);
    if (!ok) return r;
    const DWORD w = ::WaitForSingleObject(pi.hProcess, timeoutMs);
    if (w == WAIT_OBJECT_0) {
        DWORD code = 1;
        (void)::GetExitCodeProcess(pi.hProcess, &code);
        r.exitCode = code;
    } else {
        r.timedOut = true;
        (void)::TerminateProcess(pi.hProcess, 1);
        r.exitCode = 1;
    }
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    r.started = true;
    return r;
}

std::string pushBridgeLog(const std::filesystem::path& logPath, unsigned long exitCode,
                          bool timedOut) {    std::deque<std::string> tail;
    {
        std::ifstream f(logPath, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > 500) line.resize(500);
            if (tail.size() >= 200) tail.pop_front();
            tail.push_back(line);
        }
    }
    std::error_code ec;
    std::filesystem::remove(logPath, ec);
    const bool failed = timedOut || exitCode != 0;
    if (tail.empty()) {
        const std::string msg = "bridge finished with no output";
        if (failed)
            Logger::instance().warning(msg, "bridge");
        else
            Logger::instance().info(msg, "bridge");
        return msg;
    }
    for (const auto& s : tail) {
        if (failed)
            Logger::instance().warning(s, "bridge");
        else
            Logger::instance().info(s, "bridge");
    }
    return tail.back();
}

std::filesystem::path executableDir() {
    wchar_t buf[32768]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, 32767);
    if (n == 0 || n >= 32767) return {};
    const std::filesystem::path p(buf);
    if (!p.has_parent_path()) return {};
    return p.parent_path();
}

std::vector<std::filesystem::path> toolSearchRoots() {
    std::vector<std::filesystem::path> roots;
    if (std::filesystem::path exe = executableDir(); !exe.empty()) roots.push_back(exe);
    std::filesystem::path dir = std::filesystem::current_path();
    for (int level = 0; level < 4; ++level) {
        if (std::find(roots.begin(), roots.end(), dir) == roots.end()) roots.push_back(dir);
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    return roots;
}

}  // namespace m2rig
