#pragma once
// Centralized validation engine (spec section 30).
// Every subsystem reports here; UI and CLI render the same report.
#include <cstdint>
#include <string>
#include <vector>

namespace m2rig {

enum class ValidationCategory : std::uint8_t {
    Format = 0,
    Skeleton,
    Mesh,
    Weights,
    Material,
    Texture,
    Animation,
    Msm,
    Gr2,
    Path,
    Export
};

enum class Severity : std::uint8_t { Info = 0, Warning, Error, Fatal };

const char* validationCategoryName(ValidationCategory category);
const char* severityName(Severity severity);

struct ValidationItem {
    std::string id;  // stable rule id, e.g. "WEIGHTS_UNNORMALIZED"
    ValidationCategory category = ValidationCategory::Format;
    Severity severity = Severity::Info;
    std::string message;
    std::string asset;
    std::string location;  // e.g. "vertex 1234", "bone 'Bip01'"
    bool repairAvailable = false;
};

class ValidationReport {
public:
    void add(ValidationItem item);
    void add(std::string id, ValidationCategory category, Severity severity, std::string message,
             std::string asset = {}, std::string location = {}, bool repairAvailable = false);

    const std::vector<ValidationItem>& items() const { return items_; }
    std::size_t count(Severity severity) const;
    bool hasErrors() const;  // Error or Fatal present
    bool exportBlocked() const { return hasErrors(); }
    void clear();
    void merge(const ValidationReport& other);

    std::string summaryLine() const;  // "2 errors, 5 warnings, 41 infos"

private:
    std::vector<ValidationItem> items_;
};

}  // namespace m2rig
