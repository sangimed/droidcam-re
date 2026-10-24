#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace clone_protocol {
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint32_t kMaxRecordLength = 4096;
inline constexpr std::uint16_t kMaxRtpPayload = 1200;
inline constexpr std::uint8_t kH264PayloadType = 96;
inline constexpr std::uint16_t kControlPort = 48200;

enum class MessageType : std::uint8_t {
    Hello = 1,
    Accept = 2,
    Reject = 3,
    KeyframeRequest = 4,
    Close = 5,
};

struct Hello {
    std::array<std::uint8_t, 16> sessionId{};
    std::uint16_t width{};
    std::uint16_t height{};
    std::uint8_t fps{};
    std::uint8_t codec{};
    std::uint8_t payloadType{};
    std::uint32_t ssrc{};
};

struct Accept {
    std::array<std::uint8_t, 16> sessionId{};
    std::uint16_t videoPort{};
    std::uint16_t maxRtpPayload{};
};

[[nodiscard]] bool ParseHelloRecord(
    std::span<const std::uint8_t> record, Hello& hello, std::string& error);
[[nodiscard]] std::vector<std::uint8_t> EncodeAcceptRecord(const Accept& accept);
[[nodiscard]] bool ParseSessionMessage(
    std::span<const std::uint8_t> record, MessageType expectedType,
    std::array<std::uint8_t, 16>& sessionId, std::string& error);
}
