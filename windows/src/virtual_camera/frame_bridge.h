#pragma once

#include <Windows.h>
#include <memory>
#include <string>

#include "frame_contract.h"

namespace clone_vcam {

// installationId is a canonical GUID, including braces, generated once at install.
// Instances are single-thread owned. Different readers share no mutable local state.
// Session generation is nonzero and increases monotonically within a producer
// epoch. Sequence may start at zero, then increases within each session. Neutral
// transitions need an advancing publication sequence and current QPC timestamp.
class FramePublisher {
public:
    FramePublisher();
    ~FramePublisher();
    HRESULT Open(const std::wstring& installationId);
    bool Publish(const OutputFrame& frame);
    void Close() noexcept;
    std::uint64_t ProducerEpoch() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class FrameReader {
public:
    FrameReader();
    ~FrameReader();
    HRESULT Open(const std::wstring& installationId);
    // Privileged hosts supply the producer SID from trusted installation data.
    // The one-argument overload binds to the current effective user instead.
    HRESULT Open(const std::wstring& installationId, const std::wstring& expectedProducerSid);
    // At most three snapshot attempts. False leaves output unchanged. All states
    // expire at 750 ms; callers generate neutral output when this returns false.
    bool ReadLatest(std::uint64_t nowQpc, OutputFrame& output);
    void Close() noexcept;
    // Last successfully accepted epoch; independent from session generation.
    std::uint64_t ProducerEpoch() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace clone_vcam
