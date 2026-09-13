#pragma once
// Metin2 SMD (Valve ASCII skeletal) import/export (spec section 21).
// Reader handles nodes/skeleton(tri-frame)/triangles with materials, UVs,
// normals, bone links; multi-frame skeleton blocks are preserved for the
// timeline. Writer emits stable 6-decimal output grouped by material.
// Round-trip: SMD -> canonical -> SMD -> canonical with an explicit diff
// report. Nothing here "looks valid": unparsable input is an explicit Err.
#include <cstdint>
#include <string>
#include <vector>

#include "m2rig/math.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/validation.hpp"

namespace m2rig {

struct SmdLink {
    std::uint32_t bone = 0;  // file bone id (mapped to skeleton index on convert)
    float weight = 0.0f;
};

struct SmdVertex {
    std::uint32_t parentBone = 0;
    Vec3 position{0, 0, 0};
    Vec3 normal{0, 0, 1};
    Vec2 uv{0, 0};
    std::vector<SmdLink> links;  // empty == rigid bind to parentBone
};

struct SmdTriangle {
    std::string material;
    SmdVertex v[3];
};

struct SmdBone {
    std::uint32_t id = 0;
    std::string name;
    std::int32_t parentId = kNoParent;
    Vec3 bindPosition{0, 0, 0};
    Vec3 bindRotation{0, 0, 0};  // radians, XYZ
};

struct SmdBonePose {
    std::uint32_t boneId = 0;
    Vec3 position{0, 0, 0};
    Vec3 rotation{0, 0, 0};
};

struct SmdFrame {
    int time = 0;
    std::vector<SmdBonePose> poses;
};

struct SmdModel {
    std::vector<SmdBone> bones;
    std::vector<SmdFrame> frames;  // frame[0] is the bind pose block
    std::vector<SmdTriangle> triangles;
    std::vector<std::string> materials;  // first-appearance order
};

Result<SmdModel> parseSmd(const std::string& text, const std::string& asset = "<smd>");

struct SmdWriteStats {
    std::size_t verticesWritten = 0;
    std::size_t trianglesWritten = 0;
    std::size_t clampedVertices = 0;  // reduced to 4 influences
    double droppedMass = 0.0;
};

struct SmdWriteResult {
    std::string text;
    SmdWriteStats stats;
};

// The writer enforces <=4 influences exactly like the exporter contract:
// strongest kept, remainder accumulated into stats (never silent).
Result<SmdWriteResult> writeSmd(const Mesh& mesh, const Skeleton& skeleton,
                                const std::vector<SmdFrame>& frames = {});

struct ConvertedSmd {
    Mesh mesh;
    Skeleton skeleton;
    std::vector<SmdFrame> frames;
};

// File bone ids need not be contiguous: conversion maps them to dense
// skeleton indices, preserving file order sorted by id. Exact bone names
// and hierarchy are preserved; bind pose comes from frame 0.
Result<ConvertedSmd> smdToAsset(const SmdModel& model, const std::string& assetName);

// Poses the skeleton from a frame (for skeleton-only timeline preview).
// Frame 0 restores the bind pose. Mesh deformation is wave 6.
ResultVoid poseSkeletonFromFrame(Skeleton& skeleton, const std::vector<SmdFrame>& frames,
                                 std::size_t frameIndex);

struct SmdDiff {
    std::string what;
    std::string detail;
};

struct SmdRoundTripReport {
    std::vector<SmdDiff> diffs;
    bool identical() const { return diffs.empty(); }
    std::string summary() const;
};

// Full pipeline check used by tests and the CLI: parse -> asset -> write ->
// parse -> asset, then compare bones/transforms/topology/weights/materials.
Result<SmdRoundTripReport> smdRoundTrip(const std::string& text, const std::string& asset);

// Size-checked file IO (untrusted files, spec section 87).
Result<std::string> readTextFile(const std::string& path, const std::string& asset = {});
ResultVoid writeTextFile(const std::string& path, const std::string& text,
                         const std::string& asset = {});

}  // namespace m2rig
