// Centralized validation engine.
#include "m2rig/validation.hpp"

#include <sstream>

namespace m2rig {

const char* validationCategoryName(ValidationCategory category) {
    switch (category) {
        case ValidationCategory::Format: return "FORMAT";
        case ValidationCategory::Skeleton: return "SKELETON";
        case ValidationCategory::Mesh: return "MESH";
        case ValidationCategory::Weights: return "WEIGHTS";
        case ValidationCategory::Material: return "MATERIAL";
        case ValidationCategory::Texture: return "TEXTURE";
        case ValidationCategory::Animation: return "ANIMATION";
        case ValidationCategory::Msm: return "MSM";
        case ValidationCategory::Gr2: return "GR2";
        case ValidationCategory::Path: return "PATH";
        case ValidationCategory::Export: return "EXPORT";
    }
    return "FORMAT";
}

const char* severityName(Severity severity) {
    switch (severity) {
        case Severity::Info: return "INFO";
        case Severity::Warning: return "WARNING";
        case Severity::Error: return "ERROR";
        case Severity::Fatal: return "FATAL";
    }
    return "INFO";
}

void ValidationReport::add(ValidationItem item) { items_.push_back(std::move(item)); }

void ValidationReport::add(std::string id, ValidationCategory category, Severity severity,
                           std::string message, std::string asset, std::string location,
                           bool repairAvailable) {
    items_.push_back(ValidationItem{std::move(id), category, severity, std::move(message),
                                    std::move(asset), std::move(location), repairAvailable});
}

std::size_t ValidationReport::count(Severity severity) const {
    std::size_t n = 0;
    for (const auto& item : items_)
        if (item.severity == severity) ++n;
    return n;
}

bool ValidationReport::hasErrors() const {
    for (const auto& item : items_)
        if (item.severity == Severity::Error || item.severity == Severity::Fatal) return true;
    return false;
}

void ValidationReport::clear() { items_.clear(); }

void ValidationReport::merge(const ValidationReport& other) {
    items_.insert(items_.end(), other.items_.begin(), other.items_.end());
}

std::string ValidationReport::summaryLine() const {
    std::ostringstream out;
    out << count(Severity::Error) + count(Severity::Fatal) << " errors, "
        << count(Severity::Warning) << " warnings, " << count(Severity::Info) << " infos";
    return out.str();
}

}  // namespace m2rig
