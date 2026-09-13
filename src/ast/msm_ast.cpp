#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/math.hpp"
#include "m2rig/result.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stack>
#include <functional>

namespace m2rig {

// Helper: trim whitespace from both ends of a string.
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Helper: check if a line is a comment.
[[maybe_unused]] static bool isCommentLine(const std::string& line) {
    std::string trimmed = trim(line);
    return trimmed.empty() || trimmed[0] == '/' || (trimmed.size() > 1 && trimmed[0] == '*' && trimmed[1] == '/');
}

// Helper: strip a single-line comment // from the end of a line.
static std::string stripComment(std::string line) {
    size_t pos = line.find("//");
    if (pos != std::string::npos) {
        line = line.substr(0, pos);
    }
    return trim(line);
}

// Helper: strip a block comment /* ... */ from a string.
static std::string stripBlockComments(const std::string& text) {
    std::string result;
    size_t i = 0;
    while (i < text.size()) {
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
            size_t end = text.find("*/", i + 2);
            if (end != std::string::npos) {
                i = end + 2;
            } else {
                i = text.size();
            }
        } else if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            size_t eol = text.find("\n", i);
            if (eol != std::string::npos) {
                result += text.substr(i, eol - i + 1);
                i = eol + 1;
            } else {
                i = text.size();
            }
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
}

// Parse MSM text into an AST.
bool parseMsm(const std::string& text, MsmDocument& doc) {
    doc.sourcePath = "<msm>";
    doc.root = MsmNode{MsmSectionType::ShapeData, "ShapeData", 0};

    std::istringstream streams(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(streams, line)) {
        lines.push_back(line);
    }

    MsmNode* current = &doc.root;
    std::stack<MsmNode*> parentStack;
    parentStack.push(current);

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::string& rawLine = lines[li];
        std::string code = stripComment(rawLine);
        code = stripBlockComments(code);

        if (code.empty()) continue;

        std::size_t indent = 0;
        for (std::size_t i = 0; i < code.size() && std::isspace(code[i]); ++i) ++indent;

        std::string content = trim(code);
        const int level = static_cast<int>(indent / 2) - 1;

        while (level <= static_cast<int>(parentStack.size()) - 1 && parentStack.size() > 1) {
            parentStack.pop();
        }

        MsmNode node;
        node.lineNumber = static_cast<std::uint32_t>(li + 1);

        size_t spacePos = content.find(' ');
        if (spacePos != std::string::npos) {
            std::string head = trim(content.substr(0, spacePos));
            std::string rest = trim(content.substr(spacePos + 1));
            if (head == "Group" && !rest.empty()) {
                // Real MSM syntax: "Group <Name>" / "Group <Name> {".
                size_t end = rest.find_first_of(" \t{");
                node.name = rest.substr(0, end);
                node.attributes["kind"] = "Group";
            } else {
                node.name = head;
                size_t eqPos = rest.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = trim(rest.substr(0, eqPos));
                    std::string val = trim(rest.substr(eqPos + 1));
                    node.attributes[key] = val;
                } else if (!rest.empty()) {
                    node.attributes["value"] = rest;
                }
            }
        } else {
            node.name = content;
        }

        node.type = MsmSectionType::UnknownBlock;

        if (node.name == "ShapeData") node.type = MsmSectionType::ShapeData;
        else if (node.name == "ShapeIndex") node.type = MsmSectionType::ShapeIndex;
        else if (node.name == "Model") node.type = MsmSectionType::Model;
        else if (node.name == "SourceSkin") node.type = MsmSectionType::SourceSkin;

        parentStack.top()->children.push_back(std::move(node));
        current = &parentStack.top()->children.back();
    }

    return true;
}

// Stringify the AST back to MSM text.
std::string msmStringify(const MsmDocument& doc) {
    std::ostringstream out;

    std::function<void(const MsmNode&, int)> stringifyNode = [&](const MsmNode& node, int indentLevel) {
        std::string indent(indentLevel * 2, ' ');
        const auto kindIt = node.attributes.find("kind");
        if (kindIt != node.attributes.end() && kindIt->second == "Group") {
            out << indent << "Group " << node.name << "\n";
        } else {
            out << indent << node.name;
            for (const auto& [key, val] : node.attributes) {
                if (key == "kind" || key == "value") continue;
                out << " " << key << " = " << val;
            }
            const auto vit = node.attributes.find("value");
            if (vit != node.attributes.end()) out << " " << vit->second;
            out << "\n";
        }

        for (const auto& child : node.children) {
            stringifyNode(child, indentLevel + 1);
        }
    };

    stringifyNode(doc.root, 0);
    return out.str();
}

// Find a child node by name recursively.
const MsmNode* findChildByName(const MsmNode& node, const std::string& name) {
    if (node.name == name) return &node;
    for (const auto& child : node.children) {
        const MsmNode* found = findChildByName(child, name);
        if (found) return found;
    }
    return nullptr;
}

// Extract SourceSkin data (simplified interface).
SourceSkinData extractSourceSkin(const MsmNode& node) {
    SourceSkinData data;
    auto it = node.attributes.find("boneCount");
    if (it != node.attributes.end()) {
        data.boneCount = static_cast<std::uint32_t>(std::stoul(it->second));
    }
    return data;
}

// Read an MSM file from disk.
Result<MsmDocument> readMsmFile(const std::filesystem::path& path) {
    std::string text;
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        return Result<MsmDocument>::fail("Cannot open MSM file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    file.close();

    MsmDocument doc;
    if (parseMsm(text, doc)) {
        doc.sourcePath = path.string();
        return Result<MsmDocument>::ok(std::move(doc));
    }
    return Result<MsmDocument>::fail("Failed to parse MSM file: " + path.string());
}

// Write an MSM document back to text.
bool writeMsmDocument(const MsmDocument& doc, std::string& output) {
    output = msmStringify(doc);
    return true;
}

// Convenience: write in one step.
bool writeMsmFile(const std::filesystem::path& path, const MsmDocument& doc) {
    std::string output;
    if (writeMsmDocument(doc, output)) {
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;
        file << output;
        file.close();
        return true;
    }
    return false;
}

std::string buildMsmExport(const Mesh& mesh, const Skeleton& skeleton,
                           const std::string& assetId) {
    std::ostringstream out;
    out << "// Metin2 Rigging Studio MSM export\n";
    out << "// Source asset: " << assetId << "\n";
    out << "Group ShapeData" << assetId << "\n";
    out << "{\n";
    out << "  Group ShapeIndex\n  {\n";
    out << "    ShapeCount " << mesh.subMeshes.size() << "\n";
    for (std::size_t i = 0; i < mesh.subMeshes.size(); ++i) {
        const auto& sm = mesh.subMeshes[i];
        const std::string mat = sm.materialIndex < mesh.materials.size()
                                    ? mesh.materials[sm.materialIndex].name
                                    : "default.dds";
        out << "    Group Shape" << i << "\n    {\n";
        out << "      Model \"" << mat << "\"\n";
        out << "      SourceSkin \"" << assetId << "_skin\"\n";
        out << "    }\n";
    }
    out << "  }\n";
    out << "  Group Model\n  {\n";
    for (const auto& b : skeleton.bones)
        out << "    Bone " << b.id << " \"" << b.name << "\" " << b.parentId << "\n";
    out << "  }\n";
    out << "  Group SourceSkin\n  {\n";
    out << "    VertexCount " << mesh.vertices.size() << "\n";
    for (std::size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
        out << "    Vertex " << vi;
        for (const auto& inf : mesh.vertices[vi].influences)
            out << " " << inf.bone << " " << inf.weight;
        out << "\n";
    }
    out << "  }\n}\n";
    return out.str();
}

} // namespace m2rig