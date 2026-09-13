#pragma once
// Safety limits + untrusted-file sanitizer (spec section 87).
// Every importer consults these before allocating from file-declared counts.
#include <cstddef>
#include <cstdint>
#include <string>

namespace m2rig {

struct SafetyLimits {
    std::uint64_t maxFileBytes = 512ULL * 1024ULL * 1024ULL;  // 512 MiB
    std::size_t maxVertices = 4'000'000;
    std::size_t maxIndices = 24'000'000;
    std::size_t maxTriangles = 8'000'000;
    std::size_t maxBones = 1024;
    std::size_t maxInfluencesPerVertex = 8;  // internal; export clamps to target limit
    std::size_t maxFrames = 65536;
    std::size_t maxMaterials = 1024;
    std::size_t maxTextLine = 16 * 1024;
    std::size_t maxTextLines = 64'000'000;
};

const SafetyLimits& defaultLimits();

// Returns false when count would exceed the matching limit.
bool checkCount(const char* what, std::size_t count, std::size_t limit, std::string& outError);
bool checkBytes(const char* what, std::uint64_t bytes, std::uint64_t limit, std::string& outError);

// Path safety: rejects absolute escapes, ".." traversal, illegal characters,
// and normalizes separators to '/'. Never throws.
bool sanitizeImportPath(const std::string& raw, std::string& outNormalized, std::string& outError);
bool sanitizeTexturePath(const std::string& raw, std::string& outNormalized, std::string& outError);

bool isValidBoneName(const std::string& name);
bool isValidMaterialName(const std::string& name);

}  // namespace m2rig
