#pragma once
// Metin2 MSE (Model Script Effect) / MDE (Model Data Effect) parser.
// These files define particle effects attached to model bones.
// Wave 16: basic parsing + viewport overlay.
// Format based on Metin2 community documentation.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "m2rig/math.hpp"
#include "m2rig/result.hpp"
#include "m2rig/skeleton.hpp"

namespace m2rig {

// MSE = Model Script Effect (text format, defines particle system)
// MDE = Model Data Effect (binary format, contains particle data + attachment matrices)

// --- MSE (Text) -------------------------------------------------------------

struct MseEmitter {
    std::string name;
    std::string type;           // "particle", "light", "sound", etc.
    std::string texturePath;    // DDS texture for particles
    Vec3 position{0, 0, 0};     // Local offset from attachment bone
    Vec3 rotation{0, 0, 0};     // Euler XYZ degrees
    Vec3 scale{1, 1, 1};
    // Particle properties
    float lifeTime = 1.0f;
    float emitRate = 10.0f;
    float startSize = 1.0f;
    float endSize = 1.0f;
    Vec4 startColor{1, 1, 1, 1};
    Vec4 endColor{1, 1, 1, 0};
    Vec3 gravity{0, 0, 0};
    Vec3 velocity{0, 0, 0};
    float spread = 0.0f;
    bool billboard = true;
    // Animation
    bool loop = true;
    float speed = 1.0f;
};

struct MseAttachment {
    std::string boneName;       // Target bone in skeleton
    std::vector<MseEmitter> emitters;
};

struct MseDocument {
    std::string version;        // e.g., "MSE 1.0"
    std::vector<MseAttachment> attachments;
};

// Parse MSE text file (key=value format, sections)
Result<MseDocument> parseMse(const std::string& text, const std::string& asset = "<mse>");

// Stringify MSE document back to text
std::string stringifyMse(const MseDocument& doc);

// --- MDE (Binary) -----------------------------------------------------------

struct MdeHeader {
    char magic[4];              // "MDE "
    uint32_t version;           // 1
    uint32_t attachmentCount;
    uint32_t emitterCount;
    uint32_t textureCount;
    uint32_t stringTableOffset;
    uint32_t stringTableSize;
};

struct MdeAttachmentBin {
    char boneName[64];          // Null-terminated
    uint32_t emitterIndex;      // First emitter index
    uint32_t emitterCount;
    Mat4 attachmentMatrix;      // Bone-relative transform (row-major)
};

struct MdeEmitterBin {
    char name[64];
    char type[32];
    char texturePath[256];
    Vec3 position;
    Vec3 rotation;              // Euler degrees
    Vec3 scale;
    float lifeTime;
    float emitRate;
    float startSize, endSize;
    Vec4 startColor, endColor;
    Vec3 gravity;
    Vec3 velocity;
    float spread;
    uint32_t flags;             // billboard=1, loop=2, etc.
    float speed;
};

struct MdeDocument {
    MdeHeader header;
    std::vector<MdeAttachmentBin> attachments;
    std::vector<MdeEmitterBin> emitters;
    std::vector<std::string> textures;
    std::string stringTable;
};

Result<MdeDocument> parseMde(const std::uint8_t* data, std::size_t size,
                             const std::string& asset = "<mde>");
Result<MdeDocument> readMdeFile(const std::string& path);

// --- Viewport overlay -------------------------------------------------------

struct ParticleOverlay {
    Vec3 worldPos;
    Vec3 color;
    float size;
    float life;        // 0..1 (1 = freshly spawned, 0 = about to die)
    float rotation;
};

struct MseInstance {
    std::string attachmentName;
    std::vector<MseEmitter> emitters;
    Mat4 boneTransform;       // Current bone world transform
    std::vector<ParticleOverlay> particles;
    float time = 0.0f;
    bool playing = false;
};

// --- Stateful particle pool -------------------------------------------------
// Fixed-cap spawn-ordered pool (oldest first). Honest cap: 2048 live
// particles; beyond that new spawns are dropped and counted in overflow_.
// The viewport consumer draws the first 200 entries and drops the tail, so
// first-N always means oldest-N (stable under the cap).
// Physics uses ONLY format params: spawn pos = bone * em.position,
// vel0 = em.velocity (world-axis preview simplification), then per tick
// vel += gravity*dt, pos += vel*dt. Spread/speed/scale/rotation are ignored
// deterministically (no RNG) and documented here, not hidden.
// Scrub contract: update(dt<=0 or non-finite) only refreshes bone transforms,
// never advances state. Scrub = reset() + fast-forward with fixed dt.
constexpr std::size_t kMseMaxParticles = 2048;

struct MsePooledParticle {
    Vec3 pos{0, 0, 0};
    Vec3 vel{0, 0, 0};
    float age = 0.0f;
    float life = 1.0f;
    std::size_t emitterFlatIdx = 0;  // index into flattened doc emitters
    std::size_t attachmentIdx = 0;   // owning attachment (for MseInstance split)
};

// Runtime MSE system for viewport preview
struct MseRuntime {
    MseDocument doc;
    std::vector<MseInstance> instances;

    void update(float dt, const Skeleton& skel, const std::vector<Mat4>& bonePalette);
    std::vector<ParticleOverlay> getActiveParticles() const;

    // Stateful pool control (added without breaking update/getActiveParticles).
    void reset();
    std::size_t overflowCount() const { return overflow_; }
    std::size_t activeCount() const { return pool_.size(); }
    float clock() const { return clock_; }

private:
    std::vector<MsePooledParticle> pool_;  // spawn-ordered, oldest first
    std::vector<float> emissionDebt_;      // per flat emitter, fractional carry [0,1)
    std::vector<float> emitterElapsed_;    // per flat emitter, seconds since reset
    float clock_ = 0.0f;                   // total forward time since reset
    std::size_t overflow_ = 0;             // capped spawns dropped since reset
};

}  // namespace m2rig