#include "video/freshness_queue.h"

#include <chrono>

#include "test_assertions.h"

namespace {
clone_net::H264AccessUnit MakeUnit(std::uint64_t sequence, bool isIdr) {
    clone_net::H264AccessUnit unit;
    unit.sequence = sequence;
    unit.hasIdr = isIdr;
    unit.receivedAt = std::chrono::steady_clock::now();
    return unit;
}
}

int main() {
    clone_video::FreshnessQueue queue(3);
    Check(queue.Push(MakeUnit(1, true)) == clone_video::FreshnessQueue::PushResult::Queued);
    Check(queue.Push(MakeUnit(2, false)) == clone_video::FreshnessQueue::PushResult::Queued);
    Check(queue.Push(MakeUnit(3, false)) == clone_video::FreshnessQueue::PushResult::Queued);
    Check(queue.GetSnapshot().depth == 3);

    Check(queue.Push(MakeUnit(4, false)) == clone_video::FreshnessQueue::PushResult::OverflowNeedsKeyframe);
    auto snapshot = queue.GetSnapshot();
    Check(snapshot.depth == 0);
    Check(snapshot.waitingForIdr);
    Check(snapshot.droppedAccessUnits == 4);

    Check(queue.Push(MakeUnit(5, false)) == clone_video::FreshnessQueue::PushResult::DroppedUntilIdr);
    Check(queue.GetSnapshot().depth == 0);
    Check(queue.GetSnapshot().droppedAccessUnits == 5);

    Check(queue.Push(MakeUnit(6, true)) == clone_video::FreshnessQueue::PushResult::Queued);
    snapshot = queue.GetSnapshot();
    Check(snapshot.depth == 1);
    Check(!snapshot.waitingForIdr);
    clone_net::H264AccessUnit recovered;
    Check(queue.Pop(recovered));
    Check(recovered.sequence == 6);
    Check(recovered.hasIdr);

    queue.Stop();
    Check(queue.Push(MakeUnit(7, true)) == clone_video::FreshnessQueue::PushResult::Stopped);
    Check(!queue.Pop(recovered));
}
