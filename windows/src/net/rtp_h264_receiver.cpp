#include "rtp_h264_receiver.h"

#include <array>
#include <chrono>
#include <objbase.h>
#include <utility>

namespace clone_net {
namespace {
constexpr std::size_t kMaxRtpDatagram = 1200;
constexpr std::size_t kRtpHeaderBytes = 12;
constexpr std::size_t kMaxAccessUnit = 8 * 1024 * 1024;
constexpr std::uint8_t kPayloadType = 96;
constexpr std::uint8_t kFuA = 28;

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}
}

RtpH264Receiver::RtpH264Receiver(SOCKET socket, std::uint32_t expectedSsrc,
                                 AccessUnitCallback onAccessUnit, KeyframeCallback onKeyframeNeeded,
                                 std::uint32_t expectedPeerAddress)
    : socket_(socket), expectedSsrc_(expectedSsrc), onAccessUnit_(std::move(onAccessUnit)),
      onKeyframeNeeded_(std::move(onKeyframeNeeded)), expectedPeerAddress_(expectedPeerAddress) {}

RtpH264Receiver::~RtpH264Receiver() { Stop(); }

void RtpH264Receiver::Start() {
    if (running_.exchange(true)) return;
    DWORD timeoutMs = 100;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    receiveThread_ = std::thread(&RtpH264Receiver::ReceiveLoop, this);
}

void RtpH264Receiver::Stop() {
    if (!running_.exchange(false)) return;
    if (receiveThread_.joinable() && receiveThread_.get_id() != std::this_thread::get_id()) receiveThread_.join();
    DropCurrentAccessUnit(false);
}

void RtpH264Receiver::ReceiveLoop() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::array<std::uint8_t, kMaxRtpDatagram + 1> buffer{};
    while (running_.load()) {
        sockaddr_in sender{};
        int senderSize = sizeof(sender);
        const int size = recvfrom(socket_, reinterpret_cast<char*>(buffer.data()),
                                  static_cast<int>(buffer.size()), 0,
                                  reinterpret_cast<sockaddr*>(&sender), &senderSize);
        if (size == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (!running_.load()) continue;
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                if ((!currentAccessUnit_.empty() || fuActive_) && lastPacketTime_.time_since_epoch().count() != 0 &&
                    std::chrono::steady_clock::now() - lastPacketTime_ > std::chrono::milliseconds(100)) {
                    DropCurrentAccessUnit(true);
                }
                continue;
            }
            break;
        }
        received_.fetch_add(1);
        if (size <= 0 || size > static_cast<int>(kMaxRtpDatagram)) {
            DropCurrentAccessUnit(true);
            continue;
        }
        if (expectedPeerAddress_ != 0 && sender.sin_addr.s_addr != expectedPeerAddress_) continue;
        lastPacketTime_ = std::chrono::steady_clock::now();
        ProcessDatagram(buffer.data(), static_cast<std::size_t>(size));
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
}

void RtpH264Receiver::ProcessDatagram(const std::uint8_t* data, std::size_t size) {
    if (size < kRtpHeaderBytes || (data[0] >> 6) != 2 || (data[1] & 0x7f) != kPayloadType) {
        DropCurrentAccessUnit(true);
        return;
    }
    const bool marker = (data[1] & 0x80) != 0;
    const std::uint16_t sequence = ReadU16(data + 2);
    const std::uint32_t timestamp = ReadU32(data + 4);
    const std::uint32_t ssrc = ReadU32(data + 8);
    if (ssrc != expectedSsrc_) {
        DropCurrentAccessUnit(true);
        return;
    }

    std::size_t headerSize = kRtpHeaderBytes + static_cast<std::size_t>(data[0] & 0x0f) * 4;
    if (headerSize > size) {
        DropCurrentAccessUnit(true);
        return;
    }
    if ((data[0] & 0x10) != 0) {
        if (headerSize + 4 > size) {
            DropCurrentAccessUnit(true);
            return;
        }
        const std::size_t extensionBytes = static_cast<std::size_t>(ReadU16(data + headerSize + 2)) * 4;
        headerSize += 4 + extensionBytes;
        if (headerSize > size) {
            DropCurrentAccessUnit(true);
            return;
        }
    }
    std::size_t payloadEnd = size;
    if ((data[0] & 0x20) != 0) {
        const std::size_t padding = data[size - 1];
        if (padding == 0 || padding > size - headerSize) {
            DropCurrentAccessUnit(true);
            return;
        }
        payloadEnd -= padding;
    }
    if (headerSize >= payloadEnd) {
        DropCurrentAccessUnit(true);
        return;
    }

    if (sequenceKnown_ && sequence != expectedSequence_) {
        DropCurrentAccessUnit(true);
        currentTimestamp_ = timestamp;
        timestampKnown_ = true;
        currentInvalid_ = true;
    }
    sequenceKnown_ = true;
    expectedSequence_ = static_cast<std::uint16_t>(sequence + 1);

    if (timestampKnown_ && timestamp != currentTimestamp_) {
        if (!currentAccessUnit_.empty() || fuActive_) DropCurrentAccessUnit(true);
        currentTimestamp_ = timestamp;
        timestampKnown_ = true;
    } else if (!timestampKnown_) {
        currentTimestamp_ = timestamp;
        timestampKnown_ = true;
    }

    if (currentInvalid_) {
        if (marker) currentInvalid_ = false;
        return;
    }

    const auto* payload = data + headerSize;
    const std::size_t payloadSize = payloadEnd - headerSize;
    const std::uint8_t nalType = payload[0] & 0x1f;
    if (nalType >= 1 && nalType <= 23) {
        if (fuActive_) {
            DropCurrentAccessUnit(true);
            return;
        }
        AppendNal(payload, payloadSize, nalType == 5);
    } else if (nalType == kFuA && payloadSize >= 3) {
        const std::uint8_t fuHeader = payload[1];
        const bool start = (fuHeader & 0x80) != 0;
        const bool end = (fuHeader & 0x40) != 0;
        const std::uint8_t type = fuHeader & 0x1f;
        if (start) {
            if (fuActive_) DropCurrentAccessUnit(true);
            fuActive_ = true;
            fuType_ = type;
            fuNal_.clear();
            fuNal_.reserve(payloadSize + 1024);
            fuNal_.push_back(0);
            fuNal_.push_back(0);
            fuNal_.push_back(0);
            fuNal_.push_back(1);
            fuNal_.push_back(static_cast<std::uint8_t>((payload[0] & 0xe0) | type));
        } else if (!fuActive_ || fuType_ != type) {
            DropCurrentAccessUnit(true);
            return;
        }
        fuNal_.insert(fuNal_.end(), payload + 2, payload + payloadSize);
        if (fuNal_.size() > kMaxAccessUnit) {
            DropCurrentAccessUnit(true);
            return;
        }
        if (end) {
            AppendNal(fuNal_.data() + 4, fuNal_.size() - 4, type == 5);
            fuActive_ = false;
            fuNal_.clear();
        }
    } else {
        DropCurrentAccessUnit(true);
        return;
    }

    if (marker) {
        if (fuActive_) {
            DropCurrentAccessUnit(true);
            return;
        }
        if (!currentAccessUnit_.empty()) {
            if (waitingForIdr_ && !currentHasIdr_) {
                DropCurrentAccessUnit(false);
            } else {
                waitingForIdr_ = false;
                keyframeRequestPending_ = false;
                onAccessUnit_(H264AccessUnit{std::move(currentAccessUnit_), currentTimestamp_, currentHasIdr_});
                currentAccessUnit_.clear();
                currentHasIdr_ = false;
                currentHasSps_ = false;
                currentHasPps_ = false;
            }
        }
    }
}

void RtpH264Receiver::AppendNal(const std::uint8_t* data, std::size_t size, bool idr) {
    if (size == 0 || currentAccessUnit_.size() + size + 4 > kMaxAccessUnit) {
        DropCurrentAccessUnit(true);
        return;
    }
    const std::uint8_t type = data[0] & 0x1f;
    if (type == 7) {
        cachedSps_ = {0, 0, 0, 1};
        cachedSps_.insert(cachedSps_.end(), data, data + size);
        currentHasSps_ = true;
    } else if (type == 8) {
        cachedPps_ = {0, 0, 0, 1};
        cachedPps_.insert(cachedPps_.end(), data, data + size);
        currentHasPps_ = true;
    }
    if (idr) {
        if (!currentHasSps_ && !cachedSps_.empty()) currentAccessUnit_.insert(currentAccessUnit_.end(), cachedSps_.begin(), cachedSps_.end());
        if (!currentHasPps_ && !cachedPps_.empty()) currentAccessUnit_.insert(currentAccessUnit_.end(), cachedPps_.begin(), cachedPps_.end());
    }
    currentAccessUnit_.insert(currentAccessUnit_.end(), {0, 0, 0, 1});
    currentAccessUnit_.insert(currentAccessUnit_.end(), data, data + size);
    currentHasIdr_ = currentHasIdr_ || idr;
    currentHasSps_ = currentHasSps_ || type == 7;
    currentHasPps_ = currentHasPps_ || type == 8;
}

void RtpH264Receiver::DropCurrentAccessUnit(bool requestKeyframe) {
    if (!currentAccessUnit_.empty() || fuActive_ || currentInvalid_) dropped_.fetch_add(1);
    currentAccessUnit_.clear();
    fuNal_.clear();
    fuActive_ = false;
    currentHasIdr_ = false;
    currentHasSps_ = false;
    currentHasPps_ = false;
    currentInvalid_ = false;
    if (requestKeyframe) {
        waitingForIdr_ = true;
        if (!keyframeRequestPending_) {
            keyframeRequestPending_ = true;
            onKeyframeNeeded_();
        }
    }
}
}
