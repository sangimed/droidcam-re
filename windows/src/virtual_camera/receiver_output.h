#pragma once

#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "frame_bridge.h"
#include "frame_contract.h"
#include "video/h264_decoder.h"

namespace clone_vcam {

class ReceiverOutput {
public:
    ReceiverOutput();
    ~ReceiverOutput();

    ReceiverOutput(const ReceiverOutput&) = delete;
    ReceiverOutput& operator=(const ReceiverOutput&) = delete;

    HRESULT Start(const std::wstring& installationId);
    void BeginSession(std::uint64_t generation);
    void OnDecodedFrame(const clone_video::DecodedFrame& frame, std::uint64_t generation, std::uint64_t nowQpc);
    void OnDecodedFrame(clone_video::DecodedFrame&& frame, std::uint64_t generation, std::uint64_t nowQpc);
    void OnDecodedFrame(std::shared_ptr<const clone_video::DecodedFrame> frame,
                        std::uint64_t generation, std::uint64_t nowQpc);

    void SetState(FrameState state);
    void Stop();

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] std::uint64_t CurrentGeneration() const noexcept;
    [[nodiscard]] std::uint64_t LastPublishedSequence() const noexcept;
    [[nodiscard]] FrameState CurrentState() const noexcept;

private:
    void WorkerLoop();

    struct PendingFrame {
        std::shared_ptr<const clone_video::DecodedFrame> decoded;
        std::uint64_t generation{};
        std::uint64_t capturedQpc{};
    };

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    FramePublisher publisher_;
    std::thread workerThread_;

    std::wstring installationId_;
    std::uint64_t generation_{0};
    std::uint64_t sequence_{0};
    FrameState state_{FrameState::Disconnected};
    std::optional<PendingFrame> pendingFrame_;

    bool running_{false};
    bool active_{false};
};

}  // namespace clone_vcam
