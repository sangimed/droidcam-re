#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace clone_video {

[[nodiscard]] bool Nv12ToBgra(const std::uint8_t* nv12, std::size_t dataSize,
                              std::uint32_t width, std::uint32_t height,
                              std::size_t stride, std::vector<std::uint8_t>& bgra);

}  // namespace clone_video
