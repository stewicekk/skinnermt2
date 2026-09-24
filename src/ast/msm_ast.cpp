#include "m2rig/ast/msm_ast.hpp"
#include "m2rig/diagnostics.hpp"
#include "m2rig/math.hpp"
#include "m2rig/result.hpp"
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <limits>
#include <stack>
#include <functional>
#include <unordered_set>

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
// NOTE: does NOT trim — leading whitespace carries the nesting level and
// is measured by the caller before content is trimmed.
static std::string stripComment(std::string line) {
    size_t pos = line.find("//");
    if (pos != std::string::npos) {
        line = line.substr(0, pos);
    }
    return line;
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

    // Indent stack: (level, node). Root sits below every real level so the
    // first group always nests under it. Pure brace lines carry no info in
    // this indent-based dialect and are skipped (documented in the header).
    struct FlatItem {
        int level = 0;
        MsmNode node;
    };
    std::vector<FlatItem> flat;

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::string& rawLine = lines[li];
        std::string code = stripComment(rawLine);
        code = stripBlockComments(code);

        if (code.empty()) continue;

        std::size_t indent = 0;
        for (std::size_t i = 0; i < code.size() && std::isspace(code[i]); ++i) ++indent;

        std::string content = trim(code);
        if (content.empty()) continue;
        if (content == "{" || content == "}") continue;
        const int level = static_cast<int>(indent / 2) - 1;

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

        flat.push_back({level, std::move(node)});
    }

    // Assemble the tree recursively (move-only; no pointers into vectors
    // are ever held across a mutation, so reallocation is harmless).
    std::function<std::size_t(std::size_t, int, MsmNode&)> build =
        [&](std::size_t pos, int parentLevel, MsmNode& parent) -> std::size_t {
        while (pos < flat.size() && flat[pos].level > parentLevel) {
            MsmNode node = std::move(flat[pos].node);
            const int nodeLevel = flat[pos].level;
            ++pos;
            pos = build(pos, nodeLevel, node);
            parent.children.push_back(std::move(node));
        }
        return pos;
    };
    build(0, -100, doc.root);

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

std::string buildMsmExport(const Mesh& mesh, const Skeleton& skeleton,
                           const std::string& assetId) {    std::ostringstream out;
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

namespace {

int intAttr(const MsmNode& node, const std::string& key, int fallback) {
    auto it = node.attributes.find(key);
    if (it == node.attributes.end()) return fallback;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

std::string strAttr(const MsmNode& node, const std::string& key) {
    auto it = node.attributes.find(key);
    return it != node.attributes.end() ? it->second : std::string{};
}

// Top-level groups (ShapeIndex/Model/SourceSkin) live directly under the
// ShapeData root. A recursive search would wrongly match the same-named
// reference nodes nested inside Shape entries, so scope to direct children
// (falling back to the document root itself).
const MsmNode* findTopGroup(const MsmNode& root, const std::string& name) {
    const MsmNode* scope = &root;
    for (const auto& c : root.children) {
        if (c.name.compare(0, 9, "ShapeData") == 0) {
            scope = &c;
            break;
        }
    }
    for (const auto& c : scope->children) {
        if (c.name == name) return &c;
    }
    if (scope != &root) {
        for (const auto& c : root.children) {
            if (c.name == name) return &c;
        }
    }
    return nullptr;
}

}  // namespace

void validateMsmDoc(const MsmDocument& doc, const std::string& assetName,
                    ValidationReport& report) {
    const std::string asset = assetName.empty() ? doc.sourcePath : assetName;
    const MsmNode* index = findTopGroup(doc.root, "ShapeIndex");
    if (!index) {
        report.add("MSM_NO_INDEX", ValidationCategory::Mesh, Severity::Error,
                   "MSM has no ShapeIndex group.", asset, "", true);
        return;
    }
    const int declared = [&] {
        const MsmNode* sc = findChildByName(*index, "ShapeCount");
        return sc ? intAttr(*sc, "value", -1) : -1;
    }();
    std::size_t shapes = 0;
    for (const auto& c : index->children) {
        // Shape groups are named Shape<N>; ShapeCount itself also matches
        // the prefix and must be skipped.
        if (c.name == "ShapeCount") continue;
        if (c.name.compare(0, 5, "Shape") == 0) {
            ++shapes;
            const MsmNode* model = findChildByName(c, "Model");
            const MsmNode* skin = findChildByName(c, "SourceSkin");
            if (!model || strAttr(*model, "value").empty()) {
                report.add("MSM_SHAPE_REF", ValidationCategory::Mesh, Severity::Error,
                           "Shape '" + c.name + "' has an empty Model reference.", asset,
                           c.name, true);
            }
            if (!skin || strAttr(*skin, "value").empty()) {
                report.add("MSM_SHAPE_REF", ValidationCategory::Mesh, Severity::Error,
                           "Shape '" + c.name + "' has an empty SourceSkin reference.", asset,
                           c.name, true);
            }
        }
    }
    if (declared >= 0 && static_cast<std::size_t>(declared) != shapes) {
        report.add("MSM_SHAPE_COUNT", ValidationCategory::Mesh, Severity::Error,
                   "ShapeCount " + std::to_string(declared) + " does not match " +
                       std::to_string(shapes) + " Shape groups.",
                   asset, "", true);
    }
    const MsmNode* skin = findTopGroup(doc.root, "SourceSkin");
    if (!skin) {
        report.add("MSM_NO_SKIN", ValidationCategory::Mesh, Severity::Error,
                   "MSM has no SourceSkin group.", asset, "", true);
        return;
    }
    const int verts = [&] {
        const MsmNode* vc = findChildByName(*skin, "VertexCount");
        return vc ? intAttr(*vc, "value", -1) : -1;
    }();
    if (verts <= 0) {
        report.add("MSM_VERTEX_COUNT", ValidationCategory::Mesh, Severity::Error,
                   "SourceSkin VertexCount is missing or zero.", asset, "", true);
        return;
    }
    std::size_t listed = 0;
    for (const auto& c : skin->children) {
        if (c.name == "Vertex") ++listed;
    }
    if (static_cast<std::size_t>(verts) != listed) {
        report.add("MSM_VERTEX_COUNT", ValidationCategory::Mesh, Severity::Error,
                   "VertexCount " + std::to_string(verts) + " does not match " +
                       std::to_string(listed) + " Vertex lines.",
                   asset, "", true);
    }
    report.add("MSM_STATS", ValidationCategory::Mesh, Severity::Info,
               "MSM: " + std::to_string(shapes) + " shapes, " + std::to_string(listed) +
                   " skin vertices.",
               asset, "", false);
}

namespace {

// Strict full-consumption number parsing for SourceSkin lines. strtod/strtol
// accept "nan"/"inf"/hex prefixes, so callers still check finiteness and the
// tail pointer guarantees no trailing garbage (unlike operator>> loops,
// which cannot tell a clean end from a malformed tail).
bool parseStrictLong(const std::string& s, long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0') return false;
    out = v;
    return true;
}

bool parseStrictDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0') return false;
    out = v;
    return true;
}

std::string unquoteMsmRef(const std::string& s) {
    const std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        return t.substr(1, t.size() - 2);
    return t;
}

}  // namespace

Result<SmdDocument> msmToSmd(const MsmDocument& msm, const Skeleton& skeleton,
                             const std::string& asset) {
    const std::string ctx = asset.empty() ? msm.sourcePath : asset;
    if (skeleton.bones.empty()) {
        return Result<SmdDocument>::fail(
            "Cannot convert MSM to SMD without a skeleton: boneless SMD documents are "
            "invalid (the SMD parser rejects an empty 'nodes' section and the writer "
            "refuses skeleton-less output).",
            "MSM", ctx, "msmToSmd");
    }
    ValidationReport report;
    validateMsmDoc(msm, ctx, report);
    if (report.exportBlocked()) {
        std::string detail;
        for (const auto& it : report.items()) {
            if (it.severity == Severity::Error || it.severity == Severity::Fatal) {
                if (!detail.empty()) detail += "; ";
                detail += it.id + ": " + it.message;
            }
        }
        return Result<SmdDocument>::fail("Invalid MSM '" + ctx + "': " + detail, "MSM", ctx,
                                         "msmToSmd");
    }

    // Materials from Shape Model refs (first-appearance order, like
    // SmdModel::materials). The gate above already rejected missing/empty
    // refs; the unquoted-empty and material-name checks below close the
    // '""' hole the counter cannot see and keep output re-parseable.
    std::vector<std::string> materials;
    if (const MsmNode* index = findTopGroup(msm.root, "ShapeIndex")) {
        for (const auto& c : index->children) {
            if (c.name == "ShapeCount") continue;
            if (c.name.compare(0, 5, "Shape") != 0) continue;
            const MsmNode* model = findChildByName(c, "Model");
            if (!model) {
                return Result<SmdDocument>::fail("Shape '" + c.name + "' has no Model reference.",
                                                 "MSM", ctx, "msmToSmd");
            }
            const std::string mat = unquoteMsmRef(strAttr(*model, "value"));
            if (mat.empty()) {
                return Result<SmdDocument>::fail("Shape '" + c.name +
                                                     "' has an empty Model material reference.",
                                                 "MSM", ctx, "msmToSmd");
            }
            if (!isValidMaterialName(mat)) {
                return Result<SmdDocument>::fail("Shape '" + c.name +
                                                     "' has an invalid material name '" + mat +
                                                     "'.",
                                                 "MSM", ctx, "msmToSmd");
            }
            if (std::find(materials.begin(), materials.end(), mat) == materials.end())
                materials.push_back(mat);
        }
    }

    // Strict SourceSkin gate: every Vertex line must be a vertex id followed
    // by bone/weight pairs over the caller skeleton. Weights have no SMD
    // carrier without positions (which MSM never stores), so validated pairs
    // are intentionally not emitted — but malformed skin still fails here
    // instead of passing silently into a downstream re-bind.
    if (const MsmNode* skin = findTopGroup(msm.root, "SourceSkin")) {
        std::unordered_set<long> seenIds;
        for (const auto& c : skin->children) {
            if (c.name != "Vertex") continue;
            const std::string value = strAttr(c, "value");
            std::istringstream in(value);
            std::vector<std::string> toks;
            std::string tok;
            while (in >> tok) toks.push_back(tok);
            long vid = -1;
            if (toks.empty() || !parseStrictLong(toks[0], vid) || vid < 0) {
                return Result<SmdDocument>::fail(
                    "MSM SourceSkin has a malformed Vertex id in line: '" + value + "'.", "MSM",
                    ctx, "msmToSmd");
            }
            if (!seenIds.insert(vid).second) {
                return Result<SmdDocument>::fail(
                    "Duplicate MSM SourceSkin vertex id " + std::to_string(vid) + ".", "MSM", ctx,
                    "msmToSmd");
            }
            if ((toks.size() - 1) % 2 != 0) {
                return Result<SmdDocument>::fail("MSM SourceSkin Vertex line for vertex " +
                                                     std::to_string(vid) +
                                                     " must list bone/weight pairs.",
                                                 "MSM", ctx, "msmToSmd");
            }
            for (std::size_t i = 1; i < toks.size(); i += 2) {
                long bone = -1;
                double w = 0.0;
                if (!parseStrictLong(toks[i], bone)) {
                    return Result<SmdDocument>::fail(
                        "MSM SourceSkin vertex " + std::to_string(vid) +
                            " has a malformed bone id: '" + toks[i] + "'.",
                        "MSM", ctx, "msmToSmd");
                }
                if (bone < 0 ||
                    static_cast<std::uint64_t>(bone) >= skeleton.bones.size()) {
                    return Result<SmdDocument>::fail(
                        "MSM SourceSkin vertex " + std::to_string(vid) +
                            " references out-of-range bone " + std::to_string(bone) + " (" +
                            std::to_string(skeleton.bones.size()) + " bones in caller skeleton).",
                        "MSM", ctx, "msmToSmd");
                }
                if (!parseStrictDouble(toks[i + 1], w) || !std::isfinite(w)) {
                    return Result<SmdDocument>::fail("MSM SourceSkin vertex " +
                                                         std::to_string(vid) +
                                                         " has a non-finite weight: '" +
                                                         toks[i + 1] + "'.",
                                                     "MSM", ctx, "msmToSmd");
                }
                if (w < 0.0) {
                    return Result<SmdDocument>::fail("MSM SourceSkin vertex " +
                                                         std::to_string(vid) +
                                                         " has a negative weight.",
                                                     "MSM", ctx, "msmToSmd");
                }
                if (w > static_cast<double>(std::numeric_limits<float>::max())) {
                    return Result<SmdDocument>::fail("MSM SourceSkin vertex " +
                                                         std::to_string(vid) +
                                                         " has an out-of-range weight.",
                                                     "MSM", ctx, "msmToSmd");
                }
            }
        }
    }

    // Bones + bind frame verbatim from the caller skeleton (no invented
    // transforms: MSM Bone lines carry ids/names/parents only). Triangles
    // stay empty: MSM stores no positions, normals, UVs or faces, so there
    // is nothing honest to emit.
    SmdDocument doc;
    doc.bones.reserve(skeleton.bones.size());
    for (const auto& b : skeleton.bones) {
        SmdBone sb;
        sb.id = b.id;
        sb.name = b.name;
        sb.parentId = b.parentId;
        sb.bindPosition = b.localPosition;
        sb.bindRotation = b.localRotationEuler;
        doc.bones.push_back(std::move(sb));
    }
    SmdFrame bind;
    bind.time = 0;
    bind.poses.reserve(skeleton.bones.size());
    for (const auto& b : skeleton.bones) {
        SmdBonePose p;
        p.boneId = b.id;
        p.position = b.localPosition;
        p.rotation = b.localRotationEuler;
        bind.poses.push_back(p);
    }
    doc.frames.push_back(std::move(bind));
    doc.materials = std::move(materials);
    return Result<SmdDocument>::ok(std::move(doc));
}

}  // namespace m2rig