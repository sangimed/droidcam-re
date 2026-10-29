#include "nv12_to_bgra.h"

#include <algorithm>
#include <limits>

namespace clone_video {

bool Nv12ToBgra(const std::uint8_t* nv12, std::size_t dataSize,
                std::uint32_t width, std::uint32_t height,
                std::size_t stride, std::vector<std::uint8_t>& bgra) {
    if (!nv12 || width == 0 || height == 0 || (width & 1) != 0 || (height & 1) != 0 ||
        stride < width || (stride & 1) != 0 ||
        height > (std::numeric_limits<std::size_t>::max)() / stride) {
        return false;
    }

    const std::size_t ySize = stride * height;
    const std::size_t uvRows = height / 2;
    if (uvRows > ((std::numeric_limits<std::size_t>::max)() - ySize) / stride) return false;
    const std::size_t requiredSize = ySize + stride * uvRows;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    if (pixelCount > (std::numeric_limits<std::size_t>::max)() / 4 || dataSize < requiredSize) return false;

    std::vector<std::uint8_t> converted(pixelCount * 4);
    const std::uint8_t* uvPlane = nv12 + ySize;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* yRow = nv12 + static_cast<std::size_t>(y) * stride;
        const std::uint8_t* uvRow = uvPlane + static_cast<std::size_t>(y / 2) * stride;
        for (std::uint32_t x = 0; x < width; ++x) {
            const int luminance = (std::max)(0, static_cast<int>(yRow[x]) - 16);
            const std::size_t chromaOffset = static_cast<std::size_t>(x & ~1u);
            const int blueDifference = static_cast<int>(uvRow[chromaOffset]) - 128;
            const int redDifference = static_cast<int>(uvRow[chromaOffset + 1]) - 128;
            const int scaledY = 298 * luminance;
            const std::size_t pixelOffset = (static_cast<std::size_t>(y) * width + x) * 4;
            converted[pixelOffset] = static_cast<std::uint8_t>(std::clamp(
                (scaledY + 541 * blueDifference + 128) >> 8, 0, 255));
            converted[pixelOffset + 1] = static_cast<std::uint8_t>(std::clamp(
                (scaledY - 55 * blueDifference - 136 * redDifference + 128) >> 8, 0, 255));
            converted[pixelOffset + 2] = static_cast<std::uint8_t>(std::clamp(
                (scaledY + 459 * redDifference + 128) >> 8, 0, 255));
            converted[pixelOffset + 3] = 255;
        }
    }

    bgra.swap(converted);
    return true;
}

}  // namespace clone_video
