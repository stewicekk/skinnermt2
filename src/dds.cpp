// Dependency-free DDS (DXT1/BC1, DXT3/BC2, DXT5/BC3, BC4, BC5-UNORM)
// multi-mip decoder (Wave-27 Slice A + Slice B). Layout reference: Microsoft
// DDS_HEADER / DDS_PIXELFORMAT docs (offsets verified by research agent).
#include "m2rig/dds.hpp"

#include <cstring>
#include <fstream>

namespace m2rig {

namespace {

std::uint32_t readU32LE(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

Rgb expand565(std::uint16_t v) {
    const std::uint8_t r5 = static_cast<std::uint8_t>((v >> 11) & 31);
    const std::uint8_t g6 = static_cast<std::uint8_t>((v >> 5) & 63);
    const std::uint8_t b5 = static_cast<std::uint8_t>(v & 31);
    return {static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
            static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2))};
}

// Decodes the 8-byte color block at p into 16 RGBA texels (row-major).
// fourColor selects DXT1-4color mode vs DXT1-1bit-alpha mode.
void decodeColorBlock(const std::uint8_t* p, bool fourColor, std::uint8_t out[16][4]) {
    const std::uint16_t c0 =
        static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
    const std::uint16_t c1 =
        static_cast<std::uint16_t>(p[2] | (static_cast<std::uint16_t>(p[3]) << 8));
    const Rgb e0 = expand565(c0);
    const Rgb e1 = expand565(c1);
    Rgb pal[4] = {e0, e1, {0, 0, 0}, {0, 0, 0}};
    std::uint8_t alpha[4] = {255, 255, 255, 255};
    if (fourColor || c0 > c1) {
        pal[2] = {static_cast<std::uint8_t>((2 * e0.r + e1.r) / 3),
                  static_cast<std::uint8_t>((2 * e0.g + e1.g) / 3),
                  static_cast<std::uint8_t>((2 * e0.b + e1.b) / 3)};
        pal[3] = {static_cast<std::uint8_t>((e0.r + 2 * e1.r) / 3),
                  static_cast<std::uint8_t>((e0.g + 2 * e1.g) / 3),
                  static_cast<std::uint8_t>((e0.b + 2 * e1.b) / 3)};
    } else {
        pal[2] = {static_cast<std::uint8_t>((e0.r + e1.r) / 2),
                  static_cast<std::uint8_t>((e0.g + e1.g) / 2),
                  static_cast<std::uint8_t>((e0.b + e1.b) / 2)};
        alpha[3] = 0;
    }
    const std::uint32_t bits =
        static_cast<std::uint32_t>(p[4]) | (static_cast<std::uint32_t>(p[5]) << 8) |
        (static_cast<std::uint32_t>(p[6]) << 16) | (static_cast<std::uint32_t>(p[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        const int idx = static_cast<int>((bits >> (2 * i)) & 3);
        out[i][0] = pal[idx].r;
        out[i][1] = pal[idx].g;
        out[i][2] = pal[idx].b;
        out[i][3] = alpha[idx];
    }
}

std::uint64_t alphaBits48(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 5; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

// Slice B: BC4 single-channel palette, bit-identical to the DXT5-alpha ramp
// above (both a0>a1 8-step and a0<=a1 6-step + 0/255 branches).
void decodeBc4Palette(std::uint8_t a0, std::uint8_t a1, std::uint8_t pal[8]) {
    pal[0] = a0;
    pal[1] = a1;
    if (a0 > a1) {
        pal[2] = static_cast<std::uint8_t>((6 * a0 + a1) / 7);
        pal[3] = static_cast<std::uint8_t>((5 * a0 + 2 * a1) / 7);
        pal[4] = static_cast<std::uint8_t>((4 * a0 + 3 * a1) / 7);
        pal[5] = static_cast<std::uint8_t>((3 * a0 + 4 * a1) / 7);
        pal[6] = static_cast<std::uint8_t>((2 * a0 + 5 * a1) / 7);
        pal[7] = static_cast<std::uint8_t>((a0 + 6 * a1) / 7);
    } else {
        pal[2] = static_cast<std::uint8_t>((4 * a0 + a1) / 5);
        pal[3] = static_cast<std::uint8_t>((3 * a0 + 2 * a1) / 5);
        pal[4] = static_cast<std::uint8_t>((2 * a0 + 3 * a1) / 5);
        pal[5] = static_cast<std::uint8_t>((a0 + 4 * a1) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
}

// Decodes one 8-byte BC4 block at blk into 16 channel values.
void decodeBc4Values(const std::uint8_t* blk, std::uint8_t vals[16]) {
    std::uint8_t pal[8];
    decodeBc4Palette(blk[0], blk[1], pal);
    const std::uint64_t bits = alphaBits48(blk + 2);
    for (int i = 0; i < 16; ++i) {
        const int idx = static_cast<int>((bits >> (3 * i)) & 7);
        vals[i] = pal[idx];
    }
}

// Decodes one BC4 (8B/block) or BC5 (16B/block = R BC4 + G BC4) level at src
// (exactly bw*bh*blockBytes bytes) into straight RGBA8 (w*h*4) with
// R=x, G=(BC5 ? y : 0), B=0, A=255. Partial edge blocks skip out-of-image
// texels, mirroring decodeBcLevel. Always opaque (no levelAlpha).
void decodeBc45Level(const std::uint8_t* src, std::uint32_t w, std::uint32_t h, bool isBc5,
                     std::vector<std::uint8_t>& outRgba) {
    outRgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    const std::size_t bw = (static_cast<std::size_t>(w) + 3) / 4;
    const std::size_t bh = (static_cast<std::size_t>(h) + 3) / 4;
    const std::size_t blockBytes = isBc5 ? 16 : 8;
    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint8_t* blk = src + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
            std::uint8_t rVals[16];
            std::uint8_t gVals[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            decodeBc4Values(blk, rVals);
            if (isBc5) decodeBc4Values(blk + 8, gVals);
            for (int i = 0; i < 16; ++i) {
                const int ty = static_cast<int>(by) * 4 + i / 4;
                const int tx = static_cast<int>(bx) * 4 + i % 4;
                if (tx >= static_cast<int>(w) || ty >= static_cast<int>(h)) continue;
                std::uint8_t* dst =
                    &outRgba[(static_cast<std::size_t>(ty) * w + static_cast<std::size_t>(tx)) * 4];
                dst[0] = rVals[i];
                dst[1] = isBc5 ? gVals[i] : 0;
                dst[2] = 0;
                dst[3] = 255;
            }
        }
    }
}

// Decodes one BC1-3 level at src (exactly bw*bh*blockBytes bytes) into
// straight RGBA8 (w*h*4). Out-of-image texels on partial edge blocks
// (<4x4 tails like 7x5) are skipped. Sets levelAlpha when any texel is
// not fully opaque.
void decodeBcLevel(const std::uint8_t* src, std::uint32_t w, std::uint32_t h,
                   std::size_t blockBytes, bool dxt3, bool dxt5,
                   std::vector<std::uint8_t>& outRgba, bool& levelAlpha) {
    outRgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    levelAlpha = false;
    const std::size_t bw = (static_cast<std::size_t>(w) + 3) / 4;
    const std::size_t bh = (static_cast<std::size_t>(h) + 3) / 4;
    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint8_t* blk = src + (static_cast<std::size_t>(by) * bw + bx) * blockBytes;
            const std::uint8_t* colorBlk = blk;
            if (dxt3) {
                for (int i = 0; i < 16; ++i) {
                    const int ty = static_cast<int>(by) * 4 + i / 4;
                    const int tx = static_cast<int>(bx) * 4 + i % 4;
                    if (tx >= static_cast<int>(w) || ty >= static_cast<int>(h)) continue;
                    const std::uint8_t nib =
                        (i % 2 == 0) ? static_cast<std::uint8_t>(blk[i / 2] & 0xF)
                                     : static_cast<std::uint8_t>(blk[i / 2] >> 4);
                    outRgba[(static_cast<std::size_t>(ty) * w + static_cast<std::size_t>(tx)) * 4 +
                            3] = static_cast<std::uint8_t>(nib * 17);
                    levelAlpha = true;
                }
                colorBlk = blk + 8;
            } else if (dxt5) {
                const std::uint8_t a0 = blk[0], a1 = blk[1];
                std::uint8_t aVal[8];
                aVal[0] = a0;
                aVal[1] = a1;
                if (a0 > a1) {
                    aVal[2] = static_cast<std::uint8_t>((6 * a0 + a1) / 7);
                    aVal[3] = static_cast<std::uint8_t>((5 * a0 + 2 * a1) / 7);
                    aVal[4] = static_cast<std::uint8_t>((4 * a0 + 3 * a1) / 7);
                    aVal[5] = static_cast<std::uint8_t>((3 * a0 + 4 * a1) / 7);
                    aVal[6] = static_cast<std::uint8_t>((2 * a0 + 5 * a1) / 7);
                    aVal[7] = static_cast<std::uint8_t>((a0 + 6 * a1) / 7);
                } else {
                    aVal[2] = static_cast<std::uint8_t>((4 * a0 + a1) / 5);
                    aVal[3] = static_cast<std::uint8_t>((3 * a0 + 2 * a1) / 5);
                    aVal[4] = static_cast<std::uint8_t>((2 * a0 + 3 * a1) / 5);
                    aVal[5] = static_cast<std::uint8_t>((a0 + 4 * a1) / 5);
                    aVal[6] = 0;
                    aVal[7] = 255;
                }
                const std::uint64_t bits = alphaBits48(blk + 2);
                for (int i = 0; i < 16; ++i) {
                    const int ty = static_cast<int>(by) * 4 + i / 4;
                    const int tx = static_cast<int>(bx) * 4 + i % 4;
                    if (tx >= static_cast<int>(w) || ty >= static_cast<int>(h)) continue;
                    const int idx = static_cast<int>((bits >> (3 * i)) & 7);
                    outRgba[(static_cast<std::size_t>(ty) * w + static_cast<std::size_t>(tx)) * 4 +
                            3] = aVal[idx];
                    if (aVal[idx] != 255) levelAlpha = true;
                }
                colorBlk = blk + 8;
            }
            std::uint8_t px[16][4];
            // DXT1 1-bit-alpha mode applies only to plain DXT1 blocks.
            decodeColorBlock(colorBlk, dxt3 || dxt5, px);
            for (int i = 0; i < 16; ++i) {
                const int ty = static_cast<int>(by) * 4 + i / 4;
                const int tx = static_cast<int>(bx) * 4 + i % 4;
                if (tx >= static_cast<int>(w) || ty >= static_cast<int>(h)) continue;
                std::uint8_t* dst =
                    &outRgba[(static_cast<std::size_t>(ty) * w + static_cast<std::size_t>(tx)) * 4];
                dst[0] = px[i][0];
                dst[1] = px[i][1];
                dst[2] = px[i][2];
                if (!dxt3 && !dxt5) {
                    dst[3] = px[i][3];
                    if (px[i][3] != 255) levelAlpha = true;
                }
            }
        }
    }
}

}  // namespace

Result<DdsImage> decodeDds(const std::uint8_t* data, std::size_t size,
                           const std::string& asset) {
    if (!data || size < 128)
        return Result<DdsImage>::fail("File too small for a DDS header.", "FORMAT", asset,
                                      "dds.decode");
    if (std::memcmp(data, "DDS ", 4) != 0)
        return Result<DdsImage>::fail("Missing DDS magic.", "FORMAT", asset, "dds.decode");
    const std::uint8_t* h = data + 4;
    if (readU32LE(h) != 124)
        return Result<DdsImage>::fail("Bad DDS header size.", "FORMAT", asset, "dds.decode");
    const std::uint32_t height = readU32LE(h + 8);
    const std::uint32_t width = readU32LE(h + 12);
    std::uint32_t mips = readU32LE(h + 24);
    if (mips == 0) mips = 1;
    if (width == 0 || height == 0 || width > 16384 || height > 16384 || mips > 32)
        return Result<DdsImage>::fail("Implausible DDS dimensions.", "FORMAT", asset,
                                      "dds.decode");
    const std::uint8_t* pf = h + 72;
    if (readU32LE(pf) != 32)
        return Result<DdsImage>::fail("Bad DDS pixel-format size.", "FORMAT", asset, "dds.decode");
    const std::uint32_t pfFlags = readU32LE(pf + 4);
    const std::uint32_t fourCC = readU32LE(pf + 8);

    std::size_t dataOff = 128;
    std::string format;
    std::size_t blockBytes = 0;
    bool dxt3 = false, dxt5 = false;
    bool bc4 = false, bc5 = false;
    if ((pfFlags & 0x4) != 0) {
        if (fourCC == 0x31545844) {
            format = "DXT1";
            blockBytes = 8;
        } else if (fourCC == 0x33545844 || fourCC == 0x32545844) {
            format = "DXT3";
            blockBytes = 16;
            dxt3 = true;
        } else if (fourCC == 0x35545844 || fourCC == 0x34545844) {
            format = "DXT5";
            blockBytes = 16;
            dxt5 = true;
        } else if (fourCC == 0x31495441) {
            // ATI1 / BC4U UNORM single-channel (Slice B).
            format = "BC4";
            blockBytes = 8;
            bc4 = true;
        } else if (fourCC == 0x32495441 || fourCC == 0x55354342) {
            // ATI2 ("ATI2") / BC5U ("BC5U") UNORM dual-channel (Slice B).
            format = "BC5";
            blockBytes = 16;
            bc5 = true;
        } else if (fourCC == 0x30315844) {
            // DX10 extension: BC1-3 + BC4/BC5-UNORM (Slice B).
            if (size < 148)
                return Result<DdsImage>::fail("Truncated DX10 header.", "FORMAT", asset,
                                              "dds.decode");
            const std::uint32_t dxgi = readU32LE(data + 128);
            dataOff = 148;
            if (dxgi == 71 || dxgi == 72) {
                format = "DXT1";
                blockBytes = 8;
            } else if (dxgi == 74 || dxgi == 75) {
                format = "DXT3";
                blockBytes = 16;
                dxt3 = true;
            } else if (dxgi == 77 || dxgi == 78) {
                format = "DXT5";
                blockBytes = 16;
                dxt5 = true;
            } else if (dxgi == 80) {
                // DXGI_FORMAT_BC4_UNORM (Slice B).
                format = "BC4";
                blockBytes = 8;
                bc4 = true;
            } else if (dxgi == 83) {
                // DXGI_FORMAT_BC5_UNORM (Slice B).
                format = "BC5";
                blockBytes = 16;
                bc5 = true;
            } else {
                // BC4_SNORM (81) / BC5_SNORM (84) stay explicit-fail: the
                // tested corpus is UNORM-only and SNORM needs a signed
                // (-1..1) remap, not the UNORM ramp above.
                return Result<DdsImage>::fail("Unsupported DXGI format in DX10 header.", "FORMAT",
                                              asset, "dds.decode");
            }
        } else {
            return Result<DdsImage>::fail("Unsupported DDS FourCC.", "FORMAT", asset, "dds.decode");
        }
    } else {
        return Result<DdsImage>::fail("Only block-compressed DDS is supported.", "FORMAT", asset,
                                      "dds.decode");
    }

    // Wave 27 Slice A + Slice B: decode ALL declared BC1-3 + BC4/BC5-UNORM
    // mip levels. Each level halves (floor, min 1 per axis); every level
    // length is validated and a truncated tail is an explicit failure.
    // Uncompressed R8G8B8A8 stays an explicit failure (Slice B2: needs
    // pixel-format bitmask parsing, separate review).
    DdsImage img;
    img.width = width;
    img.height = height;
    img.mipCount = mips;
    img.format = format;
    img.mips.reserve(mips);
    bool alpha = false;
    std::size_t offset = dataOff;
    std::uint32_t lw = width;
    std::uint32_t lh = height;
    for (std::uint32_t level = 0; level < mips; ++level) {
        const std::size_t bw = (static_cast<std::size_t>(lw) + 3) / 4;
        const std::size_t bh = (static_cast<std::size_t>(lh) + 3) / 4;
        const std::size_t levelBytes = bw * bh * blockBytes;
        if (offset + levelBytes > size) {
            if (level == 0)
                return Result<DdsImage>::fail("Truncated DDS base level.", "FORMAT", asset,
                                              "dds.decode");
            return Result<DdsImage>::fail(
                "Truncated DDS mip level " + std::to_string(level) + " (" +
                    std::to_string(lw) + "x" + std::to_string(lh) + ").",
                "FORMAT", asset, "dds.decode");
        }
        DdsMipLevel out;
        out.width = lw;
        out.height = lh;
        bool levelAlpha = false;
        if (bc4 || bc5) {
            decodeBc45Level(data + offset, lw, lh, bc5, out.rgba);
        } else {
            decodeBcLevel(data + offset, lw, lh, blockBytes, dxt3, dxt5, out.rgba, levelAlpha);
        }
        if (levelAlpha) alpha = true;
        offset += levelBytes;
        img.mips.push_back(std::move(out));
        if (lw > 1) lw /= 2;
        if (lh > 1) lh /= 2;
        // Both already 1: further levels stay 1x1 (clamped tail).
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
    }
    img.hasAlpha = alpha;
    // Legacy mirror: pre-Wave-27 callers read img.rgba for the base level.
    img.rgba = img.mips[0].rgba;
    return Result<DdsImage>::ok(std::move(img));
}

Result<DdsImage> readDdsFile(const std::string& path, const std::string& asset) {
    std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return Result<DdsImage>::fail("Cannot open DDS file: " + path, "IO",
                                      asset.empty() ? path : asset, "dds.read");
    const std::streamsize size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > 512ULL * 1024ULL * 1024ULL)
        return Result<DdsImage>::fail("Suspicious DDS file size.", "FORMAT",
                                      asset.empty() ? path : asset, "dds.read");
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        return Result<DdsImage>::fail("Failed reading DDS file: " + path, "IO",
                                      asset.empty() ? path : asset, "dds.read");
    return decodeDds(data.data(), data.size(), asset.empty() ? path : asset);
}

}  // namespace m2rig
