// Logger implementation: ring buffer + append-only log file.
#include "m2rig/logging.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace m2rig {

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "INFO";
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

static std::string nowStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

void Logger::init(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    filePath_ = logFilePath;
    fileOpen_ = !filePath_.empty();
    if (fileOpen_) {
        std::FILE* f = nullptr;
#if defined(_WIN32)
        fopen_s(&f, filePath_.c_str(), "ab");
#else
        f = std::fopen(filePath_.c_str(), "a");
#endif
        if (f) std::fclose(f);
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    fileOpen_ = false;
}

void Logger::log(LogLevel level, std::string message, std::string category, std::string asset,
                 std::string operation) {
    if (level < minLevel_) return;
    LogEntry entry{level, nowStamp(), std::move(category), std::move(asset), std::move(operation),
                   std::move(message)};
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.push_back(entry);
    if (ring_.size() > 2000) ring_.erase(ring_.begin(), ring_.begin() + 500);
    if (fileOpen_ && !filePath_.empty()) {
        std::FILE* f = nullptr;
#if defined(_WIN32)
        fopen_s(&f, filePath_.c_str(), "ab");
#else
        f = std::fopen(filePath_.c_str(), "a");
#endif
        if (f) {
            std::fprintf(f, "[%s] %-7s", entry.timestamp.c_str(), logLevelName(level));
            if (!entry.category.empty()) std::fprintf(f, " [%s]", entry.category.c_str());
            if (!entry.asset.empty()) std::fprintf(f, " asset=%s", entry.asset.c_str());
            if (!entry.operation.empty()) std::fprintf(f, " op=%s", entry.operation.c_str());
            std::fprintf(f, " %s\n", entry.message.c_str());
            std::fclose(f);
        }
    }
}

std::vector<LogEntry> Logger::recent(std::size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.size() <= maxCount) return ring_;
    return std::vector<LogEntry>(ring_.end() - static_cast<std::ptrdiff_t>(maxCount), ring_.end());
}

void Logger::clearRecent() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
}

}  // namespace m2rig
