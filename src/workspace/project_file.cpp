#include "m2rig/workspace/project_file.hpp"
#include "m2rig/logging.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <functional>

namespace m2rig {

static std::string currentIsoTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    std::tm tm;
    gmtime_s(&tm, &time_t);
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string jsonEscape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

std::string indentJson(const std::string& json, std::size_t spaces) {
    std::string result;
    std::istringstream streams(json);
    std::string line;
    std::size_t indentLevel = 0;
    
    while (std::getline(streams, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            result += "\n";
            continue;
        }
        
        if (trimmed[0] == '}' || (trimmed.size() > 1 && trimmed[0] == ']' && trimmed.back() == ']')) {
            if (indentLevel > 0) --indentLevel;
            std::string ind(indentLevel * spaces, ' ');
            result += ind + line + "\n";
        } else {
            std::string ind(indentLevel * spaces, ' ');
            result += ind + line + "\n";
            if (trimmed.back() == '{' || trimmed.back() == '[' || 
                trimmed.back() == ',' || trimmed.back() == ':') {
                ++indentLevel;
            }
        }
    }
    return result;
}

SerializableAsset serializeAsset(const LoadedAsset& asset) {
    SerializableAsset serial;
    serial.id = asset.id;
    serial.profileId = asset.profileId;
    serial.mesh = asset.mesh;
    serial.skeleton = asset.skeleton;
    serial.animFrames = asset.animFrames;
    serial.currentFrame = asset.currentFrame;
    serial.sourcePath = asset.sourcePath;
    serial.dirty = asset.dirty;
    serial.gpuDirty = asset.gpuDirty;
    return serial;
}

LoadedAsset deserializeAsset(const SerializableAsset& serial) {
    LoadedAsset asset;
    asset.id = serial.id;
    asset.profileId = serial.profileId;
    asset.mesh = serial.mesh;
    asset.skeleton = serial.skeleton;
    asset.animFrames = serial.animFrames;
    asset.currentFrame = serial.currentFrame;
    asset.sourcePath = serial.sourcePath;
    asset.dirty = serial.dirty;
    asset.gpuDirty = false;
    return asset;
}

Result<void> saveWorkspace(const M2RigWorkspace& workspace,
                              const std::filesystem::path& path) {
    std::ostringstream json;
    
    json << "{\n";
    json << "  \"version\": " << workspace.version << ",\n";
    json << "  \"title\": \"" << jsonEscape(workspace.title) << "\",\n";
    json << "  \"lastModified\": \"" << jsonEscape(workspace.lastModified.empty() ? currentIsoTime() : workspace.lastModified) << "\",\n";
    
    json << "  \"viewport\": {\n";
    json << "    \"fovY\": " << workspace.viewport.fovY << ",\n";
    json << "    \"orthographic\": " << (workspace.viewport.orthographic ? "true" : "false") << ",\n";
    json << "    \"orthoWidth\": " << workspace.viewport.orthoWidth << ",\n";
    json << "    \"orthoHeight\": " << workspace.viewport.orthoHeight << ",\n";
    json << "    \"cameraTarget\": [" << workspace.viewport.cameraTarget[0] << ", " 
         << workspace.viewport.cameraTarget[1] << ", " << workspace.viewport.cameraTarget[2] << "],\n";
    json << "    \"viewMatrix\": [";
    for (int i = 0; i < 16; ++i) {
        json << workspace.viewport.viewMatrix[i];
        if (i < 15) json << ", ";
    }
    json << "]\n";
    json << "  },\n";
    
    json << "  \"brush\": {\n";
    json << "    \"radius\": " << workspace.brush.radius << ",\n";
    json << "    \"strength\": " << workspace.brush.strength << ",\n";
    json << "    \"normalize\": " << (workspace.brush.normalize ? "true" : "false") << ",\n";
    json << "    \"symmetryX\": " << (workspace.brush.symmetryX ? "true" : "false") << ",\n";
    json << "    \"symmetryY\": " << (workspace.brush.symmetryY ? "true" : "false") << ",\n";
    json << "    \"symmetryZ\": " << (workspace.brush.symmetryZ ? "true" : "false") << "\n";
    json << "  },\n";
    
    json << "  \"validation\": {\n";
    json << "    \"summaryLine\": \"" << jsonEscape(workspace.validation.summaryLine) << "\",\n";
    json << "    \"exportBlocked\": " << (workspace.validation.exportBlocked ? "true" : "false") << ",\n";
    json << "    \"vertexCount\": " << workspace.validation.vertexCount << ",\n";
    json << "    \"triangleCount\": " << workspace.validation.triangleCount << ",\n";
    json << "    \"boneCount\": " << workspace.validation.boneCount << ",\n";
    json << "    \"unweightedCount\": " << workspace.validation.unweightedCount << ",\n";
    json << "    \"invalidCount\": " << workspace.validation.invalidCount << ",\n";
    json << "    \"overLimitCount\": " << workspace.validation.overLimitCount << "\n";
    json << "  },\n";
    
    json << "  \"assets\": [\n";
    for (std::size_t i = 0; i < workspace.assets.size(); ++i) {
        const auto& asset = workspace.assets[i];
        json << "    {\n";
        json << "      \"id\": \"" << jsonEscape(asset.id) << "\",\n";
        json << "      \"sourcePath\": \"" << jsonEscape(asset.sourcePath) << "\",\n";
        json << "      \"profileId\": \"" << jsonEscape(asset.profileId) << "\",\n";
        json << "      \"dirty\": " << (asset.dirty ? "true" : "false") << "\n";
        json << "    }" << (i + 1 < workspace.assets.size() ? "," : "") << "\n";
    }
    json << "  ],\n";
    
    json << "  \"currentAssetId\": \"" << jsonEscape(workspace.currentAssetId) << "\",\n";
    json << "  \"lockedBones\": [";
    for (std::size_t i = 0; i < workspace.lockedBoneNames.size(); ++i) {
        if (i > 0) json << ", ";
        json << "\"" << jsonEscape(workspace.lockedBoneNames[i]) << "\"";
    }
    json << "]\n";
    json << "}\n";
    
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return Result<void>::fail("Cannot open workspace file for writing: " + path.string());
    }
    file << json.str();
    file.close();
    
    return Result<void>::ok();
}

Result<M2RigWorkspace> loadWorkspace(const std::filesystem::path& path) {
    M2RigWorkspace workspace;
    
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        return Result<M2RigWorkspace>::fail("Cannot open workspace file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    file.close();
    
    workspace.version = 1;
    workspace.lastModified = currentIsoTime();
    
    std::size_t verPos = text.find("\"version\":");
    if (verPos != std::string::npos) {
        std::size_t valStart = text.find_first_of("0123456789", verPos);
        std::size_t valEnd = text.find_first_not_of("0123456789", valStart);
        if (valStart != std::string::npos && valEnd != std::string::npos) {
            workspace.version = static_cast<std::uint32_t>(
                std::stoul(text.substr(valStart, valEnd - valStart)));
        }
    }
    
    std::size_t titlePos = text.find("\"title\":");
    if (titlePos != std::string::npos) {
        std::size_t valStart = text.find('"', text.find(':', titlePos) + 1);
        if (valStart != std::string::npos) {
            ++valStart;
            std::size_t valEnd = text.find('"', valStart);
            if (valEnd != std::string::npos) {
                workspace.title = text.substr(valStart, valEnd - valStart);
            }
        }
    }
    
    std::size_t currentPos = text.find("\"currentAssetId\":");
    if (currentPos != std::string::npos) {
        std::size_t valStart = text.find('"', text.find(':', currentPos) + 1);
        if (valStart != std::string::npos) {
            ++valStart;
            std::size_t valEnd = text.find('"', valStart);
            if (valEnd != std::string::npos) {
                workspace.currentAssetId = text.substr(valStart, valEnd - valStart);
            }
        }
    }

    // Tolerant: missing key means no locks (older files).
    std::size_t lockPos = text.find("\"lockedBones\":");
    if (lockPos != std::string::npos) {
        std::size_t arrStart = text.find('[', lockPos);
        std::size_t arrEnd =
            arrStart != std::string::npos ? text.find(']', arrStart) : std::string::npos;
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::size_t p = arrStart + 1;
            while (p < arrEnd) {
                const std::size_t q0 = text.find('"', p);
                if (q0 == std::string::npos || q0 >= arrEnd) break;
                const std::size_t q1 = text.find('"', q0 + 1);
                if (q1 == std::string::npos || q1 > arrEnd) break;
                workspace.lockedBoneNames.push_back(text.substr(q0 + 1, q1 - q0 - 1));
                p = q1 + 1;
            }
        }
    }

    return Result<M2RigWorkspace>::ok(std::move(workspace));
}

Result<void> saveCurrentWorkspace(App& app,
                                     const std::filesystem::path& path) {
    M2RigWorkspace workspace;
    workspace.title = "Metin2 Rigging Studio Project";
    workspace.lastModified = currentIsoTime();
    workspace.currentAssetId = app.current;
    
    const LoadedAsset* a = app.currentAsset();
    if (a) {
        workspace.viewport.orthographic = app.camera.orthographic;
        workspace.viewport.fovY = app.camera.fovY;
        workspace.viewport.orthoWidth = 10.0f;
        workspace.viewport.orthoHeight = 10.0f;
    }
    
    workspace.brush.radius = app.brushRadius;
    workspace.brush.strength = app.brushStrength;
    workspace.brush.normalize = true;

    if (const LoadedAsset* la = app.currentAsset()) {
        for (std::uint32_t b : app.lockedBones) {
            if (const Bone* bone = la->skeleton.findById(b))
                workspace.lockedBoneNames.push_back(bone->name);
        }
    }
    
    for (const auto& [id, asset] : app.assets) {
        M2RigAssetEntry entry;
        entry.id = id;
        entry.sourcePath = asset.sourcePath;
        entry.profileId = asset.profileId;
        entry.dirty = asset.dirty;
        workspace.assets.push_back(std::move(entry));
    }
    
    workspace.validation.summaryLine = app.report.summaryLine();
    workspace.validation.exportBlocked = app.report.exportBlocked();
    
    return saveWorkspace(workspace, path);
}

Result<void> restoreWorkspace(App& app,
                                 const std::filesystem::path& path) {
    auto loaded = loadWorkspace(path);
    if (!loaded) return Result<void>::fail(loaded.error());
    
    const auto& workspace = loaded.value();
    app.current = workspace.currentAssetId;
    // Resolve locked names against the current asset (ids are not stable).
    app.lockedBones.clear();
    if (LoadedAsset* la = app.currentAsset()) {
        for (const auto& name : workspace.lockedBoneNames) {
            if (const Bone* bone = la->skeleton.findByName(name))
                app.lockedBones.insert(bone->id);
        }
    }

    return Result<void>::ok();
}

} // namespace m2rig