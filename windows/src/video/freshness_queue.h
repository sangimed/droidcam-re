#pragma once

#include "../net/rtp_h264_receiver.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace clone_video {

// A bounded decoder handoff that only resumes after an IDR when it has to
// discard queued H.264 pictures. Dropping arbitrary reference pictures would
// leave the decoder displaying a damaged reference chain.
class FreshnessQueue final {
public:
    enum class PushResult { Queued, DroppedUntilIdr, OverflowNeedsKeyframe, Stopped };

    struct Snapshot {
        std::size_t depth{};
        std::uint64_t oldestAgeMs{};
        std::uint64_t oldestEnqueuedNs{};
        std::uint64_t droppedAccessUnits{};
        bool waitingForIdr{};
    };

    explicit FreshnessQueue(std::size_t capacity = 3) : capacity_((std::max)(std::size_t{1}, capacity)) {}

    PushResult Push(clone_net::H264AccessUnit unit) {
        std::lock_guard lock(mutex_);
        if (stopped_) return PushResult::Stopped;

        if (waitingForIdr_) {
            if (!unit.hasIdr) {
                ++droppedAccessUnits_;
                return PushResult::DroppedUntilIdr;
            }
            waitingForIdr_ = false;
        }

        if (queue_.size() >= capacity_) {
            droppedAccessUnits_ += queue_.size();
            queue_.clear();
            if (!unit.hasIdr) {
                ++droppedAccessUnits_;
                waitingForIdr_ = true;
                return PushResult::OverflowNeedsKeyframe;
            }
        }

        queue_.push_back(std::move(unit));
        ready_.notify_one();
        return PushResult::Queued;
    }

    bool Pop(clone_net::H264AccessUnit& unit) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        unit = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void Stop() {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        droppedAccessUnits_ += queue_.size();
        queue_.clear();
        ready_.notify_all();
    }

    [[nodiscard]] Snapshot GetSnapshot() const {
        std::lock_guard lock(mutex_);
        const auto age = queue_.empty() ? 0 : std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - queue_.front().receivedAt).count();
        const auto oldestNs = queue_.empty() ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(
            queue_.front().receivedAt.time_since_epoch()).count();
        return Snapshot{queue_.size(), static_cast<std::uint64_t>((std::max)(age, std::int64_t{0})),
                        static_cast<std::uint64_t>((std::max)(oldestNs, std::int64_t{0})),
                        droppedAccessUnits_, waitingForIdr_};
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<clone_net::H264AccessUnit> queue_;
    std::uint64_t droppedAccessUnits_{};
    bool waitingForIdr_{true};
    bool stopped_{};
};

}  // namespace clone_video
