#include "session_protocol.h"

#include <algorithm>

namespace clone_protocol {
namespace {
std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

bool ValidateRecord(std::span<const std::uint8_t> record, std::uint32_t expected,
                    std::string& error) {
    if (record.size() < 6 || record.size() > kMaxRecordLength + 4) {
        error = "invalid record size";
        return false;
    }
    const auto length = ReadU32(record.data());
    if (length < 2 || length > kMaxRecordLength || length != expected ||
        record.size() != static_cast<std::size_t>(length) + 4) {
        error = "invalid record length";
        return false;
    }
    if (record[4] != kVersion) {
        error = "unsupported protocol version";
        return false;
    }
    return true;
}
}

bool ParseHelloRecord(std::span<const std::uint8_t> record, Hello& hello, std::string& error) {
    constexpr std::uint32_t kHelloRecordLength = 29;
    if (!ValidateRecord(record, kHelloRecordLength, error)) return false;
    if (record[5] != static_cast<std::uint8_t>(MessageType::Hello)) {
        error = "expected HELLO";
        return false;
    }
    std::copy_n(record.begin() + 6, hello.sessionId.size(), hello.sessionId.begin());
    hello.width = ReadU16(record.data() + 22);
    hello.height = ReadU16(record.data() + 24);
    hello.fps = record[26];
    hello.codec = record[27];
    hello.payloadType = record[28];
    hello.ssrc = ReadU32(record.data() + 29);
    const bool supportedSize = (hello.width == 1280 && hello.height == 720) ||
                               (hello.width == 1920 && hello.height == 1080);
    if (!supportedSize ||
        (hello.fps != 30 && hello.fps != 60) || hello.codec != 1 ||
        hello.payloadType != kH264PayloadType) {
        error = "unsupported profile";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> EncodeAcceptRecord(const Accept& accept) {
    std::vector<std::uint8_t> out;
    out.reserve(26);
    WriteU32(out, 22);
    out.push_back(kVersion);
    out.push_back(static_cast<std::uint8_t>(MessageType::Accept));
    out.insert(out.end(), accept.sessionId.begin(), accept.sessionId.end());
    WriteU16(out, accept.videoPort);
    WriteU16(out, accept.maxRtpPayload);
    return out;
}

bool ParseSessionMessage(std::span<const std::uint8_t> record, MessageType expectedType,
                         std::array<std::uint8_t, 16>& sessionId, std::string& error) {
    constexpr std::uint32_t kSessionRecordLength = 18;
    if (!ValidateRecord(record, kSessionRecordLength, error)) return false;
    if (record[5] != static_cast<std::uint8_t>(expectedType)) {
        error = "unexpected message type";
        return false;
    }
    std::copy_n(record.begin() + 6, sessionId.size(), sessionId.begin());
    return true;
}
}
