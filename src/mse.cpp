// MSE / MDE parser implementation
#include "m2rig/mse.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace m2rig {

namespace {

// MSE text parsing helpers
struct MseTokenizer {
    const char* p;
    const char* end;
    
    MseTokenizer(const std::string& text) : p(text.c_str()), end(text.c_str() + text.size()) {}
    
    void skipWhitespace() {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        // Skip line comments (// ...) and block comments (/* ... */)
        while (p < end) {
            if (p + 1 < end && p[0] == '/' && p[1] == '/') {
                // Line comment: skip to end of line
                p += 2;
                while (p < end && *p != '\n' && *p != '\r') ++p;
            } else if (p + 1 < end && p[0] == '/' && p[1] == '*') {
                // Block comment: skip to */
                p += 2;
                while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) ++p;
                if (p + 1 < end) p += 2;
            } else {
                break;
            }
            // After skipping comment, skip more whitespace
            while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        }
    }
    
    bool match(const char* keyword) {
        skipWhitespace();
        std::size_t len = std::strlen(keyword);
        if (p + len <= end && std::strncmp(p, keyword, len) == 0) {
            p += len;
            return true;
        }
        return false;
    }
    
    bool matchChar(char c) {
        skipWhitespace();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }
    
    std::string readIdentifier() {
        skipWhitespace();
        const char* start = p;
        while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '.')) ++p;
        return std::string(start, p);
    }

    // Skip one unknown token, always consuming at least one character so
    // malformed input (stray punctuation) can never hang the parser.
    void skipUnknown() {
        const char* before = p;
        readIdentifier();
        if (p == before) {
            skipWhitespace();
            if (p < end) ++p;
        }
    }
    
    std::string readString() {
        skipWhitespace();
        if (p >= end || *p != '"') return "";
        ++p;
        const char* start = p;
        while (p < end && *p != '"') ++p;
        std::string result(start, p);
        if (p < end && *p == '"') ++p;
        return result;
    }
    
    float readFloat() {
        skipWhitespace();
        const char* start = p;
        while (p < end && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '.' || *p == '-' || *p == '+' || *p == 'e' || *p == 'E')) ++p;
        std::string s(start, p);
        return std::strtof(s.c_str(), nullptr);
    }
    
    int readInt() {
        skipWhitespace();
        const char* start = p;
        while (p < end && (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-')) ++p;
        std::string s(start, p);
        return std::strtol(s.c_str(), nullptr, 10);
    }
    
    bool eof() {
        skipWhitespace();
        return p >= end;
    }
};

Vec3 parseVec3(MseTokenizer& tk) {
    tk.matchChar('(');
    float x = tk.readFloat();
    tk.matchChar(',');
    float y = tk.readFloat();
    tk.matchChar(',');
    float z = tk.readFloat();
    tk.matchChar(')');
    return {x, y, z};
}

Vec4 parseVec4(MseTokenizer& tk) {
    tk.matchChar('(');
    float x = tk.readFloat();
    tk.matchChar(',');
    float y = tk.readFloat();
    tk.matchChar(',');
    float z = tk.readFloat();
    tk.matchChar(',');
    float w = tk.readFloat();
    tk.matchChar(')');
    return {x, y, z, w};
}

Mat4 parseMat4(MseTokenizer& tk) {
    Mat4 m = Mat4::identity();
    tk.matchChar('[');
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            m.m[r][c] = tk.readFloat();
            if (c < 3) tk.matchChar(',');
        }
        if (r < 3) tk.matchChar(';');
    }
    tk.matchChar(']');
    return m;
}

} // anonymous namespace

// --- MSE Text Parser ---

Result<MseDocument> parseMse(const std::string& text, const std::string& asset) {
    (void)asset;
    MseDocument doc;
    MseTokenizer tk(text);
    
    // Parse version
    if (tk.match("MSE")) {
        doc.version = tk.readIdentifier();
    } else {
        doc.version = "MSE 1.0";
    }
    
    // Parse attachments: attachment "bonename" { emitters... }
    while (!tk.eof()) {
        if (!tk.match("attachment")) break;
        
        MseAttachment attachment;
        attachment.boneName = tk.readString();
        
        tk.matchChar('{');
        while (!tk.eof() && !tk.matchChar('}')) {
            if (tk.match("emitter")) {
                MseEmitter emitter;
                emitter.name = tk.readString();
                
                tk.matchChar('{');
                while (!tk.eof() && !tk.matchChar('}')) {
                    if (tk.match("type")) emitter.type = tk.readString();
                    else if (tk.match("texture")) emitter.texturePath = tk.readString();
                    else if (tk.match("position")) emitter.position = parseVec3(tk);
                    else if (tk.match("rotation")) emitter.rotation = parseVec3(tk);
                    else if (tk.match("scale")) emitter.scale = parseVec3(tk);
                    else if (tk.match("lifeTime")) emitter.lifeTime = tk.readFloat();
                    else if (tk.match("emitRate")) emitter.emitRate = tk.readFloat();
                    else if (tk.match("startSize")) emitter.startSize = tk.readFloat();
                    else if (tk.match("endSize")) emitter.endSize = tk.readFloat();
                    else if (tk.match("startColor")) emitter.startColor = parseVec4(tk);
                    else if (tk.match("endColor")) emitter.endColor = parseVec4(tk);
                    else if (tk.match("gravity")) emitter.gravity = parseVec3(tk);
                    else if (tk.match("velocity")) emitter.velocity = parseVec3(tk);
                    else if (tk.match("spread")) emitter.spread = tk.readFloat();
                    else if (tk.match("billboard")) emitter.billboard = tk.readInt() != 0;
                    else if (tk.match("loop")) emitter.loop = tk.readInt() != 0;
                    else if (tk.match("speed")) emitter.speed = tk.readFloat();
                    else tk.skipUnknown();
                }
                attachment.emitters.push_back(std::move(emitter));
            } else {
                tk.skipUnknown();
            }
        }
        doc.attachments.push_back(std::move(attachment));
    }
    
    return Result<MseDocument>::ok(std::move(doc));
}

std::string stringifyMse(const MseDocument& doc) {
    std::ostringstream out;
    // Ensure version starts with "MSE " for round-trip compatibility
    if (doc.version.rfind("MSE ", 0) == 0) {
        out << doc.version << "\n\n";
    } else {
        out << "MSE " << doc.version << "\n\n";
    }
    for (const auto& att : doc.attachments) {
        out << "attachment \"" << att.boneName << "\"\n{\n";
        for (const auto& em : att.emitters) {
            out << "  emitter \"" << em.name << "\"\n  {\n";
            out << "    type \"" << em.type << "\"\n";
            out << "    texture \"" << em.texturePath << "\"\n";
            out << "    position (" << em.position.x << ", " << em.position.y << ", " << em.position.z << ")\n";
            out << "    rotation (" << em.rotation.x << ", " << em.rotation.y << ", " << em.rotation.z << ")\n";
            out << "    scale (" << em.scale.x << ", " << em.scale.y << ", " << em.scale.z << ")\n";
            out << "    lifeTime " << em.lifeTime << "\n";
            out << "    emitRate " << em.emitRate << "\n";
            out << "    startSize " << em.startSize << "\n";
            out << "    endSize " << em.endSize << "\n";
            out << "    startColor (" << em.startColor.x << ", " << em.startColor.y << ", " << em.startColor.z << ", " << em.startColor.w << ")\n";
            out << "    endColor (" << em.endColor.x << ", " << em.endColor.y << ", " << em.endColor.z << ", " << em.endColor.w << ")\n";
            out << "    gravity (" << em.gravity.x << ", " << em.gravity.y << ", " << em.gravity.z << ")\n";
            out << "    velocity (" << em.velocity.x << ", " << em.velocity.y << ", " << em.velocity.z << ")\n";
            out << "    spread " << em.spread << "\n";
            out << "    billboard " << (em.billboard ? 1 : 0) << "\n";
            out << "    loop " << (em.loop ? 1 : 0) << "\n";
            out << "    speed " << em.speed << "\n";
            out << "  }\n";
        }
        out << "}\n\n";
    }
    return out.str();
}

// --- MDE Binary Parser ---

Result<MdeDocument> parseMde(const std::uint8_t* data, std::size_t size,
                             const std::string& asset) {
    if (data == nullptr || size < sizeof(MdeHeader)) {
        return Result<MdeDocument>::fail("MDE file too small for header", "FORMAT", asset);
    }

    const MdeHeader* hdr = reinterpret_cast<const MdeHeader*>(data);
    if (std::memcmp(hdr->magic, "MDE ", 4) != 0) {
        return Result<MdeDocument>::fail("Invalid MDE magic", "FORMAT", asset);
    }
    if (hdr->version != 1) {
        return Result<MdeDocument>::fail("Unsupported MDE version: " + std::to_string(hdr->version), "FORMAT", asset);
    }
    // Untrusted counts: cap before any resize/computation so a crafted
    // header (billions of attachments) fails explicitly instead of throwing
    // bad_alloc/length_error across the no-exceptions boundary.
    constexpr std::uint32_t kMaxAttachments = 1u << 20;
    constexpr std::uint32_t kMaxEmitters = 1u << 24;
    constexpr std::uint32_t kMaxStringTable = 1u << 28;
    if (hdr->attachmentCount > kMaxAttachments || hdr->emitterCount > kMaxEmitters ||
        hdr->stringTableSize > kMaxStringTable) {
        return Result<MdeDocument>::fail("MDE header counts exceed sanity limits", "FORMAT",
                                         asset);
    }

    const std::size_t expected = sizeof(MdeHeader) +
                                 static_cast<std::size_t>(hdr->attachmentCount) * sizeof(MdeAttachmentBin) +
                                 static_cast<std::size_t>(hdr->emitterCount) * sizeof(MdeEmitterBin);
    if (size < expected) {
        return Result<MdeDocument>::fail("MDE file truncated", "FORMAT", asset);
    }
    
    MdeDocument doc;
    doc.header = *hdr;
    
    const uint8_t* p = data + sizeof(MdeHeader);
    
    // Attachments
    doc.attachments.resize(hdr->attachmentCount);
    std::memcpy(doc.attachments.data(), p, hdr->attachmentCount * sizeof(MdeAttachmentBin));
    p += hdr->attachmentCount * sizeof(MdeAttachmentBin);
    
    // Emitters
    doc.emitters.resize(hdr->emitterCount);
    std::memcpy(doc.emitters.data(), p, hdr->emitterCount * sizeof(MdeEmitterBin));
    p += hdr->emitterCount * sizeof(MdeEmitterBin);
    
    // String table
    if (hdr->stringTableOffset > 0 && hdr->stringTableSize > 0) {
        if (size >= hdr->stringTableOffset + hdr->stringTableSize) {
            doc.stringTable.assign(
                reinterpret_cast<const char*>(data + hdr->stringTableOffset),
                hdr->stringTableSize
            );
        }
    }
    
    return Result<MdeDocument>::ok(std::move(doc));
}

Result<MdeDocument> readMdeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Result<MdeDocument>::fail("Cannot open MDE file: " + path, "IO", path);
    }
    const auto endPos = file.tellg();
    if (endPos == std::ifstream::pos_type(-1)) {
        return Result<MdeDocument>::fail("Cannot determine MDE file size: " + path, "IO", path);
    }
    const auto size64 = static_cast<std::uint64_t>(endPos);
    constexpr std::uint64_t kMaxMdeBytes = 1ULL << 30;
    if (size64 > kMaxMdeBytes) {
        return Result<MdeDocument>::fail("MDE file exceeds 1 GiB sanity limit: " + path, "FORMAT",
                                         path);
    }
    const std::size_t size = static_cast<std::size_t>(size64);
    file.seekg(0);
    std::vector<std::uint8_t> data(size);
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        return Result<MdeDocument>::fail("Failed reading MDE file: " + path, "IO", path);
    }
    return parseMde(data.data(), size, path);
}

// --- MSE Runtime (stateful pool) ---

namespace {

// Flattened emitter lookup shared by update() and getActiveParticles().
// Deterministic doc-order (attachment order, emitter order within).
void collectFlatEmitters(const MseDocument& doc, std::vector<const MseEmitter*>& flat,
                         std::vector<std::size_t>& flatAtt) {
    flat.clear();
    flatAtt.clear();
    std::size_t total = 0;
    for (const auto& att : doc.attachments) total += att.emitters.size();
    flat.reserve(total);
    flatAtt.reserve(total);
    for (std::size_t ai = 0; ai < doc.attachments.size(); ++ai) {
        for (const auto& em : doc.attachments[ai].emitters) {
            flat.push_back(&em);
            flatAtt.push_back(ai);
        }
    }
}

ParticleOverlay pooledToOverlay(const MsePooledParticle& p, const MseEmitter& em) {
    float t = 1.0f;
    if (p.life > 0.0f && std::isfinite(p.life) && std::isfinite(p.age)) {
        t = p.age / p.life;
    }
    if (!std::isfinite(t)) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    ParticleOverlay o;
    o.worldPos = p.pos;
    o.color = {em.startColor.x + (em.endColor.x - em.startColor.x) * t,
               em.startColor.y + (em.endColor.y - em.startColor.y) * t,
               em.startColor.z + (em.endColor.z - em.startColor.z) * t};
    o.size = em.startSize + (em.endSize - em.startSize) * t;
    o.life = 1.0f - t;
    o.rotation = 0.0f;
    return o;
}

}  // namespace

void MseRuntime::reset() {
    pool_.clear();
    std::size_t total = 0;
    for (const auto& att : doc.attachments) total += att.emitters.size();
    emissionDebt_.assign(total, 0.0f);
    emitterElapsed_.assign(total, 0.0f);
    clock_ = 0.0f;
    overflow_ = 0;
    instances.clear();
}

void MseRuntime::update(float dt, const Skeleton& skel, const std::vector<Mat4>& bonePalette) {
    // A new document assigned without reset() invalidates flat indices:
    // drop the stale pool honestly rather than misattributing particles.
    std::size_t total = 0;
    for (const auto& att : doc.attachments) total += att.emitters.size();
    if (emissionDebt_.size() != total || emitterElapsed_.size() != total) {
        pool_.clear();
        emissionDebt_.assign(total, 0.0f);
        emitterElapsed_.assign(total, 0.0f);
        clock_ = 0.0f;
        overflow_ = 0;
    }

    // Resolve bone transforms (identity fallback for foreign skeletons,
    // same bounds-checked guarantee as before — never OOB).
    std::vector<Mat4> attXform;
    attXform.reserve(doc.attachments.size());
    for (const auto& att : doc.attachments) {
        const Bone* bone = skel.findByName(att.boneName);
        if (bone != nullptr &&
            static_cast<std::size_t>(bone->id) < bonePalette.size()) {
            attXform.push_back(bonePalette[static_cast<std::size_t>(bone->id)]);
        } else {
            attXform.push_back(Mat4::identity());
        }
    }

    std::vector<const MseEmitter*> flat;
    std::vector<std::size_t> flatAtt;
    collectFlatEmitters(doc, flat, flatAtt);

    const bool advance = (dt > 0.0f) && (std::isfinite(dt) != 0);
    if (advance) {
        // 1) Integrate existing particles: vel += gravity*dt, pos += vel*dt.
        //    Uses ONLY the format's velocity/gravity params (world-axis
        //    preview simplification, documented in mse.hpp).
        for (auto& p : pool_) {
            Vec3 grav{0, 0, 0};
            if (p.emitterFlatIdx < flat.size()) grav = flat[p.emitterFlatIdx]->gravity;
            if (std::isfinite(grav.x) != 0 && std::isfinite(grav.y) != 0 &&
                std::isfinite(grav.z) != 0) {
                p.vel.x += grav.x * dt;
                p.vel.y += grav.y * dt;
                p.vel.z += grav.z * dt;
            }
            if (std::isfinite(p.vel.x) != 0 && std::isfinite(p.vel.y) != 0 &&
                std::isfinite(p.vel.z) != 0) {
                p.pos.x += p.vel.x * dt;
                p.pos.y += p.vel.y * dt;
                p.pos.z += p.vel.z * dt;
            }
            p.age += dt;
        }

        // 2) Kill at lifeTime (stable compaction keeps spawn order = oldest first).
        std::size_t w = 0;
        for (std::size_t r = 0; r < pool_.size(); ++r) {
            const MsePooledParticle& p = pool_[r];
            const bool alive = (std::isfinite(p.age) != 0) && (std::isfinite(p.life) != 0) &&
                               (p.life > 0.0f) && (p.age < p.life);
            if (alive) {
                if (w != r) pool_[w] = pool_[r];
                ++w;
            }
        }
        pool_.resize(w);

        // 3) Emit per emitter: fractional debt carry, cap-clamped, loop-gated.
        for (std::size_t f = 0; f < flat.size(); ++f) {
            const MseEmitter& em = *flat[f];
            if ((std::isfinite(em.lifeTime) == 0) || em.lifeTime <= 0.0f) continue;
            if ((std::isfinite(em.emitRate) == 0) || em.emitRate <= 0.0f) continue;
            // Non-loop emitters spawn only during the first lifeTime window;
            // live particles finish their course (no mid-life kill).
            if (!em.loop && emitterElapsed_[f] >= em.lifeTime) continue;
            const float add = em.emitRate * dt;
            if ((std::isfinite(add) == 0) || add <= 0.0f) continue;
            float& debt = emissionDebt_[f];
            debt += add;
            if (debt < 1.0f) continue;
            const double wantD = std::floor(static_cast<double>(debt));
            float frac = debt - static_cast<float>(wantD);
            if ((std::isfinite(frac) == 0) || frac < 0.0f || frac >= 1.0f) frac = 0.0f;
            if (debt > 16777216.0f) frac = 0.0f;  // float can't carry fraction past 2^24
            debt = frac;
            if (wantD <= 0.0) continue;
            const std::size_t freeSlots =
                (pool_.size() < kMseMaxParticles) ? (kMseMaxParticles - pool_.size()) : 0;
            std::size_t spawn = freeSlots;
            if (wantD < static_cast<double>(freeSlots)) spawn = static_cast<std::size_t>(wantD);
            const double droppedD = wantD - static_cast<double>(spawn);
            if (droppedD > 0.0) {
                const double room =
                    static_cast<double>((std::numeric_limits<std::size_t>::max)() - overflow_);
                if (droppedD >= room) {
                    overflow_ = (std::numeric_limits<std::size_t>::max)();
                } else {
                    overflow_ += static_cast<std::size_t>(droppedD);
                }
            }
            if (spawn == 0) continue;
            const std::size_t attIdx = flatAtt[f];
            const Mat4& xf = attXform[attIdx];
            const Vec3 base = xf.transformPoint(em.position);
            Vec3 v0 = em.velocity;
            if ((std::isfinite(v0.x) == 0) || (std::isfinite(v0.y) == 0) ||
                (std::isfinite(v0.z) == 0)) {
                v0 = Vec3{0, 0, 0};
            }
            for (std::size_t i = 0; i < spawn; ++i) {
                MsePooledParticle p;
                p.pos = base;
                p.vel = v0;
                p.age = 0.0f;
                p.life = em.lifeTime;
                p.emitterFlatIdx = f;
                p.attachmentIdx = attIdx;
                pool_.push_back(p);
            }
        }

        // 4) Advance clocks (per-emitter elapsed drives non-loop gating).
        for (auto& e : emitterElapsed_) e += dt;
        clock_ += dt;
    }

    // Rebuild instances metadata (boneTransform/time/playing) and split the
    // pool back per attachment so legacy readers of instances[].particles
    // keep working; getActiveParticles() stays pool-ordered (oldest first).
    instances.clear();
    instances.reserve(doc.attachments.size());
    for (std::size_t ai = 0; ai < doc.attachments.size(); ++ai) {
        MseInstance inst;
        inst.attachmentName = doc.attachments[ai].boneName;
        inst.emitters = doc.attachments[ai].emitters;
        inst.boneTransform = attXform[ai];
        inst.time = clock_;
        inst.playing = true;
        instances.push_back(std::move(inst));
    }
    for (const auto& p : pool_) {
        if (p.attachmentIdx >= instances.size()) continue;
        if (p.emitterFlatIdx >= flat.size()) continue;
        instances[p.attachmentIdx].particles.push_back(
            pooledToOverlay(p, *flat[p.emitterFlatIdx]));
    }
}

std::vector<ParticleOverlay> MseRuntime::getActiveParticles() const {
    // Cap-bounded snapshot: pool_ <= kMseMaxParticles, so the copy never grows.
    // Order is spawn order (oldest first) so the viewport's first-200 cap
    // keeps the oldest and drops the newest tail.
    std::vector<const MseEmitter*> flat;
    flat.reserve(emissionDebt_.size());
    for (const auto& att : doc.attachments) {
        for (const auto& em : att.emitters) flat.push_back(&em);
    }
    std::vector<ParticleOverlay> all;
    all.reserve(pool_.size());
    for (const auto& p : pool_) {
        if (p.emitterFlatIdx >= flat.size()) continue;
        all.push_back(pooledToOverlay(p, *flat[p.emitterFlatIdx]));
    }
    return all;
}

}  // namespace m2rig