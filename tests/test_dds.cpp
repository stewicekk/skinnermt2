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

M2RIG_TEST(dds, decodes_dxt1_8x8_two_mips) {
    int failures = 0;
    // Wave-27 Slice A: 8x8 base (2x2 blocks, solid red) + 4x4 mip1 (1 block,
    // solid green). Level sizes are bw*bh*8: 4*8=32 + 1*8=8.
    auto file = ddsHeader(8, 8, 0x31545844, 2);
    const std::uint8_t redBlock[8] = {0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};
    for (int b = 0; b < 4; ++b) file.insert(file.end(), redBlock, redBlock + 8);
    const std::uint8_t greenBlock[8] = {0xE0, 0x07, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00};
    file.insert(file.end(), greenBlock, greenBlock + 8);
    auto img = decodeDds(file.data(), file.size());
    CHECK_TRUE(img.succeeded());
    if (!img.succeeded()) return failures + 1;
    const DdsImage& im = img.value();
    CHECK_EQ(im.width, 8u);
    CHECK_EQ(im.height, 8u);
    CHECK_EQ(im.mipCount, 2u);
    CHECK_TRUE(im.format == "DXT1");
    CHECK_FALSE(im.hasAlpha);
    CHECK_EQ(im.mips.size(), static_cast<std::size_t>(2));
    if (im.mips.size() != 2) return failures + 1;
    CHECK_EQ(im.mips[0].width, 8u);
    CHECK_EQ(im.mips[0].height, 8u);
    CHECK_EQ(im.mips[0].rgba.size(), static_cast<std::size_t>(8 * 8 * 4));
    CHECK_EQ(im.mips[1].width, 4u);
    CHECK_EQ(im.mips[1].height, 4u);
    CHECK_EQ(im.mips[1].rgba.size(), static_cast<std::size_t>(4 * 4 * 4));
    // Level 0: every texel solid red (all four blocks use index 0 = 0xF800).
    for (std::size_t i : {static_cast<std::size_t>(0), static_cast<std::size_t>(32),
                          static_cast<std::size_t>(63)}) {
        const std::uint8_t* px = &im.mips[0].rgba[i * 4];
        CHECK_EQ(px[0], 255u);
        CHECK_EQ(px[1], 0u);
        CHECK_EQ(px[2], 0u);
        CHECK_EQ(px[3], 255u);
    }
    // Level 1: every texel solid green (index 0 = 0x07E0 -> 0,255,0).
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint8_t* px = &im.mips[1].rgba[i * 4];
        CHECK_EQ(px[0], 0u);
        CHECK_EQ(px[1], 255u);
        CHECK_EQ(px[2], 0u);
        CHECK_EQ(px[3], 255u);
    }
    // Legacy mirror: img.rgba is a byte-identical copy of mips[0].rgba.
    CHECK_EQ(im.rgba.size(), im.mips[0].rgba.size());
    if (im.rgba.size() == im.mips[0].rgba.size()) {
        for (std::size_t i = 0; i < im.rgba.size(); ++i) CHECK_EQ(im.rgba[i], im.mips[0].rgba[i]);
    }
    return failures;
}

M2RIG_TEST(dds, truncated_mip2_fails) {
    int failures = 0;
    // Same 8x8 2-mip header but the mip1 tail is missing -> explicit fail.
    auto baseOnly = ddsHeader(8, 8, 0x31545844, 2);
    const std::uint8_t redBlock[8] = {0x00, 0xF8, 0xE0, 0x07, 0x00, 0x00, 0x00, 0x00};
    for (int b = 0; b < 4; ++b) baseOnly.insert(baseOnly.end(), redBlock, redBlock + 8);
    CHECK_FALSE(decodeDds(baseOnly.data(), baseOnly.size()).succeeded());
    // Partial tail (4 of 8 mip1 bytes) is also truncated, not "best effort".
    std::vector<std::uint8_t> partial = baseOnly;
    const std::uint8_t half[4] = {0xE0, 0x07, 0x00, 0xF8};
    partial.insert(partial.end(), half, half + 4);
    CHECK_FALSE(decodeDds(partial.data(), partial.size()).succeeded());
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

namespace {
// Slice B helpers (appended; existing ddsHeader above untouched).
std::vector<std::uint8_t> packBc4Block(std::uint8_t a0, std::uint8_t a1,
                                       const std::uint8_t idx[16]) {
    std::vector<std::uint8_t> blk(8, 0);
    blk[0] = a0;
    blk[1] = a1;
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        bits |= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(idx[i] & 7))
                 << (3 * i));
    }
    for (int b = 0; b < 6; ++b) {
        blk[static_cast<std::size_t>(2 + b)] =
            static_cast<std::uint8_t>((bits >> (8 * b)) & 0xFF);
    }
    return blk;
}

std::vector<std::uint8_t> ddsHeaderDx10(std::uint32_t w, std::uint32_t h, std::uint32_t dxgi,
                                        std::uint32_t mips = 1) {
    std::vector<std::uint8_t> out(148, 0);
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
    u32(4 + 80, 0x30315844);
    u32(128, dxgi);
    u32(132, 3);
    u32(140, 1);
    return out;
}
}  // namespace

M2RIG_TEST(dds, decodes_bc4_ramp) {
    int failures = 0;
    // Slice B: 4x4 BC4 (ATI1) single block, texel i uses palette index i%8.
    const std::uint8_t kIdx[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7};
    // a0=255/a1=0 branch: (6*255)/7=218, (5*255)/7=182, (4*255)/7=145,
    // (3*255)/7=109, (2*255)/7=72, 255/7=36.
    const std::uint8_t kHi[8] = {255, 0, 218, 182, 145, 109, 72, 36};
    // a0=0/a1=255 branch: 255/5=51, 510/5=102, 765/5=153, 1020/5=204, 0, 255.
    const std::uint8_t kLo[8] = {0, 255, 51, 102, 153, 204, 0, 255};
    const std::uint32_t kAti1 = 0x31495441;
    auto blkHi = packBc4Block(255, 0, kIdx);
    auto file = ddsHeader(4, 4, kAti1);
    file.insert(file.end(), blkHi.begin(), blkHi.end());
    auto img = decodeDds(file.data(), file.size());
    CHECK_TRUE(img.succeeded());
    if (!img.succeeded()) return failures + 1;
    const DdsImage& im = img.value();
    CHECK_TRUE(im.format == "BC4");
    CHECK_EQ(im.width, 4u);
    CHECK_EQ(im.height, 4u);
    CHECK_FALSE(im.hasAlpha);
    CHECK_EQ(im.mips.size(), static_cast<std::size_t>(1));
    CHECK_EQ(im.rgba.size(), static_cast<std::size_t>(4 * 4 * 4));
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint8_t* px = &im.rgba[i * 4];
        CHECK_EQ(px[0], kHi[i % 8]);
        CHECK_EQ(px[1], 0u);
        CHECK_EQ(px[2], 0u);
        CHECK_EQ(px[3], 255u);
    }
    // a0<=a1 branch pins the 0/255 codes (index 6 -> 0, index 7 -> 255).
    auto blkLo = packBc4Block(0, 255, kIdx);
    std::vector<std::uint8_t> fileLo = ddsHeader(4, 4, kAti1);
    fileLo.insert(fileLo.end(), blkLo.begin(), blkLo.end());
    auto imgLo = decodeDds(fileLo.data(), fileLo.size());
    CHECK_TRUE(imgLo.succeeded());
    if (imgLo.succeeded()) {
        CHECK_TRUE(imgLo.value().format == "BC4");
        for (std::size_t i = 0; i < 16; ++i) {
            const std::uint8_t* px = &imgLo.value().rgba[i * 4];
            CHECK_EQ(px[0], kLo[i % 8]);
            CHECK_EQ(px[1], 0u);
            CHECK_EQ(px[2], 0u);
            CHECK_EQ(px[3], 255u);
        }
    }
    // DX10 BC4_UNORM (80) decodes byte-identically to the ATI1 block.
    std::vector<std::uint8_t> fileDx = ddsHeaderDx10(4, 4, 80);
    fileDx.insert(fileDx.end(), blkHi.begin(), blkHi.end());
    auto imgDx = decodeDds(fileDx.data(), fileDx.size());
    CHECK_TRUE(imgDx.succeeded());
    if (imgDx.succeeded() && img.succeeded()) {
        CHECK_TRUE(imgDx.value().format == "BC4");
        CHECK_EQ(imgDx.value().rgba.size(), im.rgba.size());
        if (imgDx.value().rgba.size() == im.rgba.size()) {
            for (std::size_t i = 0; i < im.rgba.size(); ++i)
                CHECK_EQ(imgDx.value().rgba[i], im.rgba[i]);
        }
    }
    return failures;
}

M2RIG_TEST(dds, decodes_bc5_rg) {
    int failures = 0;
    // Slice B: 4x4 BC5 (ATI2) = R BC4 + G BC4. R uses the a0>a1 ramp with
    // texel i at index i%8; G uses the a0<=a1 ramp at index 7-(i%8).
    const std::uint8_t kRIdx[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7};
    const std::uint8_t kGIdx[16] = {7, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 0};
    const std::uint8_t kR[8] = {255, 0, 218, 182, 145, 109, 72, 36};
    const std::uint8_t kG[8] = {0, 255, 51, 102, 153, 204, 0, 255};
    const std::uint32_t kAti2 = 0x32495441;
    const std::uint32_t kBc5u = 0x55354342;
    auto rBlk = packBc4Block(255, 0, kRIdx);
    auto gBlk = packBc4Block(0, 255, kGIdx);
    std::vector<std::uint8_t> blk16;
    blk16.insert(blk16.end(), rBlk.begin(), rBlk.end());
    blk16.insert(blk16.end(), gBlk.begin(), gBlk.end());
    auto file = ddsHeader(4, 4, kAti2);
    file.insert(file.end(), blk16.begin(), blk16.end());
    auto img = decodeDds(file.data(), file.size());
    CHECK_TRUE(img.succeeded());
    if (!img.succeeded()) return failures + 1;
    const DdsImage& im = img.value();
    CHECK_TRUE(im.format == "BC5");
    CHECK_FALSE(im.hasAlpha);
    CHECK_EQ(im.rgba.size(), static_cast<std::size_t>(4 * 4 * 4));
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint8_t* px = &im.rgba[i * 4];
        CHECK_EQ(px[0], kR[i % 8]);
        CHECK_EQ(px[1], kG[7 - (i % 8)]);
        CHECK_EQ(px[2], 0u);
        CHECK_EQ(px[3], 255u);
    }
    // BC5U alias FourCC decodes byte-identically.
    std::vector<std::uint8_t> fileAlias = ddsHeader(4, 4, kBc5u);
    fileAlias.insert(fileAlias.end(), blk16.begin(), blk16.end());
    auto imgAlias = decodeDds(fileAlias.data(), fileAlias.size());
    CHECK_TRUE(imgAlias.succeeded());
    if (imgAlias.succeeded()) {
        CHECK_TRUE(imgAlias.value().format == "BC5");
        CHECK_EQ(imgAlias.value().rgba.size(), im.rgba.size());
        if (imgAlias.value().rgba.size() == im.rgba.size()) {
            for (std::size_t i = 0; i < im.rgba.size(); ++i)
                CHECK_EQ(imgAlias.value().rgba[i], im.rgba[i]);
        }
    }
    // DX10 BC5_UNORM (83) decodes byte-identically.
    std::vector<std::uint8_t> fileDx = ddsHeaderDx10(4, 4, 83);
    fileDx.insert(fileDx.end(), blk16.begin(), blk16.end());
    auto imgDx = decodeDds(fileDx.data(), fileDx.size());
    CHECK_TRUE(imgDx.succeeded());
    if (imgDx.succeeded()) {
        CHECK_TRUE(imgDx.value().format == "BC5");
        CHECK_EQ(imgDx.value().rgba.size(), im.rgba.size());
        if (imgDx.value().rgba.size() == im.rgba.size()) {
            for (std::size_t i = 0; i < im.rgba.size(); ++i)
                CHECK_EQ(imgDx.value().rgba[i], im.rgba[i]);
        }
    }
    return failures;
}

M2RIG_TEST(dds, rejects_bc_snorm_dxgi) {
    int failures = 0;
    // SNORM (81 = BC4_SNORM, 84 = BC5_SNORM) stays explicit-fail: the tested
    // corpus is UNORM-only and SNORM needs a signed (-1..1) remap.
    std::vector<std::uint8_t> file81 = ddsHeaderDx10(4, 4, 81);
    file81.resize(148 + 16, 0);
    CHECK_FALSE(decodeDds(file81.data(), file81.size()).succeeded());
    std::vector<std::uint8_t> file84 = ddsHeaderDx10(4, 4, 84);
    file84.resize(148 + 16, 0);
    CHECK_FALSE(decodeDds(file84.data(), file84.size()).succeeded());
    return failures;
}
