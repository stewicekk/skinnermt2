#pragma once
// Thread-safe leveled logger: in-app ring buffer + log file.
// File: logs/metin2_rigging_studio.log (spec section 46).
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace m2rig {

enum class LogLevel : std::uint8_t { Trace = 0, Debug, Info, Warning, Error, Fatal };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string timestamp;  // local time HH:MM:SS
    std::string category;
    std::string asset;
    std::string operation;
    std::string message;
};

const char* logLevelName(LogLevel level);

class Logger {
public:
    static Logger& instance();

    void init(const std::string& logFilePath);
    void shutdown();

    void setMinLevel(LogLevel level) { minLevel_ = level; }
    LogLevel minLevel() const { return minLevel_; }

    void log(LogLevel level, std::string message, std::string category = {},
             std::string asset = {}, std::string operation = {});

    void trace(std::string m, std::string c = {}) { log(LogLevel::Trace, std::move(m), std::move(c)); }
    void debug(std::string m, std::string c = {}) { log(LogLevel::Debug, std::move(m), std::move(c)); }
    void info(std::string m, std::string c = {}) { log(LogLevel::Info, std::move(m), std::move(c)); }
    void warning(std::string m, std::string c = {}) {
        log(LogLevel::Warning, std::move(m), std::move(c));
    }
    void error(std::string m, std::string c = {}) { log(LogLevel::Error, std::move(m), std::move(c)); }
    void fatal(std::string m, std::string c = {}) { log(LogLevel::Fatal, std::move(m), std::move(c)); }

    // Copy of recent entries for the in-app console panel.
    std::vector<LogEntry> recent(std::size_t maxCount = 200) const;
    void clearRecent();

private:
    Logger() = default;
    mutable std::mutex mutex_;
    std::vector<LogEntry> ring_;
    std::string filePath_;
    bool fileOpen_ = false;
    LogLevel minLevel_ = LogLevel::Trace;
};

#define M2RIG_LOG_TRACE(msg) ::m2rig::Logger::instance().trace((msg), __func__)
#define M2RIG_LOG_DEBUG(msg) ::m2rig::Logger::instance().debug((msg), __func__)
#define M2RIG_LOG_INFO(msg) ::m2rig::Logger::instance().info((msg), __func__)
#define M2RIG_LOG_WARN(msg) ::m2rig::Logger::instance().warning((msg), __func__)
#define M2RIG_LOG_ERROR(msg) ::m2rig::Logger::instance().error((msg), __func__)

}  // namespace m2rig
