// Safety limits + path sanitizer.
#include "m2rig/diagnostics.hpp"

#include <cctype>

namespace m2rig {

const SafetyLimits& defaultLimits() {
    static const SafetyLimits kLimits;
    return kLimits;
}

bool checkCount(const char* what, std::size_t count, std::size_t limit, std::string& outError) {
    if (count > limit) {
        outError = std::string(what) + ": count " + std::to_string(count) + " exceeds limit " +
                   std::to_string(limit) + " (file treated as untrusted, refusing allocation)";
        return false;
    }
    return true;
}

bool checkBytes(const char* what, std::uint64_t bytes, std::uint64_t limit, std::string& outError) {
    if (bytes > limit) {
        outError = std::string(what) + ": size " + std::to_string(bytes) + " exceeds limit " +
                   std::to_string(limit);
        return false;
    }
    return true;
}

namespace {

bool hasTraversal(const std::string& p) {
    // Detect ".." as a full path segment.
    std::size_t i = 0;
    while (i <= p.size()) {
        std::size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        if (j - i == 2 && p[i] == '.' && p[i + 1] == '.') return true;
        i = j + 1;
    }
    return false;
}

bool sanitize(const std::string& raw, bool allowSubdirs, std::string& out, std::string& err) {
    if (raw.empty()) {
        err = "empty path";
        return false;
    }
    if (raw.size() > 1024) {
        err = "path too long";
        return false;
    }
    std::string p = raw;
    for (char& c : p)
        if (c == '\\') c = '/';
    // Strip drive-letter style "d:" but keep the remainder; reject otherwise absolute paths.
    if (p.size() >= 2 && p[1] == ':' && std::isalpha(static_cast<unsigned char>(p[0])) != 0) {
        p = p.substr(2);
    }
    while (!p.empty() && (p.front() == '/' || p.front() == ' ')) p.erase(p.begin());
    while (!p.empty() && (p.back() == '/' || p.back() == ' ')) p.pop_back();
    if (p.empty()) {
        err = "path is empty after normalization";
        return false;
    }
    if (hasTraversal(p)) {
        err = "path traversal ('..') is not allowed: " + raw;
        return false;
    }
    for (char c : p) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '/' || c == '.' || c == '_' || c == '-' || c == ' ' || c == '(' ||
                        c == ')' || c == '+' || c == ',';
        if (!ok) {
            err = std::string("illegal character in path: '") + c + "' in " + raw;
            return false;
        }
    }
    if (!allowSubdirs && p.find('/') != std::string::npos) {
        err = "subdirectories not allowed here: " + raw;
        return false;
    }
    out = p;
    return true;
}

}  // namespace

bool sanitizeImportPath(const std::string& raw, std::string& outNormalized, std::string& outError) {
    return sanitize(raw, true, outNormalized, outError);
}

bool sanitizeTexturePath(const std::string& raw, std::string& outNormalized, std::string& outError) {
    return sanitize(raw, true, outNormalized, outError);
}

bool isValidBoneName(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        if (c < 32 || c == '"' || c == '\n' || c == '\r') return false;
    }
    return true;
}

bool isValidMaterialName(const std::string& name) {
    if (name.empty() || name.size() > 260) return false;
    for (char c : name) {
        if (c < 32 || c == '\n' || c == '\r') return false;
    }
    return true;
}

}  // namespace m2rig
