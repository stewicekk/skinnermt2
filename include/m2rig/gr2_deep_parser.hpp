#pragma once
// Deep GR2 parser for Metin2 GR2 variant (magic 29DE6CC0).
// Extracts full skeleton hierarchy, bone weights, mesh geometry, and animations.
// Based on reverse-engineered Metin2 Granny 2.x variant format.
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <memory>

#include "m2rig/result.hpp"
#include "m2rig/math.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/profiles.hpp"

namespace m2rig {

// Metin2 GR2 constants
constexpr uint32_t GR2_METIN2_MAGIC = 0x29DE6CC0;  // 29 DE 6C C0
constexpr uint32_t GR2_STANDARD_MAGIC = 0x00677267; // "gr2\0"

enum class Gr2SectionType : uint32_t {
    Unknown = 0,
    FileInfo = 1,
    Skeleton = 2,
    MeshBinding = 3,
    Mesh = 4,
    Material = 5,
    Animation = 6,
    TrackGroup = 7,
    VertexData = 8,
    IndexData = 9,
    Texture = 10,
    ExtendedData = 0xFFFFFFFF
};

struct Gr2Header {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t totalSize = 0;
    uint32_t crc32 = 0;
    uint32_t sectionCount = 0;
    uint32_t rootSectionOffset = 0;
    uint32_t rootSectionSize = 0;
    uint32_t reserved = 0;
};

struct Gr2Section {
    Gr2SectionType type = Gr2SectionType::Unknown;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t dataOffset = 0;
    std::vector<uint8_t> data;
};

struct Gr2Bone {
    std::string name;
    int32_t parentIndex = -1;
    Mat4 localTransform = Mat4::identity();
    Mat4 inverseBindTransform = Mat4::identity();
    Vec3 position = {0, 0, 0};
    Vec3 rotation = {0, 0, 0}; // Euler XYZ radians
    Vec3 scale = {1, 1, 1};
    float length = 0.0f;
    bool isSocket = false;
    bool isWeapon = false;
};

struct Gr2Mesh {
    std::string name;
    std::string materialName;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t vertexFormat = 0;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Vec4> colors;
    std::vector<uint32_t> indices;
    std::vector<std::vector<BoneInfluence>> boneWeights; // per vertex
};

struct Gr2Animation {
    std::string name;
    float duration = 0.0f;
    float frameRate = 30.0f;
    std::vector<std::vector<Mat4>> boneFrames; // [frame][bone] -> local transform
};

struct Gr2Material {
    std::string name;
    std::string texturePath;
    Vec4 diffuse = {1, 1, 1, 1};
    Vec4 specular = {0, 0, 0, 1};
    float shininess = 0.0f;
    bool twoSided = false;
    bool transparent = false;
};

struct Gr2ParseResult {
    Gr2Header header;
    std::vector<Gr2Bone> bones;
    std::vector<Gr2Mesh> meshes;
    std::vector<Gr2Animation> animations;
    std::vector<Gr2Material> materials;
    std::vector<Gr2Section> sections;
    std::string formatType = "Metin2 GR2";
    bool hasSkeleton = false;
    bool hasWeights = false;
    bool hasAnimation = false;
    bool hasMesh = false;
};

class Gr2DeepParser {
public:
    Gr2DeepParser();
    ~Gr2DeepParser();

    // Parse a Metin2 GR2 file
    Result<Gr2ParseResult> parse(const std::string& path);
    
    // Parse from memory buffer
    Result<Gr2ParseResult> parseFromMemory(const uint8_t* data, size_t size);

    // Convert to m2rig Skeleton
    Result<Skeleton> toSkeleton(const Gr2ParseResult& parseResult) const;
    
    // Convert to m2rig Mesh
    Result<Mesh> toMesh(const Gr2ParseResult& parseResult, size_t meshIndex = 0) const;
    
    // Convert to m2rig AnimFrames
    Result<std::vector<SmdFrame>> toAnimFrames(const Gr2ParseResult& parseResult) const;

    // Export to SMD text
    Result<std::string> exportToSmd(const Gr2ParseResult& parseResult, const std::string& assetId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;

    bool isMetin2Gr2(const uint8_t* data, size_t size) const;
    Result<void> parseHeader(const uint8_t* data, size_t size, Gr2Header& header, size_t& offset);
    Result<void> parseSections(const uint8_t* data, size_t size, const Gr2Header& header,
                               std::vector<Gr2Section>& sections);
    Result<void> parseSkeleton(const Gr2Section& section, Gr2ParseResult& result);
    Result<void> parseMeshBinding(const Gr2Section& section, Gr2ParseResult& result);
    Result<void> parseMesh(const Gr2Section& section, Gr2ParseResult& result);
    Result<void> parseMaterial(const Gr2Section& section, Gr2ParseResult& result);
    Result<void> parseAnimation(const Gr2Section& section, Gr2ParseResult& result);
    Result<void> parseVertexData(const Gr2Section& section, Gr2ParseResult& result, size_t meshIndex);
    Result<void> parseIndexData(const Gr2Section& section, Gr2ParseResult& result, size_t meshIndex);
    
    // Helper: read various types from buffer with bounds checking
    template<typename T>
    Result<T> readValue(const uint8_t* data, size_t size, size_t& offset);
    
    Result<std::string> readString(const uint8_t* data, size_t size, size_t& offset);
    Result<Mat4> readMatrix(const uint8_t* data, size_t size, size_t& offset);
    Result<Vec3> readVec3(const uint8_t* data, size_t size, size_t& offset);
    Result<Vec2> readVec2(const uint8_t* data, size_t size, size_t& offset);
    Result<uint32_t> readUInt32(const uint8_t* data, size_t size, size_t& offset);
    Result<int32_t> readInt32(const uint8_t* data, size_t size, size_t& offset);
    Result<float> readFloat(const uint8_t* data, size_t size, size_t& offset);
    Result<uint16_t> readUInt16(const uint8_t* data, size_t size, size_t& offset);
    Result<int16_t> readInt16(const uint8_t* data, size_t size, size_t& offset);
    Result<uint8_t> readUInt8(const uint8_t* data, size_t size, size_t& offset);
    
    // Bounds checking helper (delegates to Impl)
    bool checkBounds(size_t offset, size_t needed) const;
};

// Character type detection from GR2
enum class Gr2CharacterType {
    Unknown = 0,
    WarriorMale,
    WarriorFemale,
    AssassinMale,
    AssassinFemale,
    SuraMale,
    SuraFemale,
    ShamanMale,
    ShamanFemale,
    Wolfman,
    Mount,
    MobPetNpc
};

struct Gr2CharacterProfile {
    Gr2CharacterType type = Gr2CharacterType::Unknown;
    std::string race;       // "warrior", "assassin", "sura", "shaman", "wolfman", "mount"
    std::string gender;     // "male", "female", "none"
    std::string variant;    // "base", "weapon", "armor", "costume"
    std::string profileId;  // e.g., "pc_warrior_m"
    uint32_t expectedBones = 0;
    std::vector<std::string> requiredBones;
    std::vector<std::string> socketBones;
    std::vector<std::string> weaponBones;
};

// Detect character type from filename and content
Gr2CharacterProfile detectGr2Character(const std::string& filename, const Gr2ParseResult& parseResult);

// Get the corresponding m2rig skeleton profile (from profiles.hpp)
const SkeletonProfile* getProfileForGr2Character(const Gr2CharacterProfile& charProfile);

}  // namespace m2rig