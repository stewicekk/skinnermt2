#pragma once
// Self-learning weight transfer and auto-rig system for Metin2.
// Learns from 00-BONES 3ds Max presets, real GR2 files, and user corrections.
// Provides character-type-aware weight transfer, auto-rigging, and bone mapping.
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <memory>
#include <fstream>
#include <sstream>
#include <filesystem>

#include "m2rig/result.hpp"
#include "m2rig/math.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/skin_weights.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/gr2_deep_parser.hpp"

namespace m2rig {

// Learning database entry for a character type
struct LearningEntry {
    std::string characterKey;      // e.g., "warrior_m_base"
    Gr2CharacterType charType = Gr2CharacterType::Unknown;
    std::string race;
    std::string gender;
    std::string variant;
    
    // Bone mapping from source to target
    std::unordered_map<std::string, std::string> boneMap;     // source -> target
    std::unordered_map<std::string, float> boneConfidence;    // mapping confidence 0-1
    
    // Weight transfer patterns learned from successful transfers
    struct TransferPattern {
        std::string sourceBone;
        std::string targetBone;
        float avgDistance = 0.0f;
        float successRate = 0.0f;
        uint32_t sampleCount = 0;
        std::vector<float> distanceSamples;
    };
    std::vector<TransferPattern> transferPatterns;
    
    // Vertex-to-bone affinity learned from real models
    struct VertexAffinity {
        std::string boneName;
        std::vector<std::pair<uint32_t, float>> vertexWeights; // vertex index -> weight
        Vec3 bonePosition;  // average position of influenced vertices
        float influenceRadius = 0.0f;
    };
    std::vector<VertexAffinity> vertexAffinities;
    
    // Symmetry pairs for this character type
    std::vector<std::pair<std::string, std::string>> symmetryPairs;
    
    // Socket/weapon bone usage
    std::vector<std::string> socketBones;
    std::vector<std::string> weaponBones;
    
    // Statistics
    uint32_t totalSamples = 0;
    uint32_t successfulTransfers = 0;
    double lastUpdated = 0.0;
};

// Self-learning database
class SelfLearningDatabase {
public:
    SelfLearningDatabase();
    ~SelfLearningDatabase();
    
    // Load/save database
    ResultVoid load(const std::filesystem::path& path);
    ResultVoid save(const std::filesystem::path& path) const;
    
    // Get or create entry for character type
    LearningEntry* getOrCreateEntry(const Gr2CharacterProfile& profile);
    const LearningEntry* getEntry(const std::string& characterKey) const;
    
    // Record a successful weight transfer
    void recordTransfer(const Gr2CharacterProfile& sourceProfile,
                        const Gr2CharacterProfile& targetProfile,
                        const std::string& sourceBone,
                        const std::string& targetBone,
                        float distance,
                        bool success);
    
    // Record vertex affinity from a successfully rigged model
    void recordVertexAffinities(const Gr2CharacterProfile& profile,
                                const Mesh& mesh,
                                const Skeleton& skeleton);
    
    // Get best bone mapping for a transfer
    std::optional<std::string> getBestMapping(const Gr2CharacterProfile& sourceProfile,
                                              const Gr2CharacterProfile& targetProfile,
                                              const std::string& sourceBone) const;
    
    // Get transfer confidence
    float getTransferConfidence(const Gr2CharacterProfile& sourceProfile,
                                const Gr2CharacterProfile& targetProfile,
                                const std::string& sourceBone,
                                const std::string& targetBone) const;
    
    // Get recommended symmetry pairs
    const std::vector<std::pair<std::string, std::string>>& getSymmetryPairs(
        const Gr2CharacterProfile& profile) const;
    
    // Get socket/weapon bones
    const std::vector<std::string>& getSocketBones(const Gr2CharacterProfile& profile) const;
    const std::vector<std::string>& getWeaponBones(const Gr2CharacterProfile& profile) const;
    
    // Statistics
    void printStats() const;

private:
    std::unordered_map<std::string, LearningEntry> entries_;
    std::filesystem::path dbPath_;
    void updateEntryStats(LearningEntry& entry);
    LearningEntry* getOrCreateEntryByKey(const std::string& key);
    const std::unordered_map<std::string, LearningEntry>& getEntries() const { return entries_; }
};

// Self-learning weight transfer engine
class SelfLearningTransfer {
public:
    SelfLearningTransfer() = default;
    SelfLearningTransfer(SelfLearningDatabase& db);
    
    // Transfer weights with self-learning
    Result<WeightTransferStats> transfer(SelfLearningDatabase& db,
                                         const Mesh& sourceMesh,
                                         const Skeleton& sourceSkeleton,
                                         const Gr2CharacterProfile& sourceProfile,
                                         Mesh& targetMesh,
                                         Skeleton& targetSkeleton,
                                         const Gr2CharacterProfile& targetProfile,
                                         uint32_t kNearest = 3,
                                         const std::set<uint32_t>* lockedBones = nullptr);
    
    // Auto-rig a mesh using learned patterns
    Result<AutoRigStats> autoRig(SelfLearningDatabase& db,
                                 Mesh& mesh,
                                 Skeleton& skeleton,
                                 const Gr2CharacterProfile& profile,
                                 float maxDistance = 0.02f,
                                 const std::set<uint32_t>* lockedBones = nullptr);

private:
    SelfLearningDatabase& db_;
    
    // Find k-nearest bones using learned patterns
    std::vector<std::pair<uint32_t, float>> findNearestBonesLearned(
        const Vec3& vertexPos,
        const Skeleton& skeleton,
        const Gr2CharacterProfile& profile,
        uint32_t k) const;
    
    // Apply learned vertex affinities
    void applyLearnedAffinities(Mesh& mesh,
                                const Skeleton& skeleton,
                                const Gr2CharacterProfile& profile) const;
};

// GR2 Batch Analyzer - analyzes all GR2 files to build initial database
class Gr2BatchAnalyzer {
public:
    Gr2BatchAnalyzer(SelfLearningDatabase& db);
    
    // Analyze all GR2 files in a directory
    ResultVoid analyzeDirectory(const std::filesystem::path& dirPath);
    
    // Analyze 00-BONES 3ds Max preset files (via exported SMD/FBX)
    ResultVoid analyzeBonesPresets(const std::filesystem::path& bonesDir);
    
    // Build initial learning database from analysis
    ResultVoid buildInitialDatabase();
    
    // Export analysis report
    ResultVoid exportReport(const std::filesystem::path& outputPath) const;

private:
    SelfLearningDatabase& db_;
    Gr2DeepParser parser_;
    
    struct AnalysisResult {
        std::string filename;
        Gr2CharacterProfile profile;
        Gr2ParseResult parseResult;
        bool success = false;
        std::string error;
    };
    std::vector<AnalysisResult> results_;
    
    void processGr2File(const std::filesystem::path& filePath);
    void extractBoneHierarchy(const Gr2ParseResult& result, LearningEntry& entry);
    void extractVertexAffinities(const Gr2ParseResult& result, LearningEntry& entry);
    void detectSymmetryPairs(const Gr2ParseResult& result, LearningEntry& entry);
};

}  // namespace m2rig