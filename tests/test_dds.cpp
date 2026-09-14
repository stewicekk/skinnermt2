// DDS decoder tests: synthetic DXT1/DXT5 blocks + malformed inputs +
// a real Data/Models file header check (graceful skip when absent).
#include <cstdio>
#include <filesystem>
#include <vector>

#include "../tests/expect.hpp"
#include "m2rig/dds.hpp"

using namespace m2rig;

namespace {

std::vector<std::uint8_t> ddsHeader(std::uint32_t w, std::uint32_t h, std::uint32_t fourCC,
                                    std::uint32_t mips = 1) {
    std::vector<std::uint8_t> out(128, 0);
    out[0] = 'D';
    out[1] = 'D';
    out[2] = 'S';
    out[3] = ' ';
    auto u32 = [&](std::size_t off, std::uint32_t v) {
        out[off] = static_cast<std::uint8_t>(v & 0xFF);
        out[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        out[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        out[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };
    u32(4, 124);
    u32(4 + 8, h);
    u32(4 + 12, w);
    u32(4 + 24, mips);
    u32(4 + 72, 32);
    u32(4 + 76, 0x4);
    u32(4 + 80, fourCC);
    return out;
}

}  // namespace

M2RIG_TEST(dds, rejects_garbage) {
    int failures = 0;
    const std::uint8_t tiny[4] = {0, 0, 0, 0};
    CHECK_FALSE(decodeDds(tiny, sizeof(tiny)).succeeded());
    std::vector<std::uint8_t> bad(128, 0);
    CHECK_FALSE(decodeDds(bad.data(), bad.size()).succeeded());
    auto hdr = ddsHeader(4, 4, 0x31545844);
    CHECK_FALSE(decodeDds(hdr.data(), hdr.size()).succeeded());  // no base level
    auto uncompressed = ddsHeader(4, 4, 0);
    uncompressed.resize(128 + 64, 0);
    CHECK_FALSE(decodeDds(uncompressed.data(), uncompressed.size()).succeeded());
    return failures;
}

M2RIG_TEST(dds, decodes_dxt1_solid_red) {
    int failures = 0;
    auto file = ddsHeader(4, 4, 0x31545844);
    // color0 = red (0xF800), color1 = green (0x07E0), all indices 0.
    const std::uint8_t block[8] = {0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};
    file.insert(file.end(), block, block + 8);
    auto img = decodeDds(file.data(), file.size());
    CHECK_TRUE(img.succeeded());
    if (!img.succeeded()) return failures + 1;
    CHECK_EQ(img.value().width, 4u);
    CHECK_EQ(img.value().height, 4u);
    CHECK_TRUE(img.value().format == "DXT1");
    CHECK_FALSE(img.value().hasAlpha);
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint8_t* px = &img.value().rgba[i * 4];
        CHECK_EQ(px[0], 255u);  // R8 = (31<<3)|(31>>2)
        CHECK_EQ(px[1], 0u);
        CHECK_EQ(px[2], 0u);
        CHECK_EQ(px[3], 255u);
    }
    return failures;
}

M2RIG_TEST(dds, decodes_dxt5_opaque_and_transparent) {
    int failures = 0;
    auto file = ddsHeader(4, 4, 0x35545844);
    // alpha0=255, alpha1=0 -> 8-step ramp; all alpha indices 0 (opaque).
    // color block: white vs black, all indices 0 (white).
    std::uint8_t block[16] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    file.insert(file.end(), block, block + 16);
    auto img = decodeDds(file.data(), file.size());
    CHECK_TRUE(img.succeeded());
    if (!img.succeeded()) return failures + 1;
    CHECK_TRUE(img.value().format == "DXT5");
    CHECK_FALSE(img.value().hasAlpha);
    const std::uint8_t* px = img.value().rgba.data();
    CHECK_EQ(px[0], 255u);
    CHECK_EQ(px[3], 255u);
    // Same block but alpha0=0/alpha1=255 branch with texel 0 at index 6
    // (table value 0) -> transparent.
    std::uint8_t block2[16] = {0x00, 0xFF, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<std::uint8_t> file2 = ddsHeader(4, 4, 0x35545844);
    file2.insert(file2.end(), block2, block2 + 16);
    auto img2 = decodeDds(file2.data(), file2.size());
    CHECK_TRUE(img2.succeeded());
    if (img2.succeeded()) {
        CHECK_TRUE(img2.value().hasAlpha);
        CHECK_EQ(img2.value().rgba[3], 0u);
    }
    return failures;
}

M2RIG_TEST(dds, reads_real_model_texture_header) {
    int failures = 0;
    // Any real Data/Models DDS validates header parsing end to end.
    std::filesystem::path dir = std::filesystem::current_path();
    std::filesystem::path dds;
    for (int level = 0; level < 5 && dds.empty(); ++level) {
        const std::filesystem::path cand = dir / "Data" / "Models";
        std::error_code ec;
        if (std::filesystem::exists(cand, ec)) {
            for (const auto& e : std::filesystem::directory_iterator(cand, ec)) {
                if (e.is_regular_file() && e.path().extension() == ".dds") {
                    dds = e.path();
                    break;
                }
            }
        }
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    if (dds.empty()) {
        printf("    SKIP real DDS test (no Data/Models)\n");
        return failures;
    }
    auto img = readDdsFile(dds.string());
    CHECK_TRUE(img.succeeded());
    if (img.succeeded()) {
        printf("    real dds: %s %ux%u %s alpha=%d\n", dds.filename().string().c_str(),
               img.value().width, img.value().height, img.value().format.c_str(),
               (int)img.value().hasAlpha);
        CHECK_TRUE(img.value().width > 0 && img.value().height > 0);
        CHECK_EQ(img.value().rgba.size(),
                 static_cast<std::size_t>(img.value().width) * img.value().height * 4u);
    }
    return failures;
}
