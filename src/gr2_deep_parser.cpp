// Deep GR2 parser implementation for Metin2 GR2 variant (magic 29DE6CC0).
// Reverse-engineered parser for the Granny 2.x variant used by Metin2.
#include "m2rig/gr2_deep_parser.hpp"

#include <fstream>
#include <algorithm>
#include <cstring>
#include <iostream>

#include "m2rig/logging.hpp"
#include "m2rig/profiles.hpp"
#include "m2rig/coordsys.hpp"

namespace m2rig {

struct Gr2DeepParser::Impl {
    // Buffer for parsing
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    
    // Helper to check bounds
    bool checkBounds(size_t offset, size_t needed) const {
        return offset + needed <= dataSize;
    }
};

Gr2DeepParser::Gr2DeepParser() : pImpl(std::make_unique<Impl>()) {}
Gr2DeepParser::~Gr2DeepParser() = default;

bool Gr2DeepParser::checkBounds(size_t offset, size_t needed) const {
    return pImpl->checkBounds(offset, needed);
}

bool Gr2DeepParser::isMetin2Gr2(const uint8_t* data, size_t size) const {
    if (size < 4) return false;
    uint32_t magic = *reinterpret_cast<const uint32_t*>(data);
    return magic == GR2_METIN2_MAGIC;
}

Result<void> Gr2DeepParser::parseHeader(const uint8_t* data, size_t size, Gr2Header& header, size_t& offset) {
    if (size < offset + 32) return Result<void>::fail("File too small for header", "GR2_PARSE");
    
    header.magic = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.version = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.totalSize = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.crc32 = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.sectionCount = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.rootSectionOffset = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.rootSectionSize = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    header.reserved = *reinterpret_cast<const uint32_t*>(data + offset); offset += 4;
    
    if (header.magic != GR2_METIN2_MAGIC) {
        return Result<void>::fail("Invalid Metin2 GR2 magic: 0x" + 
            std::to_string(header.magic), "GR2_PARSE");
    }
    
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseSections(const uint8_t* data, size_t size, const Gr2Header& header,
                                           std::vector<Gr2Section>& sections) {
    // Sections are typically stored in a table after the header
    // Each entry: type(4), offset(4), size(4), dataOffset(4) = 16 bytes
    size_t sectionTableOffset = 32; // After header
    
    if (!checkBounds(sectionTableOffset, header.sectionCount * 16)) {
        return Result<void>::fail("Section table out of bounds", "GR2_PARSE");
    }
    
    sections.resize(header.sectionCount);
    for (uint32_t i = 0; i < header.sectionCount; ++i) {
        Gr2Section& sec = sections[i];
        sec.type = static_cast<Gr2SectionType>(*reinterpret_cast<const uint32_t*>(data + sectionTableOffset)); sectionTableOffset += 4;
        sec.offset = *reinterpret_cast<const uint32_t*>(data + sectionTableOffset); sectionTableOffset += 4;
        sec.size = *reinterpret_cast<const uint32_t*>(data + sectionTableOffset); sectionTableOffset += 4;
        sec.dataOffset = *reinterpret_cast<const uint32_t*>(data + sectionTableOffset); sectionTableOffset += 4;
        
        // Validate and extract section data
        if (sec.dataOffset + sec.size <= size) {
            sec.data.assign(data + sec.dataOffset, data + sec.dataOffset + sec.size);
        }
    }
    
    return Result<void>::ok();
}

template<typename T>
Result<T> Gr2DeepParser::readValue(const uint8_t* data, size_t size, size_t& offset) {
    if (offset + sizeof(T) > size) return Result<T>::fail("Read out of bounds", "GR2_PARSE");
    T val = *reinterpret_cast<const T*>(data + offset);
    offset += sizeof(T);
    return Result<T>::ok(val);
}

Result<uint32_t> Gr2DeepParser::readUInt32(const uint8_t* data, size_t size, size_t& offset) {
    return readValue<uint32_t>(data, size, offset);
}

Result<int32_t> Gr2DeepParser::readInt32(const uint8_t* data, size_t size, size_t& offset) {
    return readValue<int32_t>(data, size, offset);
}

Result<float> Gr2DeepParser::readFloat(const uint8_t* data, size_t size, size_t& offset) {
    return readValue<float>(data, size, offset);
}

Result<uint16_t> Gr2DeepParser::readUInt16(const uint8_t* data, size_t size, size_t& offset) {
    return readValue<uint16_t>(data, size, offset);
}

Result<int16_t> Gr2DeepParser::readInt16(const uint8_t* data, size_t size, size_t& offset) {
    return readValue<int16_t>(data, size, offset);
}

Result<uint8_t> Gr2DeepParser::readUInt8(const uint8_t* data, size_t size, size_t& offset) {
    return readValue<uint8_t>(data, size, offset);
}

Result<Vec3> Gr2DeepParser::readVec3(const uint8_t* data, size_t size, size_t& offset) {
    auto x = readFloat(data, size, offset);
    auto y = readFloat(data, size, offset);
    auto z = readFloat(data, size, offset);
    if (!x || !y || !z) return Result<Vec3>::fail("Failed to read Vec3", "GR2_PARSE");
    return Result<Vec3>::ok(Vec3{x.value(), y.value(), z.value()});
}

Result<Vec2> Gr2DeepParser::readVec2(const uint8_t* data, size_t size, size_t& offset) {
    auto x = readFloat(data, size, offset);
    auto y = readFloat(data, size, offset);
    if (!x || !y) return Result<Vec2>::fail("Failed to read Vec2", "GR2_PARSE");
    return Result<Vec2>::ok(Vec2{x.value(), y.value()});
}

Result<Mat4> Gr2DeepParser::readMatrix(const uint8_t* data, size_t size, size_t& offset) {
    Mat4 m;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            auto v = readFloat(data, size, offset);
            if (!v) return Result<Mat4>::fail("Failed to read matrix", "GR2_PARSE");
            m.m[r][c] = v.value();
        }
    }
    return Result<Mat4>::ok(m);
}

Result<std::string> Gr2DeepParser::readString(const uint8_t* data, size_t size, size_t& offset) {
    auto len = readUInt32(data, size, offset);
    if (!len) return Result<std::string>::fail("Failed to read string length", "GR2_PARSE");
    
    uint32_t strLen = len.value();
    if (strLen > 1024) strLen = 1024; // Sanity limit
    if (offset + strLen > size) strLen = static_cast<uint32_t>(size - offset);
    
    std::string str(reinterpret_cast<const char*>(data + offset), strLen);
    offset += strLen;
    return Result<std::string>::ok(str);
}

Result<void> Gr2DeepParser::parseSkeleton(const Gr2Section& section, Gr2ParseResult& result) {
    if (section.data.empty()) return Result<void>::ok();
    
    size_t offset = 0;
    const uint8_t* data = section.data.data();
    size_t size = section.data.size();
    
    // Skeleton section typically starts with bone count
    auto boneCount = readUInt32(data, size, offset);
    if (!boneCount) return Result<void>::fail("Failed to read bone count", "GR2_PARSE");
    
    uint32_t count = boneCount.value();
    if (count > 256) count = 256; // Sanity limit
    
    result.bones.reserve(count);
    
    for (uint32_t i = 0; i < count; ++i) {
        Gr2Bone bone;
        
        // Read bone name
        auto name = readString(data, size, offset);
        if (name) bone.name = name.value();
        else bone.name = "bone_" + std::to_string(i);
        
        // Read parent index
        auto parent = readInt32(data, size, offset);
        if (parent) bone.parentIndex = parent.value();
        
        // Read local transform (4x4 matrix)
        auto mat = readMatrix(data, size, offset);
        if (mat) {
            bone.localTransform = mat.value();
            // Decompose
            bone.position = {mat.value().m[3][0], mat.value().m[3][1], mat.value().m[3][2]};
            bone.rotation = mat.value().eulerXyzFromRotation();
            bone.scale = {mat.value().m[0][0], mat.value().m[1][1], mat.value().m[2][2]};
        }
        
        // Read inverse bind matrix
        auto invMat = readMatrix(data, size, offset);
        if (invMat) bone.inverseBindTransform = invMat.value();
        
        // Read length
        auto len = readFloat(data, size, offset);
        if (len) bone.length = len.value();
        
        // Detect socket/weapon bones by name
        std::string lowerName = bone.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bone.isSocket = (lowerName.find("equip") != std::string::npos || 
                        lowerName.find("stip") != std::string::npos ||
                        lowerName.find("socket") != std::string::npos);
        bone.isWeapon = (lowerName.find("weapon") != std::string::npos ||
                        lowerName.find("sword") != std::string::npos ||
                        lowerName.find("hand") != std::string::npos);
        
        result.bones.push_back(std::move(bone));
    }
    
    result.hasSkeleton = true;
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseMeshBinding(const Gr2Section& section, Gr2ParseResult& result) {
    // Mesh binding links bones to mesh vertices (skinning data)
    if (section.data.empty()) return Result<void>::ok();
    
    // This section typically contains bone indices per vertex
    // We'll parse it when we have mesh data
    result.hasWeights = true;
    (void)section; // suppress unused warning
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseMesh(const Gr2Section& section, Gr2ParseResult& result) {
    if (section.data.empty()) return Result<void>::ok();
    
    size_t offset = 0;
    const uint8_t* data = section.data.data();
    size_t size = section.data.size();
    
    // Try to parse mesh data - format varies
    auto meshCount = readUInt32(data, size, offset);
    if (!meshCount) return Result<void>::ok(); // Not a mesh section or different format
    
    uint32_t count = std::min(meshCount.value(), 16u);
    
    for (uint32_t m = 0; m < count; ++m) {
        Gr2Mesh mesh;
        
        auto name = readString(data, size, offset);
        if (name) mesh.name = name.value();
        else mesh.name = "mesh_" + std::to_string(m);
        
        auto matName = readString(data, size, offset);
        if (matName) mesh.materialName = matName.value();
        
        auto vCount = readUInt32(data, size, offset);
        if (vCount) mesh.vertexCount = std::min(vCount.value(), 50000u);
        
        auto tCount = readUInt32(data, size, offset);
        if (tCount) mesh.triangleCount = std::min(tCount.value(), 100000u);
        
        auto vFormat = readUInt32(data, size, offset);
        if (vFormat) mesh.vertexFormat = vFormat.value();
        
        // Try to read vertex data
        if (mesh.vertexCount > 0) {
            // Positions
            mesh.positions.resize(mesh.vertexCount);
            for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
                auto pos = readVec3(data, size, offset);
                if (pos) mesh.positions[v] = pos.value();
            }
            
            // Normals
            mesh.normals.resize(mesh.vertexCount);
            for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
                auto nrm = readVec3(data, size, offset);
                if (nrm) mesh.normals[v] = nrm.value();
            }
            
            // UVs
            mesh.uvs.resize(mesh.vertexCount);
            for (uint32_t v = 0; v < mesh.vertexCount; ++v) {
                auto uv = readVec2(data, size, offset);
                if (uv) mesh.uvs[v] = uv.value();
            }
        }
        
        // Indices
        if (mesh.triangleCount > 0) {
            mesh.indices.resize(mesh.triangleCount * 3);
            for (uint32_t i = 0; i < mesh.triangleCount * 3; ++i) {
                auto idx = readUInt16(data, size, offset);
                if (idx) mesh.indices[i] = idx.value();
            }
        }
        
        result.meshes.push_back(std::move(mesh));
    }
    
    result.hasMesh = true;
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseMaterial(const Gr2Section& section, Gr2ParseResult& result) {
    if (section.data.empty()) return Result<void>::ok();
    
    size_t offset = 0;
    const uint8_t* data = section.data.data();
    size_t size = section.data.size();
    
    auto matCount = readUInt32(data, size, offset);
    if (!matCount) return Result<void>::ok();
    
    uint32_t count = std::min(matCount.value(), 32u);
    
    for (uint32_t i = 0; i < count; ++i) {
        Gr2Material mat;
        auto name = readString(data, size, offset);
        if (name) mat.name = name.value();
        
        auto tex = readString(data, size, offset);
        if (tex) mat.texturePath = tex.value();
        
        auto diff = readVec3(data, size, offset);
        if (diff) mat.diffuse = {diff.value().x, diff.value().y, diff.value().z, 1.0f};
        
        auto spec = readVec3(data, size, offset);
        if (spec) mat.specular = {spec.value().x, spec.value().y, spec.value().z, 1.0f};
        
        auto shin = readFloat(data, size, offset);
        if (shin) mat.shininess = shin.value();
        
        result.materials.push_back(std::move(mat));
    }
    
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseAnimation(const Gr2Section& section, Gr2ParseResult& result) {
    if (section.data.empty()) return Result<void>::ok();
    
    size_t offset = 0;
    const uint8_t* data = section.data.data();
    size_t size = section.data.size();
    
    auto animCount = readUInt32(data, size, offset);
    if (!animCount) return Result<void>::ok();
    
    uint32_t count = std::min(animCount.value(), 16u);
    
    for (uint32_t a = 0; a < count; ++a) {
        Gr2Animation anim;
        auto name = readString(data, size, offset);
        if (name) anim.name = name.value();
        else anim.name = "anim_" + std::to_string(a);
        
        auto dur = readFloat(data, size, offset);
        if (dur) anim.duration = dur.value();
        
        auto fps = readFloat(data, size, offset);
        if (fps) anim.frameRate = fps.value();
        
        // Frame count
        auto frameCount = readUInt32(data, size, offset);
        uint32_t frames = frameCount ? std::min(frameCount.value(), 5000u) : 0;
        
        // Per-bone per-frame transforms
        if (frames > 0 && !result.bones.empty()) {
            anim.boneFrames.resize(frames);
            for (uint32_t f = 0; f < frames; ++f) {
                anim.boneFrames[f].resize(result.bones.size());
                for (size_t b = 0; b < result.bones.size(); ++b) {
                    auto mat = readMatrix(data, size, offset);
                    if (mat) anim.boneFrames[f][b] = mat.value();
                }
            }
        }
        
        result.animations.push_back(std::move(anim));
    }
    
    result.hasAnimation = true;
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseVertexData(const Gr2Section&, Gr2ParseResult&, size_t) {
    // Vertex data section - additional vertex attributes
    return Result<void>::ok();
}

Result<void> Gr2DeepParser::parseIndexData(const Gr2Section&, Gr2ParseResult&, size_t) {
    // Index data section
    return Result<void>::ok();
}

Result<Gr2ParseResult> Gr2DeepParser::parse(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return Result<Gr2ParseResult>::fail("Cannot open file: " + path, "GR2_PARSE");
    
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    file.close();
    
    return parseFromMemory(buffer.data(), buffer.size());
}

Result<Gr2ParseResult> Gr2DeepParser::parseFromMemory(const uint8_t* data, size_t size) {
    Gr2ParseResult result;
    
    if (!isMetin2Gr2(data, size)) {
        return Result<Gr2ParseResult>::fail("Not a Metin2 GR2 file", "GR2_PARSE");
    }
    
    pImpl->data = data;
    pImpl->dataSize = size;
    
    size_t offset = 0;
    Gr2Header header;
    
    auto hr = parseHeader(data, size, header, offset);
    if (!hr) return Result<Gr2ParseResult>::fail(hr.error().message, "GR2_PARSE");
    
    result.header = header;
    
    // Parse section table
    std::vector<Gr2Section> sections;
    hr = parseSections(data, size, header, sections);
    if (!hr) return Result<Gr2ParseResult>::fail(hr.error().message, "GR2_PARSE");
    
    result.sections = std::move(sections);
    
    // Parse each section by type
    for (const auto& section : result.sections) {
        switch (section.type) {
            case Gr2SectionType::Skeleton:
                parseSkeleton(section, result);
                break;
            case Gr2SectionType::MeshBinding:
                parseMeshBinding(section, result);
                break;
            case Gr2SectionType::Mesh:
                parseMesh(section, result);
                break;
            case Gr2SectionType::Material:
                parseMaterial(section, result);
                break;
            case Gr2SectionType::Animation:
                parseAnimation(section, result);
                break;
            case Gr2SectionType::VertexData:
            case Gr2SectionType::IndexData:
                // Will be parsed with mesh context
                break;
            default:
                break;
        }
    }
    
    return Result<Gr2ParseResult>::ok(std::move(result));
}

Result<Skeleton> Gr2DeepParser::toSkeleton(const Gr2ParseResult& parseResult) const {
    std::vector<BoneDefinition> defs;
    defs.reserve(parseResult.bones.size());
    
    for (const auto& b : parseResult.bones) {
        BoneDefinition def;
        def.name = b.name;
        def.parentId = b.parentIndex;
        def.localPosition = b.position;
        def.localRotationEuler = b.rotation;
        def.localScale = b.scale;
        defs.push_back(def);
    }
    
    std::string name = "gr2_skeleton";
    if (!parseResult.bones.empty()) {
        name = parseResult.bones[0].name;
        size_t pos = name.find("Bip01");
        if (pos != std::string::npos) name = name.substr(0, pos) + "root";
    }
    
    return buildSkeleton(name, defs);
}

Result<Mesh> Gr2DeepParser::toMesh(const Gr2ParseResult& parseResult, size_t meshIndex) const {
    if (meshIndex >= parseResult.meshes.size()) {
        return Result<Mesh>::fail("Mesh index out of range", "GR2_CONVERT");
    }
    
    const Gr2Mesh& src = parseResult.meshes[meshIndex];
    Mesh mesh;
    mesh.name = src.name;
    
    mesh.vertices.resize(src.vertexCount);
    for (size_t i = 0; i < src.vertexCount; ++i) {
        Vertex v;
        if (i < src.positions.size()) v.position = src.positions[i];
        if (i < src.normals.size()) v.normal = src.normals[i];
        if (i < src.uvs.size()) v.uv0 = src.uvs[i];
        if (i < src.colors.size()) v.color = src.colors[i];
        mesh.vertices[i] = v;
    }
    
    mesh.indices = src.indices;
    
    // Add submesh
    SubMesh sm;
    sm.materialIndex = 0;
    sm.startIndex = 0;
    sm.indexCount = static_cast<uint32_t>(src.indices.size());
    mesh.subMeshes.push_back(sm);
    
    // Add material
    if (!src.materialName.empty()) {
        MaterialRef mat;
        mat.name = src.materialName;
        // Find matching material
        for (const auto& m : parseResult.materials) {
            if (m.name == src.materialName) {
                mat.texturePath = m.texturePath;
                break;
            }
        }
        mesh.materials.push_back(mat);
    }
    
    // Compute bounds
    for (const auto& v : mesh.vertices) {
        mesh.bounds.grow(v.position);
    }
    
    return Result<Mesh>::ok(std::move(mesh));
}

Result<std::vector<SmdFrame>> Gr2DeepParser::toAnimFrames(const Gr2ParseResult& parseResult) const {
    std::vector<SmdFrame> frames;
    
    if (parseResult.animations.empty()) return Result<std::vector<SmdFrame>>::ok(frames);
    
    // Use first animation
    const Gr2Animation& anim = parseResult.animations[0];
    frames.resize(anim.boneFrames.size());
    
    for (size_t f = 0; f < anim.boneFrames.size(); ++f) {
        SmdFrame frame;
        frame.time = static_cast<int>(f * (anim.duration / anim.boneFrames.size()));
        frame.poses.reserve(anim.boneFrames[f].size());
        
        for (size_t b = 0; b < anim.boneFrames[f].size(); ++b) {
            if (b < parseResult.bones.size()) {
                SmdBonePose pose;
                pose.boneId = static_cast<uint32_t>(b);
                pose.position = {anim.boneFrames[f][b].m[3][0], anim.boneFrames[f][b].m[3][1], anim.boneFrames[f][b].m[3][2]};
                pose.rotation = anim.boneFrames[f][b].eulerXyzFromRotation();
                frame.poses.push_back(pose);
            }
        }
        frames[f] = std::move(frame);
    }
    
    return Result<std::vector<SmdFrame>>::ok(std::move(frames));
}

Result<std::string> Gr2DeepParser::exportToSmd(const Gr2ParseResult& parseResult, const std::string&) const {
    auto skel = toSkeleton(parseResult);
    if (!skel) return Result<std::string>::fail(skel.error().message, "GR2_EXPORT");
    
    auto mesh = toMesh(parseResult, 0);
    if (!mesh) return Result<std::string>::fail(mesh.error().message, "GR2_EXPORT");
    
    auto frames = toAnimFrames(parseResult);
    if (!frames) return Result<std::string>::fail(frames.error().message, "GR2_EXPORT");
    
    auto written = writeSmd(mesh.value(), skel.value(), frames.value());
    if (!written) return Result<std::string>::fail(written.error().message, "GR2_EXPORT");
    return Result<std::string>::ok(written.value().text);
}

Gr2CharacterProfile detectGr2Character(const std::string& filename, const Gr2ParseResult&) {
    Gr2CharacterProfile profile;
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    // Determine race
    if (lower.find("warrior") != std::string::npos || lower.find("woj") != std::string::npos) {
        profile.race = "warrior";
    } else if (lower.find("assassin") != std::string::npos || lower.find("ninja") != std::string::npos) {
        profile.race = "assassin";
    } else if (lower.find("sura") != std::string::npos || lower.find("surka") != std::string::npos) {
        profile.race = "sura";
    } else if (lower.find("shaman") != std::string::npos || lower.find("szaman") != std::string::npos || lower.find("szamanka") != std::string::npos) {
        profile.race = "shaman";
    } else if (lower.find("wolfman") != std::string::npos) {
        profile.race = "wolfman";
    } else if (lower.find("gryphon") != std::string::npos || lower.find("mount") != std::string::npos) {
        profile.race = "mount";
    } else {
        profile.race = "unknown";
    }
    
    // Determine gender
    if (lower.find("_f.") != std::string::npos || lower.find("_female") != std::string::npos ||
        lower.find("szamanka") != std::string::npos || lower.find("surka") != std::string::npos) {
        profile.gender = "female";
        profile.type = (profile.race == "warrior") ? Gr2CharacterType::WarriorFemale :
                      (profile.race == "assassin") ? Gr2CharacterType::AssassinFemale :
                      (profile.race == "sura") ? Gr2CharacterType::SuraFemale :
                      (profile.race == "shaman") ? Gr2CharacterType::ShamanFemale :
                      Gr2CharacterType::Unknown;
    } else if (lower.find("_m.") != std::string::npos || lower.find("_male") != std::string::npos ||
               lower.find("woj.") != std::string::npos || lower.find("szaman") != std::string::npos) {
        profile.gender = "male";
        profile.type = (profile.race == "warrior") ? Gr2CharacterType::WarriorMale :
                      (profile.race == "assassin") ? Gr2CharacterType::AssassinMale :
                      (profile.race == "sura") ? Gr2CharacterType::SuraMale :
                      (profile.race == "shaman") ? Gr2CharacterType::ShamanMale :
                      Gr2CharacterType::Unknown;
    } else {
        profile.gender = "none";
        if (profile.race == "mount") profile.type = Gr2CharacterType::Mount;
        else if (profile.race == "wolfman") profile.type = Gr2CharacterType::Wolfman;
        else profile.type = Gr2CharacterType::MobPetNpc;
    }
    
    // Determine variant
    if (lower.find("w2") != std::string::npos || lower.find("_w2") != std::string::npos ||
        lower.find("_m2") != std::string::npos) {
        profile.variant = "weapon2";
    } else if (lower.find("_w") != std::string::npos || lower.find("_m") != std::string::npos) {
        profile.variant = "weapon";
    } else if (lower.find("out") != std::string::npos) {
        profile.variant = "exported";
    } else {
        profile.variant = "base";
    }
    
    // Build profile ID
    if (profile.race != "unknown" && profile.race != "mount" && profile.race != "mobpetnpc") {
        profile.profileId = "pc_" + profile.race + (profile.gender == "male" ? "_m" : profile.gender == "female" ? "_f" : "");
    } else if (profile.race == "mount") {
        profile.profileId = "pc_mount";
    } else {
        profile.profileId = "pc_warrior"; // fallback
    }
    
    // Set expected bone counts based on race
    if (profile.race == "warrior" || profile.race == "assassin" || 
        profile.race == "sura" || profile.race == "shaman") {
        profile.expectedBones = (profile.gender == "male") ? 90 : 90; // Both have ~90 with fingers
    } else if (profile.race == "mount") {
        profile.expectedBones = 37;
    } else {
        profile.expectedBones = 50;
    }
    
    return profile;
}

const SkeletonProfile* getProfileForGr2Character(const Gr2CharacterProfile& charProfile) {
    return findProfile(charProfile.profileId);
}

}  // namespace m2rig