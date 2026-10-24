#pragma once

#include <Winsock2.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace clone_net {
struct H264AccessUnit {
    std::vector<std::uint8_t> annexB;
    std::uint32_t timestamp90k{};
    bool hasIdr{};
};

class RtpH264Receiver final {
public:
    using AccessUnitCallback = std::function<void(H264AccessUnit)>;
    using KeyframeCallback = std::function<void()>;

    RtpH264Receiver(SOCKET socket, std::uint32_t expectedSsrc,
                    AccessUnitCallback onAccessUnit, KeyframeCallback onKeyframeNeeded,
                    std::uint32_t expectedPeerAddress = 0);
    ~RtpH264Receiver();
    RtpH264Receiver(const RtpH264Receiver&) = delete;
    RtpH264Receiver& operator=(const RtpH264Receiver&) = delete;

    void Start();
    void Stop();
    [[nodiscard]] std::uint64_t DroppedAccessUnits() const noexcept { return dropped_.load(); }
    [[nodiscard]] std::uint64_t ReceivedPackets() const noexcept { return received_.load(); }

private:
    void ReceiveLoop();
    void ProcessDatagram(const std::uint8_t* data, std::size_t size);
    void DropCurrentAccessUnit(bool requestKeyframe);
    void AppendNal(const std::uint8_t* data, std::size_t size, bool idr);

    SOCKET socket_{INVALID_SOCKET};
    std::uint32_t expectedSsrc_{};
    std::uint32_t expectedPeerAddress_{};
    AccessUnitCallback onAccessUnit_;
    KeyframeCallback onKeyframeNeeded_;
    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::thread receiveThread_;
    std::chrono::steady_clock::time_point lastPacketTime_{};
    bool sequenceKnown_{};
    std::uint16_t expectedSequence_{};
    bool timestampKnown_{};
    std::uint32_t currentTimestamp_{};
    std::vector<std::uint8_t> currentAccessUnit_;
    std::vector<std::uint8_t> cachedSps_;
    std::vector<std::uint8_t> cachedPps_;
    bool currentHasIdr_{};
    bool currentHasSps_{};
    bool currentHasPps_{};
    bool currentInvalid_{};
    bool waitingForIdr_{true};
    bool keyframeRequestPending_{};
    bool fuActive_{};
    std::uint8_t fuType_{};
    std::vector<std::uint8_t> fuNal_;
};
}
