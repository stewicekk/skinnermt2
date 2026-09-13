// Shared Win32 bridge-process runner with output capture.
#include "m2rig/adapters/bridge_process.hpp"

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
    r.logPath = std::filesystem::temp_directory_path() /
                ("m2rig_bridge_" + std::to_string(::GetCurrentProcessId()) + ".log");
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
                          bool timedOut) {
    std::deque<std::string> tail;
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

}  // namespace m2rig
