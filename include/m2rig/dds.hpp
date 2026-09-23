#pragma once
// Dependency-free DDS decoder: BC1-3 (DXT1/DXT3/DXT5) + BC4/BC5-UNORM with ALL
// declared mip levels decoded to straight RGBA8 (Wave-27 Slice A + Slice B).
// BC4/BC5-SNORM + uncompressed stay explicit failures (SNORM: tested corpus is
// UNORM-only; uncompressed R8G8B8A8 Slice B2 needs bitmask parsing); DX10
// other formats, cubemaps/volumes fail explicitly instead of guessing.
#include <cstdint>
#include <string>
#include <vector>

#include "m2rig/result.hpp"

namespace m2rig {

struct DdsMipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;  // width*height*4, row-major from top
};

struct DdsImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipCount = 0;
    std::string format;          // "DXT1", "DXT3", "DXT5", "BC4", "BC5", "R8G8B8A8", etc.
    bool hasAlpha = false;
    std::vector<std::uint8_t> rgba;  // legacy mirror of mips[0].rgba (Wave-27 transition)
    std::vector<DdsMipLevel> mips;  // All mip levels, level 0 = base
};

// Decodes raw file bytes. The file is fully validated first (magic, header
// sizes, FourCC, data length for all mip levels); truncated data is an error.
Result<DdsImage> decodeDds(const std::uint8_t* data, std::size_t size,
                           const std::string& asset = "<dds>");

// Convenience: read + decode from disk.
Result<DdsImage> readDdsFile(const std::string& path, const std::string& asset = {});
}  // namespace m2rig
