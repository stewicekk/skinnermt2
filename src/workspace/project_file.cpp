#include "m2rig/workspace/project_file.hpp"
#include "m2rig/logging.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    json << "],\n";
    json << "  \"selectionSets\": {";
    bool firstSet = true;
    for (const auto& [setName, bones] : workspace.selectionSets) {
        if (!firstSet) json << ", ";
        firstSet = false;
        json << "\"" << jsonEscape(setName) << "\": [";
        for (std::size_t i = 0; i < bones.size(); ++i) {
            if (i > 0) json << ", ";
            json << "\"" << jsonEscape(bones[i]) << "\"";
        }
        json << "]";
    }
    json << "},\n";
    json << "  \"hiddenBones\": [";
    for (std::size_t i = 0; i < workspace.hiddenBoneNames.size(); ++i) {
        if (i > 0) json << ", ";
        json << "\"" << jsonEscape(workspace.hiddenBoneNames[i]) << "\"";
    }
    json << "],\n";
    json << "  \"soloBone\": \"" << jsonEscape(workspace.soloBoneName) << "\"\n";
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

    // Tolerant numeric/bool extraction (missing/invalid keys keep defaults).
    auto extractDouble = [&](const char* key, double& out) -> bool {
        const std::string k = std::string("\"") + key + "\":";
        const std::size_t pos = text.find(k);
        if (pos == std::string::npos) return false;
        const char* start = text.c_str() + pos + k.size();
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start || !std::isfinite(v)) return false;
        out = v;
        return true;
    };
    auto extractBool = [&](const char* key, bool& out) -> bool {
        const std::string k = std::string("\"") + key + "\":";
        const std::size_t pos = text.find(k);
        if (pos == std::string::npos) return false;
        const std::size_t v = text.find_first_not_of(" \t", pos + k.size());
        if (v == std::string::npos) return false;
        if (text.compare(v, 4, "true") == 0) {
            out = true;
            return true;
        }
        if (text.compare(v, 5, "false") == 0) {
            out = false;
            return true;
        }
        return false;
    };
    double radius = 0, strength = 0, fovY = 0;
    if (extractDouble("radius", radius) && radius > 0.0 && radius < 100.0)
        workspace.brush.radius = static_cast<float>(radius);
    if (extractDouble("strength", strength) && strength > 0.0 && strength <= 1.0)
        workspace.brush.strength = static_cast<float>(strength);
    bool ortho = false;
    if (extractBool("orthographic", ortho)) workspace.viewport.orthographic = ortho;
    if (extractDouble("fovY", fovY) && fovY > 0.01 && fovY < 3.13)
        workspace.viewport.fovY = static_cast<float>(fovY);

    // Asset entries (tolerant: skips malformed objects, keeps the rest).
    // Needed by restore: without sources there is nothing to reimport.
    auto unescape = [](const std::string& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                const char n = s[i + 1];
                if (n == '"' || n == '\\') {
                    out += n;
                    ++i;
                    continue;
                }
                if (n == 'n') {
                    out += '\n';
                    ++i;
                    continue;
                }
            }
            out += s[i];
        }
        return out;
    };
    auto fieldIn = [&](const std::string& obj, const char* key, std::string& out) -> bool {
        const std::string k = std::string("\"") + key + "\":";
        const std::size_t pos = obj.find(k);
        if (pos == std::string::npos) return false;
        const std::size_t q0 = obj.find('"', pos + k.size());
        if (q0 == std::string::npos) return false;
        std::size_t q1 = q0 + 1;
        while (q1 < obj.size()) {
            if (obj[q1] == '\\') {
                q1 += 2;
                continue;
            }
            if (obj[q1] == '"') break;
            ++q1;
        }
        if (q1 >= obj.size()) return false;
        out = unescape(obj.substr(q0 + 1, q1 - q0 - 1));
        return true;
    };
    const std::size_t assetsPos = text.find("\"assets\":");
    if (assetsPos != std::string::npos) {
        const std::size_t arrStart = text.find('[', assetsPos);
        if (arrStart != std::string::npos) {
            std::size_t p = arrStart + 1;
            while (p < text.size()) {
                const std::size_t o0 = text.find('{', p);
                if (o0 == std::string::npos) break;
                // Entries contain no nested objects: first '}' closes.
                const std::size_t o1 = text.find('}', o0);
                if (o1 == std::string::npos) break;
                const std::string obj = text.substr(o0, o1 - o0 + 1);
                M2RigAssetEntry entry;
                if (fieldIn(obj, "id", entry.id)) {
                    fieldIn(obj, "sourcePath", entry.sourcePath);
                    fieldIn(obj, "profileId", entry.profileId);
                    workspace.assets.push_back(std::move(entry));
                }
                p = o1 + 1;
                // Stop at the array end to avoid swallowing later sections.
                const std::size_t arrEnd = text.find(']', arrStart);
                if (arrEnd != std::string::npos && p > arrEnd) break;
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

    // Selection sets (tolerant: missing key means no sets).
    std::size_t setsPos = text.find("\"selectionSets\":");
    if (setsPos != std::string::npos) {
        std::size_t objStart = text.find('{', setsPos);
        std::size_t objEnd =
            objStart != std::string::npos ? text.find('}', objStart) : std::string::npos;
        if (objStart != std::string::npos && objEnd != std::string::npos) {
            std::size_t p = objStart + 1;
            while (p < objEnd) {
                const std::size_t q0 = text.find('"', p);
                if (q0 == std::string::npos || q0 >= objEnd) break;
                const std::size_t q1 = text.find('"', q0 + 1);
                if (q1 == std::string::npos || q1 > objEnd) break;
                std::string setName = text.substr(q0 + 1, q1 - q0 - 1);
                const std::size_t arrStart = text.find('[', q1);
                const std::size_t arrEnd =
                    arrStart != std::string::npos ? text.find(']', arrStart) : std::string::npos;
                if (arrStart != std::string::npos && arrEnd != std::string::npos && arrStart < objEnd) {
                    std::size_t bp = arrStart + 1;
                    std::vector<std::string> bones;
                    while (bp < arrEnd) {
                        const std::size_t bq0 = text.find('"', bp);
                        if (bq0 == std::string::npos || bq0 >= arrEnd) break;
                        const std::size_t bq1 = text.find('"', bq0 + 1);
                        if (bq1 == std::string::npos || bq1 > arrEnd) break;
                        bones.push_back(text.substr(bq0 + 1, bq1 - bq0 - 1));
                        bp = bq1 + 1;
                    }
                    workspace.selectionSets[setName] = std::move(bones);
                }
                p = arrEnd + 1;
            }
        }
    }

    // Hidden bones
    std::size_t hiddenPos = text.find("\"hiddenBones\":");
    if (hiddenPos != std::string::npos) {
        std::size_t arrStart = text.find('[', hiddenPos);
        std::size_t arrEnd =
            arrStart != std::string::npos ? text.find(']', arrStart) : std::string::npos;
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::size_t p = arrStart + 1;
            while (p < arrEnd) {
                const std::size_t q0 = text.find('"', p);
                if (q0 == std::string::npos || q0 >= arrEnd) break;
                const std::size_t q1 = text.find('"', q0 + 1);
                if (q1 == std::string::npos || q1 > arrEnd) break;
                workspace.hiddenBoneNames.push_back(text.substr(q0 + 1, q1 - q0 - 1));
                p = q1 + 1;
            }
        }
    }

    // Solo bone
    std::size_t soloPos = text.find("\"soloBone\":");
    if (soloPos != std::string::npos) {
        std::size_t valStart = text.find('"', text.find(':', soloPos) + 1);
        if (valStart != std::string::npos) {
            ++valStart;
            std::size_t valEnd = text.find('"', valStart);
            if (valEnd != std::string::npos) {
                workspace.soloBoneName = text.substr(valStart, valEnd - valStart);
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
        for (std::uint32_t b : app.hiddenBones) {
            if (const Bone* bone = la->skeleton.findById(b))
                workspace.hiddenBoneNames.push_back(bone->name);
        }
        if (app.soloBone >= 0) {
            if (const Bone* bone = la->skeleton.findById(static_cast<std::uint32_t>(app.soloBone)))
                workspace.soloBoneName = bone->name;
        }
        for (const auto& [setName, boneNames] : app.boneSelectionSets) {
            std::vector<std::string> names;
            for (const std::string& b : boneNames) {
                if (const Bone* bone = la->skeleton.findByName(b))
                    names.push_back(bone->name);
            }
            if (!names.empty())
                workspace.selectionSets[setName] = std::move(names);
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
    // Reimport asset sources that still exist on disk (the bridge layer
    // handles smd/fbx/gr2). Live session assets always win: painted weights
    // are never clobbered by a disk reimport.
    std::size_t restored = 0;
    std::string firstRestored;
    for (const auto& e : workspace.assets) {
        if (e.sourcePath.empty() || app.assets.count(e.id) != 0) continue;
        std::error_code ec;
        if (!std::filesystem::exists(e.sourcePath, ec)) continue;
        const std::string held = app.current;
        if (app.importBridgedFile(e.sourcePath).succeeded()) {
            ++restored;
            if (firstRestored.empty()) firstRestored = app.current;
            app.current = held;  // resolve the selection once, below
        }
    }
    // app.current is untouched until the selection resolves: a failed
    // restore must leave the live session exactly as it was (no blank
    // viewport, no dangling id, no false success).
    bool currentResolved = false;
    if (!workspace.currentAssetId.empty() && app.assets.count(workspace.currentAssetId) != 0) {
        app.current = workspace.currentAssetId;
        currentResolved = true;
    } else if (!firstRestored.empty() && app.assets.count(firstRestored) != 0) {
        app.current = firstRestored;
    } else if (restored == 0) {
        return Result<void>::fail("Workspace references '" + workspace.currentAssetId +
                                  "' but no asset source file still exists; session unchanged.");
    }
    LoadedAsset* la = app.currentAsset();
    if (!la) {
        return Result<void>::fail("Workspace references '" + workspace.currentAssetId +
                                  "' but no asset source file still exists; session unchanged.");
    }
    app.selectedBone = -1;
    app.selectedBones.clear();
    app.hiddenBones.clear();
    app.soloBone = -1;
    app.selectedVertex = -1;
    app.hoveredBone = -1;
    app.clearUndoHistory();
    app.noteWeightsChanged();
    // Resolve locked names against the current asset (ids are not stable).
    app.lockedBones.clear();
    for (const auto& name : workspace.lockedBoneNames) {
        if (const Bone* bone = la->skeleton.findByName(name))
            app.lockedBones.insert(bone->id);
    }
    app.hiddenBones.clear();
    for (const auto& name : workspace.hiddenBoneNames) {
        if (const Bone* bone = la->skeleton.findByName(name))
            app.hiddenBones.insert(bone->id);
    }
    app.soloBone = -1;
    if (!workspace.soloBoneName.empty()) {
        if (const Bone* bone = la->skeleton.findByName(workspace.soloBoneName))
            app.soloBone = static_cast<int>(bone->id);
    }
    app.boneSelectionSets.clear();
    for (const auto& [setName, boneNames] : workspace.selectionSets) {
        std::set<std::string> names;
        for (const auto& name : boneNames) {
            if (const Bone* bone = la->skeleton.findByName(name))
                names.insert(bone->name);
        }
        if (!names.empty())
            app.boneSelectionSets[setName] = std::move(names);
    }
    app.brushRadius = workspace.brush.radius;
    app.brushStrength = workspace.brush.strength;
    app.camera.orthographic = workspace.viewport.orthographic;
    app.camera.fovY = workspace.viewport.fovY;
    app.camera.frameAabb(la->mesh.bounds);
    la->gpuDirty = true;  // guarantee a real upload for the restored selection
    char buf[256];
    if (currentResolved) {
        std::snprintf(buf, sizeof(buf), "Workspace loaded: %s (%zu asset%s reimported).",
                      la->id.c_str(), restored, restored == 1 ? "" : "s");
        app.setStatus(buf, "success");
    } else {
        std::snprintf(buf, sizeof(buf),
                      "Workspace asset '%s' not found; kept '%s' (%zu asset%s reimported).",
                      workspace.currentAssetId.c_str(), la->id.c_str(), restored,
                      restored == 1 ? "" : "s");
        app.setStatus(buf, "warning");
    }
    return Result<void>::ok();
}

} // namespace m2rig