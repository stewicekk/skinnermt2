// MSE / MDE parser implementation
#include "m2rig/mse.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
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

// --- MSE Runtime ---

void MseRuntime::update(float dt, const Skeleton& skel, const std::vector<Mat4>& bonePalette) {
    instances.clear();
    
    for (const auto& att : doc.attachments) {
        MseInstance inst;
        inst.attachmentName = att.boneName;
        inst.emitters = att.emitters;
        
        // Find bone index (bounds-checked: the palette belongs to a
        // possibly different skeleton than the effect was authored for).
        const Bone* bone = skel.findByName(att.boneName);
        if (bone && bone->id < bonePalette.size()) {
            inst.boneTransform = bonePalette[bone->id];
        } else {
            inst.boneTransform = Mat4::identity();
        }
        
        // Update particles for each emitter
        for (auto& em : inst.emitters) {
            if (!em.loop && inst.time > em.lifeTime) continue;
            
            // Simple particle simulation (emit + update)
            // In a real implementation, this would maintain particle state across frames
            // For now, generate a few representative particles for visualization
            int numParticles = static_cast<int>(em.emitRate * dt);
            numParticles = std::min(numParticles, 50);
            
            for (int i = 0; i < numParticles; ++i) {
                ParticleOverlay p;
                p.worldPos = inst.boneTransform.transformPoint(em.position);
                p.color = {em.startColor.x, em.startColor.y, em.startColor.z};
                p.size = em.startSize;
                p.life = 1.0f;
                p.rotation = 0.0f;
                inst.particles.push_back(p);
            }
        }
        
        inst.time += dt;
        inst.playing = true;
        instances.push_back(std::move(inst));
    }
}

std::vector<ParticleOverlay> MseRuntime::getActiveParticles() const {
    std::vector<ParticleOverlay> all;
    for (const auto& inst : instances) {
        for (const auto& p : inst.particles) {
            all.push_back(p);
        }
    }
    return all;
}

}  // namespace m2rig