#pragma once

#include <Windows.h>
#include <cstdint>
#include <mftransform.h>
#include <mutex>
#include <vector>

namespace clone_video {
struct DecodedFrame {
    UINT32 width{};
    UINT32 height{};
    std::vector<std::uint8_t> bgra;
};

// Synchronous H.264 Media Foundation transform. Calls must be serialized.
class H264Decoder final {
public:
    H264Decoder() = default;
    ~H264Decoder();
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;

    [[nodiscard]] bool Start(UINT32 width, UINT32 height);
    void Stop();
    [[nodiscard]] bool Decode(const std::vector<std::uint8_t>& annexB, DecodedFrame& frame);

private:
    [[nodiscard]] bool StartSoftware(UINT32 width, UINT32 height);
    IMFTransform* transform_{};
    UINT32 width_{};
    UINT32 height_{};
    bool started_{};
};
}
