#pragma once
// Dependency-free DDS decoder (DXT1/BC1, DXT3/BC2, DXT5/BC3).
// Decodes the base mip level to straight RGBA8. Everything else (DX10 with
// non-BC1..3 formats, uncompressed pixel formats, cubemaps/volumes) fails
// explicitly instead of guessing.
#include <cstdint>
#include <string>
#include <vector>

#include "m2rig/result.hpp"

namespace m2rig {

struct DdsImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipCount = 0;  // as declared (only level 0 is decoded)
    std::string format;          // "DXT1", "DXT3" or "DXT5"
    bool hasAlpha = false;
    std::vector<std::uint8_t> rgba;  // width*height*4, row-major from top
};

// Decodes raw file bytes. The file is fully validated first (magic, header
// sizes, FourCC, data length for the base level); truncated data is an error.
Result<DdsImage> decodeDds(const std::uint8_t* data, std::size_t size,
                           const std::string& asset = "<dds>");

// Convenience: read + decode from disk.
Result<DdsImage> readDdsFile(const std::string& path, const std::string& asset = {});

}  // namespace m2rig
