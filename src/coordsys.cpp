// Coordinate system conversion profiles implementation.
#include "m2rig/coordsys.hpp"
#include "m2rig/mesh.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/smd.hpp"
#include "m2rig/math.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace m2rig {

const char* coordSysName(CoordSys cs) {
    switch (cs) {
        case CoordSys::Canonical: return "Canonical (Y-up, Z-forward)";
        case CoordSys::ZUp_YForward: return "Z-up, Y-forward (Blender/FBX/Granny)";
        case CoordSys::YUp_ZForward: return "Y-up, Z-forward";
        case CoordSys::ZUp_YBackward: return "Z-up, Y-backward";
        case CoordSys::XUp: return "X-up";
    }
    return "Unknown";
}

// Canonical: Y-up, Z-forward (forward = -Z in view space)
// Row-vector convention: v' = v * M
//
// ZUp_YForward (Blender/FBX/Granny): X=right, Y=forward, Z=up
//   -> Canonical: X=right, Y=up, Z=-forward
//   (x, y, z) -> (x, z, -y)
//   Matrix: +90 deg around X
Mat4 convertToCanonical(CoordSys src) {
    switch (src) {
        case CoordSys::Canonical:
            return Mat4::identity();
        case CoordSys::ZUp_YForward: {
            // (x, y, z) -> (x, z, -y). Single source of truth: reuse the
            // verified Mat4::convertZUpToYUp() — never a second hand-written
            // copy (HARD RULE: one canonical conversion, no stacked hacks).
            return Mat4::convertZUpToYUp();
        }
        case CoordSys::YUp_ZForward: {
            // Already Y-up but Z-forward vs Z=-forward
            // Canonical has forward = -Z, this has forward = +Z
            // (x, y, z) -> (x, y, -z)
            Mat4 r = Mat4::identity();
            r.m[2][2] = -1.0f;
            return r;
        }
        case CoordSys::ZUp_YBackward: {
            // X=right, Y=backward, Z=up (left-handed) -> canonical.
            // Forward (-Y) -> -Z, up (Z) -> Y: (x, y, z) -> (x, z, y).
            // Left-to-right-handed needs a mirror (det -1), as encoded.
            Mat4 r = Mat4::identity();
            r.m[1][1] = 0.0f;
            r.m[1][2] = 1.0f;
            r.m[2][1] = 1.0f;
            r.m[2][2] = 0.0f;
            return r;
        }
        case CoordSys::XUp: {
            // No agreed X-up mapping: callers must use tryConvertToCanonical
            // (explicit failure), never this unchecked path.
            return Mat4::identity();
        }
    }
    return Mat4::identity();
}

bool isConversionImplemented(CoordSys src) { return src != CoordSys::XUp; }

Result<Mat4> tryConvertToCanonical(CoordSys src) {
    if (!isConversionImplemented(src))
        return Result<Mat4>::fail(
            std::string("No implemented conversion from ") + coordSysName(src) +
                " to canonical Y-up; refusing a silent identity pass-through.",
            "FORMAT", "", "coordsys.convert");
    return Result<Mat4>::ok(convertToCanonical(src));
}

std::optional<CoordSys> coordSysFromUpFront(AxisDir up, AxisDir front) {
    using A = AxisDir;
    if (up == A::PosZ && front == A::PosY) return CoordSys::ZUp_YForward;
    if (up == A::PosZ && front == A::NegY) return CoordSys::ZUp_YBackward;
    if (up == A::PosY && front == A::PosZ) return CoordSys::YUp_ZForward;
    if (up == A::PosY && front == A::NegZ) return CoordSys::Canonical;
    return std::nullopt;
}

Mat4 convertFromCanonical(CoordSys dst) {
    // Inverse of convertToCanonical
    return convertToCanonical(dst).inverseRigid();
}

std::optional<CoordSys> detectGr2CoordSys(const std::filesystem::path& path) {
    // GR2/Granny doesn't store coordinate system in an easily readable way.
    // Granny3D is historically Z-up, Y-forward.
    (void)path;
    return std::nullopt;
}

namespace {

ConversionProfile gFbxProfile{
    "FBX",
    CoordSys::ZUp_YForward,
    true,
    true, true, true,
    "blender"
};

ConversionProfile gGr2Profile{
    "GR2",
    CoordSys::ZUp_YForward,
    true,
    true, true, true,
    "granny2"
};

ConversionProfile gSmdProfile{
    "SMD",
    CoordSys::Canonical,  // SMD from Metin2 tools is already canonical
    false,
    false, false, false,
    "metin2"
};

ConversionProfile gNoesisProfile{
    "Noesis",
    CoordSys::ZUp_YForward,  // Noesis typically works in Z-up
    false,
    true, true, true,
    "noesis"
};

// Single resolution rule shared by all three apply-overloads (audit: was
// triplicated). Auto-detect wins when the profile allows it and detection
// produced a non-canonical answer; otherwise the profile's assumed source.
CoordSys resolveSource(const ConversionProfile& profile,
                       const std::optional<CoordSys>& detected) {
    if (profile.detectAutomatically && detected.has_value() &&
        detected.value() != CoordSys::Canonical)
        return detected.value();
    return profile.assumedSource;
}

}  // namespace

const ConversionProfile& fbxConversionProfile() { return gFbxProfile; }
const ConversionProfile& gr2ConversionProfile() { return gGr2Profile; }
const ConversionProfile& smdConversionProfile() { return gSmdProfile; }
const ConversionProfile& noesisConversionProfile() { return gNoesisProfile; }

ResultVoid applyConversionProfile(const ConversionProfile& profile,
                                  const std::optional<CoordSys>& detectedSource, Mesh& mesh) {
    if (!profile.convertMesh) return ResultVoid::ok();
    const CoordSys src = resolveSource(profile, detectedSource);
    if (src == CoordSys::Canonical) return ResultVoid::ok();
    auto conv = tryConvertToCanonical(src);
    if (!conv) return ResultVoid::fail(conv.error());
    const Mat4 M = conv.value();
    for (auto& v : mesh.vertices) {
        v.position = M.transformPoint(v.position);
        v.normal = M.transformVector(v.normal);
        if (v.hasTangent) {
            const Vec3 t = M.transformVector({v.tangent.x, v.tangent.y, v.tangent.z});
            v.tangent = {t.x, t.y, t.z, v.tangent.w};
        }
    }
    // Bounds follow the moved vertices. Normals/tangents are transformed, not
    // recomputed: recompute would silently change imported shading data.
    computeBounds(mesh);
    return ResultVoid::ok();
}

ResultVoid applyConversionProfile(const ConversionProfile& profile,
                                  const std::optional<CoordSys>& detectedSource,
                                  Skeleton& skeleton) {
    if (!profile.convertSkeleton) return ResultVoid::ok();
    const CoordSys src = resolveSource(profile, detectedSource);
    if (src == CoordSys::Canonical) return ResultVoid::ok();
    auto conv = tryConvertToCanonical(src);
    if (!conv) return ResultVoid::fail(conv.error());
    const Mat4 M = conv.value();
    const Mat4 Minv = M.inverseRigid();
    for (auto& b : skeleton.bones) {
        // Local position: transform as point
        b.localPosition = M.transformPoint(b.localPosition);
        // Local rotation: basis-change conjugation R' = M^-1 * R * M.
        // Direction matters and was wrong before (M*R*M^-1): the converted
        // rotation must satisfy (u*M)*R' = (u*R)*M for every vector u, i.e.
        // rotating then converting equals converting then rotating — only
        // M^-1*R*M does. The old direction is invisible for identity binds
        // (ninja et al., unchanged) but detaches mesh from skeleton under
        // posing/frame-playback for any bone with a real bind rotation.
        // Together with t' = t*M this makes the full local L' = M^-1*L*M,
        // so globals telescope to G' = M^-1*G*M and the skinning palette
        // stays a rigid rotation of the source palette.
        Mat4 localRot = Mat4::rotationEulerXyz(b.localRotationEuler);
        Mat4 convertedRot = Minv * localRot * M;
        b.localRotationEuler = convertedRot.eulerXyzFromRotation();
        // Local scale: for uniform scale, unchanged; for non-uniform, would need decomposition
        // We assume uniform scale for coordinate system conversion
    }
    // Rebuild global transforms and inverse binds
    if (auto r = rebuildSkeletonRuntime(skeleton); !r) return ResultVoid::fail(r.error());
    return ResultVoid::ok();
}

ResultVoid applyConversionProfile(const ConversionProfile& profile,
                                  const std::optional<CoordSys>& detectedSource,
                                  std::vector<SmdFrame>& frames) {
    if (!profile.convertAnimFrames) return ResultVoid::ok();
    const CoordSys src = resolveSource(profile, detectedSource);
    if (src == CoordSys::Canonical) return ResultVoid::ok();
    auto conv = tryConvertToCanonical(src);
    if (!conv) return ResultVoid::fail(conv.error());
    const Mat4 M = conv.value();
    const Mat4 Minv = M.inverseRigid();
    for (auto& frame : frames) {
        for (auto& pose : frame.poses) {
            pose.position = M.transformPoint(pose.position);
            // Same basis-change conjugation as skeleton locals (see above):
            // frame poses share the source space, so frame 0 still reproduces
            // the converted bind pose exactly.
            Mat4 localRot = Mat4::rotationEulerXyz(pose.rotation);
            Mat4 convertedRot = Minv * localRot * M;
            pose.rotation = convertedRot.eulerXyzFromRotation();
        }
    }
    return ResultVoid::ok();
}

namespace {

std::string lowerCopy(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

}  // namespace

bool OrientationReport::sane() const {
    for (const auto& f : findings)
        if (f.blocking) return false;
    return true;
}

std::string OrientationReport::verdictLine() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "ORIENTATION %s (%zu joints, %zu findings)",
                  sane() ? "SANE" : "CHECK", jointCount, findings.size());
    return buf;
}

std::string OrientationReport::toDisplayString() const {
    std::string out = verdictLine() + "\n";
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "mesh Y [%.2f, %.2f], skeleton Y [%.2f, %.2f]",
                      meshBounds.min.y, meshBounds.max.y, skeletonBounds.min.y,
                      skeletonBounds.max.y);
        out += buf;
        out += "\n";
    }
    if (hasHeadFeet) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "head Y %.2f, feet Y %.2f", headY, footY);
        out += buf;
        out += "\n";
    }
    if (maxRigidDistance >= 0.0f) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "worst rigid-bind distance %.2f (%s)", maxRigidDistance,
                      maxRigidBone.c_str());
        out += buf;
        out += "\n";
    }
    for (const auto& f : findings) {
        out += (f.blocking ? "[BLOCK] " : "[note] ");
        out += f.id;
        out += ": ";
        out += f.message;
        out += "\n";
    }
    return out;
}

OrientationReport diagnoseOrientation(const Mesh& mesh, const Skeleton& skeleton) {
    OrientationReport rep;
    rep.meshBounds = mesh.bounds;
    rep.headY = -1.0f;
    rep.footY = -1.0f;
    if (skeleton.bones.empty()) {
        rep.findings.push_back({"ORIENT_NO_SKELETON", "Skeleton has no bones.", true});
        return rep;
    }
    if (mesh.bounds.empty) {
        rep.findings.push_back(
            {"ORIENT_NO_MESH", "Mesh bounds are empty; orientation uncheckable.", false});
        return rep;
    }
    bool first = true;
    bool haveHead = false, haveFoot = false;
    for (const auto& b : skeleton.bones) {
        const Vec3 j{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                     b.globalTransform.m[3][2]};
        if (!isFiniteF(j.x) || !isFiniteF(j.y) || !isFiniteF(j.z)) {
            rep.findings.push_back(
                {"ORIENT_NON_FINITE_JOINT",
                 "Joint '" + b.name + "' is NaN/inf; orientation uncheckable.", true});
            return rep;
        }
        if (first) {
            rep.skeletonBounds.min = rep.skeletonBounds.max = j;
            rep.skeletonBounds.empty = false;
            first = false;
        } else {
            rep.skeletonBounds.grow(j);
        }
        ++rep.jointCount;
        const std::string n = lowerCopy(b.name);
        if (n.find("head") != std::string::npos && n.find("forehead") == std::string::npos) {
            if (!haveHead || j.y > rep.headY) rep.headY = j.y;
            haveHead = true;
        }
        if (n.find("foot") != std::string::npos || n.find("feet") != std::string::npos ||
            n.find("ankle") != std::string::npos) {
            if (!haveFoot || j.y < rep.footY) rep.footY = j.y;
            haveFoot = true;
        }
    }
    rep.hasHeadFeet = haveHead && haveFoot;
    // Rule 1: head must be above feet (catches 180° flips / lying-flat rigs).
    if (rep.hasHeadFeet && rep.headY <= rep.footY) {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "Head Y (%.2f) is at/below feet Y (%.2f): model is likely "
                      "rotated 180°, lying flat, or skeleton-detached.",
                      rep.headY, rep.footY);
        rep.findings.push_back({"ORIENT_HEAD_BELOW_FEET", buf, true});
    }
    // Rule 2: skeleton must vertically overlap the mesh (mesh-relative, so
    // partial assets like helmets don't false-positive).
    {
        const float mMin = mesh.bounds.min.y, mMax = mesh.bounds.max.y;
        const float sMin = rep.skeletonBounds.min.y, sMax = rep.skeletonBounds.max.y;
        const float meshH = mMax - mMin;
        if (meshH > 1e-6f) {
            const float overlap =
                std::max(0.0f, std::min(mMax, sMax) - std::max(mMin, sMin)) / meshH;
            if (overlap < 0.5f) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                              "Skeleton covers only %.0f%% of mesh height: skeleton is "
                              "likely offset/detached from the mesh.",
                              overlap * 100.0f);
                rep.findings.push_back({"ORIENT_SKELETON_OFF_MESH", buf, true});
            }
        }
    }
    // Rule 3 (advisory): gross unit/scale mismatch between rig and mesh.
    {
        const float meshH = mesh.bounds.max.y - mesh.bounds.min.y;
        const float skelH = rep.skeletonBounds.max.y - rep.skeletonBounds.min.y;
        if (meshH > 1e-6f && skelH > 1e-6f) {
            const float ratio = skelH / meshH;
            if (ratio < 0.2f || ratio > 5.0f) {
                char buf[192];
                std::snprintf(buf, sizeof(buf),
                              "Skeleton height is %.1fx mesh height: check import "
                              "units/scale.",
                              ratio);
                rep.findings.push_back({"ORIENT_SCALE_SUSPECT", buf, false});
            }
        }
    }
    // Rule 4 (blocking): rigid-bind alignment. Vertices dominated by one
    // bone (weight >= 0.5) must cluster near that bone's joint; a joint far
    // from its own rigid verts means mesh and skeleton live in different
    // spaces (detached import, baked-pose mesh vs bind skeleton, ...).
    // Threshold is mesh-relative (half the mesh height) so hero and tiny
    // assets share one rule; bones with <8 rigid verts are skipped, not
    // punished (sockets, nub ends).
    {
        const float meshH = mesh.bounds.max.y - mesh.bounds.min.y;
        if (meshH > 1e-6f && !mesh.vertices.empty()) {
            float worst = -1.0f;
            std::string worstBone;
            for (const auto& b : skeleton.bones) {
                Vec3 acc{0, 0, 0};
                std::size_t n = 0;
                for (const auto& v : mesh.vertices) {
                    for (const auto& inf : v.influences) {
                        if (inf.bone == b.id && inf.weight >= 0.5f) {
                            acc += v.position;
                            ++n;
                            break;
                        }
                    }
                }
                if (n < 8) continue;
                const Vec3 centroid = acc / static_cast<float>(n);
                const Vec3 j{b.globalTransform.m[3][0], b.globalTransform.m[3][1],
                             b.globalTransform.m[3][2]};
                const float d = distance(j, centroid);
                if (d > worst) {
                    worst = d;
                    worstBone = b.name;
                }
            }
            rep.maxRigidDistance = worst;
            rep.maxRigidBone = worstBone;
            if (worst >= 0.0f && worst > 0.5f * meshH) {
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              "Joint '%s' is %.1f units from its rigid verts "
                              "(mesh height %.1f): skeleton is detached from the mesh.",
                              worstBone.c_str(), worst, meshH);
                rep.findings.push_back({"ORIENT_RIGID_DETACHED", buf, true});
            }
        }
    }
    return rep;
}

}  // namespace m2rig