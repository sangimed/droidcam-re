#include "video/nv12_to_bgra.h"

#include <array>
#include <cstdint>
#include <vector>

#include "test_assertions.h"

int main() {
    std::array<std::uint8_t, 6> nv12{63, 63, 63, 63, 102, 240};
    std::vector<std::uint8_t> bgra;

    Check(clone_video::Nv12ToBgra(nv12.data(), nv12.size(), 2, 2, 2, bgra));
    Check((bgra == std::vector<std::uint8_t>{0, 1, 255, 255, 0, 1, 255, 255,
                                              0, 1, 255, 255, 0, 1, 255, 255}));

    const auto previous = bgra;
    Check(!clone_video::Nv12ToBgra(nv12.data(), nv12.size(), 3, 2, 2, bgra));
    Check(bgra == previous);
    Check(!clone_video::Nv12ToBgra(nv12.data(), nv12.size() - 1, 2, 2, 2, bgra));
    Check(bgra == previous);
}
