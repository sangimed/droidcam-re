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
    [[nodiscard]] bool Decode(const std::vector<std::uint8_t>& annexB, DecodedFrame& frame,
                              std::uint32_t inputTimestamp90k = 0);
    [[nodiscard]] const wchar_t* Mode() const noexcept { return mode_; }
    [[nodiscard]] HRESULT LastDecodeStatus() const noexcept { return lastDecodeStatus_; }
    [[nodiscard]] std::uint32_t LastTransformTimeMs() const noexcept { return lastTransformTimeMs_; }
    [[nodiscard]] std::uint32_t LastConversionTimeMs() const noexcept { return lastConversionTimeMs_; }

private:
    [[nodiscard]] bool StartSoftware(UINT32 width, UINT32 height);
    IMFTransform* transform_{};
    UINT32 width_{};
    UINT32 height_{};
    UINT32 outputStride_{};
    UINT32 outputFrameWidth_{};
    UINT32 outputFrameHeight_{};
    bool outputIsNv12_{};
    bool started_{};
    HRESULT lastDecodeStatus_{S_OK};
    std::uint32_t lastTransformTimeMs_{};
    std::uint32_t lastConversionTimeMs_{};
    DWORD outputStreamFlags_{};
    const wchar_t* mode_{L"unavailable"};
};
}
