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
    std::uint64_t sequence{};
    std::chrono::steady_clock::time_point receivedAt{};
};

class RtpH264Receiver final {
public:
    using AccessUnitCallback = std::function<void(H264AccessUnit)>;
    using KeyframeCallback = std::function<void()>;
    using StatsCallback = std::function<void(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, bool, std::uint64_t)>;

    RtpH264Receiver(SOCKET socket, std::uint32_t expectedSsrc,
                    AccessUnitCallback onAccessUnit, KeyframeCallback onKeyframeNeeded,
                    std::uint32_t expectedPeerAddress = 0, StatsCallback onStats = {});
    ~RtpH264Receiver();
    RtpH264Receiver(const RtpH264Receiver&) = delete;
    RtpH264Receiver& operator=(const RtpH264Receiver&) = delete;

    void Start();
    void Stop();
    [[nodiscard]] std::uint64_t DroppedAccessUnits() const noexcept { return dropped_.load(); }
    [[nodiscard]] std::uint64_t ReceivedPackets() const noexcept { return received_.load(); }
    [[nodiscard]] std::uint64_t LostPackets() const noexcept { return lostPackets_.load(); }
    [[nodiscard]] std::uint64_t ReorderedPackets() const noexcept { return reorderedPackets_.load(); }

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
    StatsCallback onStats_;
    std::atomic_bool running_{false};
    std::atomic<std::uint64_t> received_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> lostPackets_{0};
    std::atomic<std::uint64_t> reorderedPackets_{0};
    std::thread receiveThread_;
    std::chrono::steady_clock::time_point lastPacketTime_{};
    bool streamStalled_{};
    bool stallRequestSent_{};
    bool sequenceKnown_{};
    std::uint16_t expectedSequence_{};
    std::uint64_t nextAccessUnitSequence_{};
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
