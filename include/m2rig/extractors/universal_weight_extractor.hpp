#pragma once
// Universal weight extractor: one honest entry point for SMD/FBX/GR2.
// SMD is parsed natively (see smd.hpp). FBX/GR2 go through the external
// Noesis bridge into a temp SMD which is then parsed natively. Nothing is
// faked: when the bridge is missing, extraction returns an explicit error.
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "m2rig/mesh.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"

namespace m2rig {

enum class ModelFormat { Unknown = 0, SMD, FBX, GR2 };

const char* modelFormatName(ModelFormat fmt);
ModelFormat detectModelFormat(const std::filesystem::path& path);

struct ExtractedMeshData {
    Mesh mesh;
    Skeleton skeleton;
    std::vector<SmdFrame> frames;
    std::string sourceFormat;
    bool hasAnimationData = false;
};

class IWeightExtractor {
public:
    virtual ~IWeightExtractor() = default;
    virtual Result<ExtractedMeshData> extractWeights(const std::filesystem::path& filePath) = 0;
    virtual bool supportsFormat(ModelFormat fmt) const = 0;
};

class SmdWeightExtractor : public IWeightExtractor {
public:
    bool supportsFormat(ModelFormat fmt) const override { return fmt == ModelFormat::SMD; }
    Result<ExtractedMeshData> extractWeights(const std::filesystem::path& filePath) override;
};

class NoesisBridgeExtractor : public IWeightExtractor {
public:
    explicit NoesisBridgeExtractor(std::filesystem::path noesisCli);
    bool supportsFormat(ModelFormat fmt) const override;
    Result<ExtractedMeshData> extractWeights(const std::filesystem::path& filePath) override;

private:
    std::filesystem::path noesisCli_;
};

class UniversalWeightExtractor {
public:
    UniversalWeightExtractor();
    void registerExtractor(ModelFormat fmt, std::unique_ptr<IWeightExtractor> extractor);
    Result<ExtractedMeshData> extractFromFile(const std::filesystem::path& filePath);
    static ModelFormat detectFormat(const std::filesystem::path& filePath);

private:
    std::vector<std::pair<ModelFormat, std::unique_ptr<IWeightExtractor>>> extractors_;
};

}  // namespace m2rig
