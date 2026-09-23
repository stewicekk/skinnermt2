// MSE/MDE effect parser tests: round-trip, malformed-input termination,
// binary header validation, untrusted-count guards, runtime palette guard.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../tests/expect.hpp"
#include "m2rig/mse.hpp"
#include "m2rig/samples.hpp"
#include "m2rig/skeleton.hpp"
#include "m2rig/skin_weights.hpp"

using namespace m2rig;

namespace {

const char* kSampleMse =
    "MSE 1.0\n"
    "\n"
    "attachment \"Bip01 Head\"\n"
    "{\n"
    "  emitter \"head_glow\"\n"
    "  {\n"
    "    type \"particle\"\n"
    "    texture \"glow.dds\"\n"
    "    position (0.0, 2.4, 0.1)\n"
    "    lifeTime 1.5\n"
    "    emitRate 20.0\n"
    "    billboard 1\n"
    "    loop 1\n"
    "  }\n"
    "}\n";

}  // namespace

M2RIG_TEST(mse, parse_stringify_roundtrip) {
    int failures = 0;
    auto doc = parseMse(kSampleMse, "sample");
    CHECK_TRUE(doc.succeeded());
    if (!doc.succeeded()) return failures + 1;
    CHECK_EQ(doc.value().attachments.size(), 1u);
    CHECK_TRUE(doc.value().attachments[0].boneName == "Bip01 Head");
    CHECK_EQ(doc.value().attachments[0].emitters.size(), 1u);
    const MseEmitter& e = doc.value().attachments[0].emitters[0];
    CHECK_TRUE(e.name == "head_glow");
    CHECK_TRUE(e.type == "particle");
    CHECK_TRUE(e.texturePath == "glow.dds");
    CHECK_NEAR(e.position.y, 2.4f, 1e-5);
    CHECK_NEAR(e.lifeTime, 1.5f, 1e-5);
    CHECK_TRUE(e.billboard);
    const std::string back = stringifyMse(doc.value());
    CHECK_FALSE(back.empty());
    auto doc2 = parseMse(back, "reserialized");
    CHECK_TRUE(doc2.succeeded());
    if (!doc2.succeeded()) return failures + 1;
    CHECK_EQ(doc2.value().attachments.size(), 1u);
    CHECK_EQ(doc2.value().attachments[0].emitters.size(), 1u);
    CHECK_TRUE(doc2.value().attachments[0].emitters[0].name == "head_glow");
    CHECK_NEAR(doc2.value().attachments[0].emitters[0].position.y, 2.4f, 1e-5);
    return failures;
}

M2RIG_TEST(mse, malformed_input_terminates) {
    // Stray punctuation inside blocks used to hang the skip-unknown path
    // (identifier read consumed nothing). Completing at all IS the test,
    // plus the well-formed prefix must survive.
    int failures = 0;
    const std::string evil =
        "MSE 1.0\nattachment \"Bip01\" { emitter \"e\" { type \"particle\" @#$; %%% }}}\n"
        "@@@ ### ;;;\n";
    auto doc = parseMse(evil, "evil");
    CHECK_TRUE(doc.succeeded());
    if (!doc.succeeded()) return failures + 1;
    CHECK_EQ(doc.value().attachments.size(), 1u);
    CHECK_TRUE(doc.value().attachments[0].boneName == "Bip01");
    // Pure garbage yields an empty (but valid) document, never a hang.
    auto empty = parseMse("@#$ %%% ;;;", "garbage");
    CHECK_TRUE(empty.succeeded());
    if (empty.succeeded()) CHECK_EQ(empty.value().attachments.size(), 0u);
    auto blank = parseMse("", "blank");
    CHECK_TRUE(blank.succeeded());
    return failures;
}

namespace {

std::vector<std::uint8_t> makeMdeBytes(std::uint32_t attachments, std::uint32_t emitters,
                                       const char* magic = "MDE ", std::uint32_t version = 1,
                                       bool fullPayload = true) {
    MdeHeader hdr{};
    std::memcpy(hdr.magic, magic, 4);
    hdr.version = version;
    hdr.attachmentCount = attachments;
    hdr.emitterCount = emitters;
    // NOTE: never size a buffer from untrusted counts here — the insane test
    // below passes fullPayload=false so only the header is materialized.
    const std::size_t payload =
        fullPayload ? static_cast<std::size_t>(attachments) * sizeof(MdeAttachmentBin) +
                          static_cast<std::size_t>(emitters) * sizeof(MdeEmitterBin)
                    : 0u;
    std::vector<std::uint8_t> buf(sizeof(hdr) + payload, 0);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    return buf;
}

}  // namespace

M2RIG_TEST(mse, mde_rejects_bad_headers) {
    int failures = 0;
    CHECK_FALSE(parseMde(nullptr, 0, "null").succeeded());
    const std::uint8_t tiny[4] = {'M', 'D', 'E', ' '};
    CHECK_FALSE(parseMde(tiny, sizeof(tiny), "tiny").succeeded());
    auto badMagic = makeMdeBytes(0, 0, "XXXX");
    CHECK_FALSE(parseMde(badMagic.data(), badMagic.size(), "badmagic").succeeded());
    auto badVer = makeMdeBytes(0, 0, "MDE ", 99);
    CHECK_FALSE(parseMde(badVer.data(), badVer.size(), "badver").succeeded());
    // Truncated payload (header claims entries the buffer lacks).
    auto trunc = makeMdeBytes(2, 1);
    CHECK_FALSE(parseMde(trunc.data(), sizeof(MdeHeader), "trunc").succeeded());
    return failures;
}

M2RIG_TEST(mse, mde_parses_minimal_valid) {
    int failures = 0;
    auto buf = makeMdeBytes(1, 1);
    auto* att = reinterpret_cast<MdeAttachmentBin*>(buf.data() + sizeof(MdeHeader));
    // strncpy is banned under /WX (C4996): bounded memcpy + terminator.
    std::memset(att->boneName, 0, sizeof(att->boneName));
    std::memcpy(att->boneName, "Bip01 Head", sizeof("Bip01 Head") - 1);
    att->emitterIndex = 0;
    att->emitterCount = 1;
    auto* em = reinterpret_cast<MdeEmitterBin*>(buf.data() + sizeof(MdeHeader) +
                                                sizeof(MdeAttachmentBin));
    std::memset(em->name, 0, sizeof(em->name));
    std::memcpy(em->name, "puff", sizeof("puff") - 1);
    std::memset(em->texturePath, 0, sizeof(em->texturePath));
    std::memcpy(em->texturePath, "puff.dds", sizeof("puff.dds") - 1);
    em->lifeTime = 2.0f;
    auto doc = parseMde(buf.data(), buf.size(), "mini");
    CHECK_TRUE(doc.succeeded());
    if (!doc.succeeded()) return failures + 1;
    CHECK_EQ(doc.value().attachments.size(), 1u);
    CHECK_EQ(doc.value().emitters.size(), 1u);
    CHECK_TRUE(std::string(doc.value().attachments[0].boneName) == "Bip01 Head");
    CHECK_NEAR(doc.value().emitters[0].lifeTime, 2.0f, 1e-6);
    return failures;
}

M2RIG_TEST(mse, mde_insane_counts_fail_explicitly) {
    // A crafted header claiming billions of entries must fail with an
    // explicit error — never throw bad_alloc across the boundary, never hang.
    int failures = 0;
    auto buf = makeMdeBytes(0xFFFFFFFFu, 0xFFFFFFFFu, "MDE ", 1, false);
    auto doc = parseMde(buf.data(), buf.size(), "insane");
    CHECK_FALSE(doc.succeeded());
    return failures;
}

M2RIG_TEST(mse, runtime_survives_palette_mismatch) {
    // An effect authored for a foreign skeleton (unknown bones, short
    // palette) must degrade to identity — never read OOB.
    int failures = 0;
    auto sample = makeSampleArmor();
    CHECK_TRUE(sample.succeeded());
    if (!sample.succeeded()) return failures + 1;
    auto doc = parseMse(kSampleMse, "sample");
    CHECK_TRUE(doc.succeeded());
    if (!doc.succeeded()) return failures + 1;
    MseRuntime rt;
    rt.doc = doc.value();
    // Empty palette + known bone: must degrade to identity, never read OOB.
    // dt=1 emits emitRate(20) particles, proving generation ran.
    std::vector<Mat4> emptyPalette;
    rt.update(1.0f, sample.value().skeleton, emptyPalette);
    CHECK_EQ(rt.getActiveParticles().size(), 20u);
    // Matching skeleton but short palette: still no OOB.
    std::vector<Mat4> shortPalette(2, Mat4::identity());
    rt.update(1.0f, sample.value().skeleton, shortPalette);
    CHECK_EQ(rt.getActiveParticles().size(), 20u);
    return failures;
}
