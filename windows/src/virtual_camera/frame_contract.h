#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace clone_vcam {

inline constexpr std::uint32_t kOutputWidth = 1920;
inline constexpr std::uint32_t kOutputHeight = 1080;
inline constexpr std::uint32_t kOutputFps = 30;
inline constexpr std::size_t kOutputFrameBytes = 3'110'400;

enum class FrameState { Disconnected, Reconnecting, Live };

struct FrameMetadata {
    std::uint64_t generation{};
    std::uint64_t sequence{};
    std::uint64_t capturedQpc{};
    FrameState state{FrameState::Disconnected};
};

struct OutputFrame {
    FrameMetadata metadata;
    std::vector<std::uint8_t> nv12;
};

struct SampleTiming {
    std::int64_t timestamp;
    std::int64_t duration;
};

// Each consumer owns its clock. Times and durations are in 100 ns units.
class SampleClock {
public:
    SampleTiming Next() noexcept {
        constexpr std::int64_t ticksPerSecond = 10'000'000;
        remainder_ += ticksPerSecond % kOutputFps;
        const auto duration = ticksPerSecond / kOutputFps + remainder_ / kOutputFps;
        remainder_ %= kOutputFps;
        const SampleTiming timing{timestamp_, duration};
        timestamp_ += duration;
        return timing;
    }

    void Reset() noexcept {
        timestamp_ = 0;
        remainder_ = 0;
    }

private:
    std::int64_t timestamp_{};
    std::uint32_t remainder_{};
};

// Only current-generation live frames strictly younger than 750 ms qualify.
// Timestamps and frequency must come from the same local QPC clock.
bool IsFresh(const FrameMetadata& metadata, std::uint64_t currentGeneration,
             std::uint64_t nowQpc, std::uint64_t qpcFrequency);

}  // namespace clone_vcam
