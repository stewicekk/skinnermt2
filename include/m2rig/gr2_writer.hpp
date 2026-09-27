#pragma once
// Native GR2 (Granny 2.x Metin2 variant) writer — the inverse of
// Gr2DeepParser. Serializes the canonical Mesh + Skeleton + Materials +
// AnimFrames into the little-endian GR2 binary container:
//   - 32-byte header (magic 29DE6CC0, version, totalSize, CRC32,
//     sectionCount, rootSectionOffset/Size, reserved)
//   - 16-byte section table entries (type, offset, size, dataOffset)
//   - section payloads (Skeleton / Mesh / MeshBinding / Material / Animation)
//
// Format notes (derived from the reverse-engineered parser, never guessed):
// - All scalars little-endian; Mat4 is 16 row-major floats m[row][col],
//   matching the parser's readMatrix exactly (writer is the exact inverse).
// - Strings are uint32 length + raw bytes (no NUL), parser readString.
// - Indices are uint16 (parser readUInt16), so the writer refuses meshes
//   with more than 65535 vertices instead of truncating silently.
// - Bone local transforms are written as Mat4::compose(pos, euler, scale);
//   the parser decomposes position from m[3][0..2] and euler via
//   eulerXyzFromRotation, which is the exact inverse for the rigid
//   (scale=1) case used by animation frames.
// - Per-vertex bone weights live in the MeshBinding section (type 3), the
//   parser's documented home for skinning data. The parser currently
//   flags the section (hasWeights) without expanding it, so the bytes are
//   written for external tools and round-trip-verified structurally.
#include <cstdint>
#include <string>
#include <vector>

#include "m2rig/result.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"

namespace m2rig {

class Gr2Writer {
public:
    struct Options {
        bool writeSkeleton = true;    // Skeleton section (bones + hierarchy)
        bool writeMesh = true;        // Mesh section (verts/normals/uvs/indices)
        bool writeMeshBinding = true; // MeshBinding section (per-vertex weights)
        bool writeMaterials = true;  // Material section
        bool writeAnimation = true;   // Animation section (from animFrames)
    };

    // Serialize mesh + skeleton + materials + animFrames to a GR2 buffer.
    // Fails honestly (never partial output): empty mesh/skeleton, >65535
    // vertices (uint16 index overflow) or non-finite floats are explicit Err.
    static Result<std::vector<uint8_t>> serialize(
        const Mesh& mesh,
        const Skeleton& skeleton,
        const std::vector<SmdFrame>& animFrames,
        const Options& options = {});

    // Serialize and write the buffer to `path` (binary, truncated).
    static ResultVoid writeToFile(
        const std::string& path,
        const Mesh& mesh,
        const Skeleton& skeleton,
        const std::vector<SmdFrame>& animFrames,
        const Options& options = {});
};

}  // namespace m2rig
