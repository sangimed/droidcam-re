#include "receiver_output.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

#include "frame_conversion.h"

namespace clone_vcam {

ReceiverOutput::ReceiverOutput() = default;

ReceiverOutput::~ReceiverOutput() {
    Stop();
}

HRESULT ReceiverOutput::Start(const std::wstring& installationId) {
    Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    installationId_ = installationId;
    HRESULT hr = publisher_.Open(installationId);
    if (FAILED(hr)) return hr;

    active_ = true;
    running_ = true;
    workerThread_ = std::thread(&ReceiverOutput::WorkerLoop, this);
    return S_OK;
}

void ReceiverOutput::BeginSession(std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !active_ || generation == 0 || generation <= generation_) return;
    generation_ = generation;
    sequence_ = 0;
    pendingFrame_.reset();
    state_ = FrameState::Reconnecting;
    OutputFrame neutral;
    GenerateNeutralFrame(state_, neutral);
    neutral.metadata.generation = generation_;
    neutral.metadata.sequence = sequence_++;
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    neutral.metadata.capturedQpc = static_cast<std::uint64_t>(qpc.QuadPart);
    (void)publisher_.Publish(neutral);
}

void ReceiverOutput::OnDecodedFrame(const clone_video::DecodedFrame& frame,
                                    std::uint64_t generation,
                                    std::uint64_t nowQpc) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !active_ || generation != generation_ || state_ == FrameState::Disconnected) {
        return;
    }
    // Compatibility overload; callers on the hot decode path should transfer
    // shared ownership so the preview and publisher do not duplicate 1080p data.
    auto copy = std::make_shared<clone_video::DecodedFrame>(frame);
    pendingFrame_ = PendingFrame{std::move(copy), generation, nowQpc};
    cv_.notify_one();
}

void ReceiverOutput::OnDecodedFrame(clone_video::DecodedFrame&& frame,
                                    std::uint64_t generation,
                                    std::uint64_t nowQpc) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !active_ || generation != generation_ || state_ == FrameState::Disconnected) {
        return;
    }
    pendingFrame_ = PendingFrame{std::make_shared<clone_video::DecodedFrame>(std::move(frame)), generation, nowQpc};
    cv_.notify_one();
}

void ReceiverOutput::OnDecodedFrame(std::shared_ptr<const clone_video::DecodedFrame> frame,
                                    std::uint64_t generation, std::uint64_t nowQpc) {
    if (!frame) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || !active_ || generation != generation_ || state_ == FrameState::Disconnected) return;
    pendingFrame_ = PendingFrame{std::move(frame), generation, nowQpc};
    cv_.notify_one();
}


void ReceiverOutput::SetState(FrameState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    pendingFrame_.reset();
    if (active_ && state != FrameState::Live) {
        OutputFrame neutral;
        GenerateNeutralFrame(state, neutral);
        neutral.metadata.generation = generation_;
        neutral.metadata.sequence = sequence_++;
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        neutral.metadata.capturedQpc = static_cast<std::uint64_t>(qpc.QuadPart);
        neutral.metadata.state = state;
        publisher_.Publish(neutral);
    }
    cv_.notify_one();
}

void ReceiverOutput::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
        active_ = false;
        pendingFrame_.reset();
    }
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    publisher_.Close();
}

bool ReceiverOutput::IsActive() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

std::uint64_t ReceiverOutput::CurrentGeneration() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

std::uint64_t ReceiverOutput::LastPublishedSequence() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

FrameState ReceiverOutput::CurrentState() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void ReceiverOutput::WorkerLoop() {
    while (true) {
        PendingFrame item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || pendingFrame_.has_value(); });
            if (!running_) break;
            if (!pendingFrame_) continue;
            item = std::move(*pendingFrame_);
            pendingFrame_.reset();
            if (item.generation != generation_ || state_ == FrameState::Disconnected) {
                continue;
            }
        }

        OutputFrame output;
        output.metadata.generation = item.generation;
        output.metadata.capturedQpc = item.capturedQpc;
        output.metadata.state = FrameState::Live;

        const auto stride64 = static_cast<std::uint64_t>(item.decoded->width) * 4;
        bool ok = false;
        if (stride64 <= (std::numeric_limits<std::uint32_t>::max)() && item.decoded->height > 0) {
            ok = ConvertBgraToOutput(item.decoded->bgra, item.decoded->width, item.decoded->height,
                                     static_cast<std::uint32_t>(stride64), output);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || !active_ || item.generation != generation_ || state_ == FrameState::Disconnected) {
            continue;
        }

        if (ok) {
            state_ = FrameState::Live;
            output.metadata.sequence = sequence_++;
            (void)publisher_.Publish(output);
        } else {
            // Conversion failed -> emit neutral frame
            GenerateNeutralFrame(FrameState::Disconnected, output);
            output.metadata.generation = item.generation;
            output.metadata.sequence = sequence_++;
            output.metadata.capturedQpc = item.capturedQpc;
            output.metadata.state = FrameState::Disconnected;
            (void)publisher_.Publish(output);
        }
    }
}

}  // namespace clone_vcam
