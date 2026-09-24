#pragma once
// AST parser for Metin2 .msm shape files (spec section 22).
// Preserves comments, arbitrary blocks, and exact topology.
// The current generator is lossy (regenerates, drops unknown fields) —
// this native parser is AST-based and tolerant of comment/order/unknown-field variations.

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <filesystem>

#include "m2rig/result.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/validation.hpp"

namespace m2rig {

// MSM section types (based on Granny3D / Metin2 format).
enum class MsmSectionType {
    Unknown,
    ShapeData,      // root group
    ShapeIndex,     // shape index group
    Model,          // model group
    SourceSkin,     // skin weight group
    UnknownBlock    // arbitrary block (preserved as-is)
};

// A parsed node in the MSM AST.
struct MsmNode {
    MsmSectionType type = MsmSectionType::Unknown;
    std::string name;          // section name (e.g. "SourceSkin")
    std::uint32_t lineNumber = 0;  // source file line for error reporting
    // Key-value pairs parsed from the section header/body.
    // Values are strings; type-specific parsers convert as needed.
    std::map<std::string, std::string> attributes;
    // Child nodes (nested groups).
    std::vector<MsmNode> children;
    // Raw text of this section (for preserving comments/unknown fields).
    std::string rawText;
};

// Parsed MSM document root.
struct MsmDocument {
    MsmNode root;
    std::string sourcePath;
};

// Tolerant parser: handles comments (// and /* */), reordered fields,
// unknown section types, and missing optional fields. Nesting follows
// indentation (2 spaces per level); standalone "{" / "}" lines carry no
// information in this dialect and are skipped. Returns false on
// unrecoverable syntax errors (not on unknown blocks).
bool parseMsm(const std::string& text, MsmDocument& doc);

// Stringify the AST back to MSM text (preserving comments that were in rawText).
// Comments from child nodes are merged into the output.
std::string msmStringify(const MsmDocument& doc);

// Find a child node by name recursively.
const MsmNode* findChildByName(const MsmNode& node, const std::string& name);

// Get weighted influences from a SourceSkin node.
// Returns per-vertex weight data mapped to bone indices.
struct SourceSkinData {
    std::uint32_t boneCount = 0;
};

// Parse an MSM file from disk.
Result<MsmDocument> readMsmFile(const std::filesystem::path& path);

// Write an MSM document back to text.
bool writeMsmDocument(const MsmDocument& doc, std::string& output);

// Builds MSM export text from a canonical asset (shared by the GUI
// exporter and the headless CLI so both emit byte-identical output).
std::string buildMsmExport(const Mesh& mesh, const Skeleton& skeleton,
                           const std::string& assetId);

// Semantic validation of a parsed document against the ShapeData contract:
// ShapeCount matches Shape children, every Shape has non-empty Model and
// SourceSkin references, VertexCount matches Vertex children. Findings go
// to the report (errors block export-style use, like the mesh gate).
void validateMsmDoc(const MsmDocument& doc, const std::string& assetName,
                    ValidationReport& report);

// The SMD document type produced by msmToSmd. Alias (not a new abstraction):
// SmdModel is the existing parsed-SMD document (nodes/skeleton/triangles/
// materials) that smdToAsset and the writer already consume.
using SmdDocument = SmdModel;

// MSM -> SMD lowering (geometry shell).
//
// Strict-validates the MSM through validateMsmDoc first: any Error/Fatal
// (MSM_SHAPE_COUNT / MSM_SHAPE_REF / MSM_VERTEX_COUNT / MSM_NO_INDEX /
// MSM_NO_SKIN) fails explicitly with the rule ids in the message. SourceSkin
// vertex lines are then parsed strictly (vertex id + bone/weight pairs must
// be well-formed; bone ids must index the caller skeleton; weights must be
// finite and non-negative; duplicate vertex ids rejected) — export-grade
// gate, mirroring the smdToAsset unknown-bone and parseSmd duplicate-id
// hard errors.
//
// Output carries everything MSM can honestly supply:
//   bones  <- caller skeleton verbatim (ids/names/parents/binds),
//   frames <- single time-0 bind-pose block (required by the SMD parser),
//   materials <- MSM Shape Model refs in first-appearance order,
//   triangles <- EMPTY (documented, not an oversight).
// Triangles are empty because the MSM dialect carries no vertex positions,
// normals, UVs or faces — only per-vertex bone/weight pairs (see
// buildMsmExport and tests/data/sample.msm). Emitting zero-filled positions
// or triple-grouped topology would be placeholder production functionality,
// so it is explicitly refused; the shell re-parses through parseSmd and
// converts through smdToAsset. MSM bone ids are dense skeleton indices (as
// written by buildMsmExport); out-of-range references fail explicitly.
// Validated SourceSkin weights have no SMD carrier without positions and
// are intentionally not emitted.
//
// Why the caller skeleton (boneless SMD is invalid, verified in code):
// parseSmd rejects an empty 'nodes' section (smd.cpp), writeSmd refuses
// skeleton-less output (smd.cpp), buildSkeleton rejects empty defs and
// validateSkeleton reports SKEL_EMPTY Fatal (skeleton.cpp), and empty
// influences fail downstream as WEIGHTS_UNWEIGHTED Error (skin_weights.cpp).
// No bones are invented: MSM Bone lines carry no transforms, so binds come
// from the caller (pass the export-time or layout-compatible skeleton).
//
// CLI contract (msm2smd verb wiring track): supply the skeleton the MSM was
// exported from; on success check triangles.empty() and report "shell only,
// no geometry" — geometry recovery needs the source asset, and autorig
// cannot fill it from MSM (autoRigMesh binds by segment distance from vertex
// positions, which MSM does not carry). The shell is the strict gate plus
// skeleton/materials for inspection and re-binding workflows.
Result<SmdDocument> msmToSmd(const MsmDocument& msm, const Skeleton& skeleton,
                             const std::string& asset);

} // namespace m2rig