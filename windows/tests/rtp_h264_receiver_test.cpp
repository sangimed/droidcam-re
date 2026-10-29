#include "net/rtp_h264_receiver.h"

#include <Winsock2.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "test_assertions.h"

namespace {
constexpr std::uint32_t kSsrc = 0x12345678;

void WriteU16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 8);
    output[1] = static_cast<std::uint8_t>(value);
}

void WriteU32(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 24);
    output[1] = static_cast<std::uint8_t>(value >> 16);
    output[2] = static_cast<std::uint8_t>(value >> 8);
    output[3] = static_cast<std::uint8_t>(value);
}

bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

class RtpHarness final {
public:
    RtpHarness() {
        Check(WSAStartup(MAKEWORD(2, 2), &wsa_) == 0);
        receiveSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sendSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        Check(receiveSocket_ != INVALID_SOCKET && sendSocket_ != INVALID_SOCKET);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        Check(bind(receiveSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        int addressSize = sizeof(address);
        Check(getsockname(receiveSocket_, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0);
        destination_ = address;

        receiver_ = std::make_unique<clone_net::RtpH264Receiver>(receiveSocket_, kSsrc,
            [this](clone_net::H264AccessUnit unit) {
                if (unit.hasIdr) ++idrAccessUnits;
                ++accessUnits;
            },
            [this] { ++keyframeRequests; },
            0,
            [this](std::uint64_t, std::uint64_t lost, std::uint64_t reordered,
                   std::uint64_t dropped, bool isStalled, std::uint64_t) {
                lostPackets.store(lost);
                reorderedPackets.store(reordered);
                droppedAccessUnits.store(dropped);
                stalled.store(isStalled);
            });
        receiver_->Start();
    }

    ~RtpHarness() {
        receiver_.reset();
        if (sendSocket_ != INVALID_SOCKET) closesocket(sendSocket_);
        if (receiveSocket_ != INVALID_SOCKET) closesocket(receiveSocket_);
        WSACleanup();
    }

    void Send(std::uint16_t sequence, std::uint32_t timestamp, bool marker, bool idr) {
        std::array<std::uint8_t, 15> packet{};
        packet[0] = 0x80;
        packet[1] = static_cast<std::uint8_t>((marker ? 0x80 : 0) | 96);
        WriteU16(packet.data() + 2, sequence);
        WriteU32(packet.data() + 4, timestamp);
        WriteU32(packet.data() + 8, kSsrc);
        packet[12] = idr ? 0x65 : 0x41;
        packet[13] = 0x80;
        packet[14] = 0x00;
        const int sent = sendto(sendSocket_, reinterpret_cast<const char*>(packet.data()),
                                static_cast<int>(packet.size()), 0,
                                reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
        Check(sent == static_cast<int>(packet.size()));
    }

    std::atomic<std::uint64_t> accessUnits{};
    std::atomic<std::uint64_t> idrAccessUnits{};
    std::atomic<std::uint64_t> keyframeRequests{};
    std::atomic<std::uint64_t> lostPackets{};
    std::atomic<std::uint64_t> reorderedPackets{};
    std::atomic<std::uint64_t> droppedAccessUnits{};
    std::atomic_bool stalled{};

private:
    WSADATA wsa_{};
    SOCKET receiveSocket_{INVALID_SOCKET};
    SOCKET sendSocket_{INVALID_SOCKET};
    sockaddr_in destination_{};
    std::unique_ptr<clone_net::RtpH264Receiver> receiver_;
};

void TestLossAndReorderingRecoverAtIdr() {
    RtpHarness harness;
    harness.Send(100, 1000, true, true);
    Check(WaitUntil([&] { return harness.accessUnits.load() == 1; }, std::chrono::seconds(1)));

    harness.Send(101, 2000, false, false);
    harness.Send(103, 2000, true, false);
    Check(WaitUntil([&] { return harness.lostPackets.load() == 1 && harness.keyframeRequests.load() == 1; },
                    std::chrono::seconds(1)));
    Check(harness.accessUnits.load() == 1);

    harness.Send(104, 3000, true, true);
    Check(WaitUntil([&] { return harness.accessUnits.load() == 2; }, std::chrono::seconds(1)));

    harness.Send(105, 4000, false, false);
    harness.Send(104, 4000, true, false);
    Check(WaitUntil([&] { return harness.reorderedPackets.load() == 1 && harness.keyframeRequests.load() == 2; },
                    std::chrono::seconds(1)));
    Check(harness.accessUnits.load() == 2);

    harness.Send(106, 5000, true, false);
    harness.Send(107, 6000, true, true);
    Check(WaitUntil([&] { return harness.accessUnits.load() == 3; }, std::chrono::seconds(1)));
    Check(harness.idrAccessUnits.load() == 3);
    Check(harness.droppedAccessUnits.load() >= 2);
}

void TestRtpStallRequestsKeyframeOnceAndResumesLive() {
    RtpHarness harness;
    harness.Send(200, 1000, true, true);
    Check(WaitUntil([&] { return harness.accessUnits.load() == 1; }, std::chrono::seconds(1)));

    Check(WaitUntil([&] { return harness.stalled.load() && harness.keyframeRequests.load() == 1; },
                    std::chrono::seconds(2)));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    Check(harness.keyframeRequests.load() == 1);

    harness.Send(201, 2000, true, true);
    Check(WaitUntil([&] { return !harness.stalled.load() && harness.accessUnits.load() == 2; },
                    std::chrono::seconds(1)));

    Check(WaitUntil([&] { return harness.stalled.load() && harness.keyframeRequests.load() == 2; },
                    std::chrono::seconds(2)));
    harness.Send(202, 3000, true, true);
    Check(WaitUntil([&] { return !harness.stalled.load() && harness.accessUnits.load() == 3; },
                    std::chrono::seconds(1)));
    Check(harness.keyframeRequests.load() == 2);
}
}

int main() {
    TestLossAndReorderingRecoverAtIdr();
    TestRtpStallRequestsKeyframeOnceAndResumesLive();
}
