#include "virtual_camera/frame_conversion.h"
#include "virtual_camera/frame_bridge.h"
#include "virtual_camera/receiver_output.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <thread>
#include <cstdio>

#define CHECK_ASSERT(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::abort(); } } while(0)
#define Check CHECK_ASSERT



namespace {
using namespace clone_vcam;
constexpr std::size_t yBytes = 1920 * 1080;

std::vector<std::uint8_t> SolidBgra(std::uint32_t width, std::uint32_t height,
                                     std::uint8_t b, std::uint8_t g, std::uint8_t r,
                                     std::uint32_t padding = 0) {
    const auto stride = width * 4 + padding;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height, 0xcc);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = static_cast<std::size_t>(y) * stride + x * 4;
            pixels[offset] = b;
            pixels[offset + 1] = g;
            pixels[offset + 2] = r;
            pixels[offset + 3] = 0;  // Alpha does not mask camera pixels.
        }
    }
    return pixels;
}

void rejectsInvalidDimensionsAndStride() {
    OutputFrame output{{7, 8, 9, FrameState::Live}, {1, 2, 3}};
    const auto pixels = SolidBgra(2, 2, 255, 255, 255);
    const auto max = (std::numeric_limits<std::uint32_t>::max)();
    const std::array<std::array<std::uint32_t, 3>, 7> invalid{{
        {0, 2, 8}, {2, 0, 8}, {2, 2, 7}, {2, 2, 0},
        {max, 2, max}, {max / 4 + 1, 2, max}, {2, max, max}
    }};
    for (const auto& dimensions : invalid) {
        Check(!ConvertBgraToOutput(pixels, dimensions[0], dimensions[1], dimensions[2], output));
    }
    Check(!ConvertBgraToOutput({}, 2, 2, 8, output));
    Check(!ConvertBgraToOutput(std::span(pixels).first(15), 2, 2, 8, output));
    Check((output.nv12 == std::vector<std::uint8_t>{1, 2, 3}));
    Check(output.metadata.generation == 7 && output.metadata.sequence == 8);
    Check(output.metadata.capturedQpc == 9 && output.metadata.state == FrameState::Live);
}

void convertsKnownBlackWhiteNv12() {
    OutputFrame output{{4, 5, 6, FrameState::Live}, {}};
    Check(ConvertBgraToOutput(SolidBgra(2, 2, 0, 0, 0), 2, 2, 8, output));
    Check(output.nv12.size() == 3'110'400);
    Check(std::all_of(output.nv12.begin(), output.nv12.begin() + yBytes,
                      [](auto y) { return y == 16; }));
    Check(std::all_of(output.nv12.begin() + yBytes, output.nv12.end(),
                      [](auto uv) { return uv == 128; }));
    // Square input is centered from x=420 through x=1499.
    Check(ConvertBgraToOutput(SolidBgra(2, 2, 255, 255, 255, 4), 2, 2, 12, output));
    Check(output.nv12[419] == 16 && output.nv12[420] == 235);
    Check(output.nv12[1499] == 235 && output.nv12[1500] == 16);
    Check(output.nv12[1079 * 1920 + 420] == 235);
    Check(std::all_of(output.nv12.begin() + yBytes, output.nv12.end(),
                      [](auto uv) { return uv == 128; }));
    Check(output.metadata.generation == 4 && output.metadata.sequence == 5);
    Check(output.metadata.capturedQpc == 6 && output.metadata.state == FrameState::Live);
}

void letterboxes720pAndPortrait() {
    OutputFrame output;
    Check(ConvertBgraToOutput(SolidBgra(1280, 720, 255, 255, 255), 1280, 720, 5120, output));
    Check(std::all_of(output.nv12.begin(), output.nv12.begin() + yBytes,
                      [](auto y) { return y == 235; }));
    Check(ConvertBgraToOutput(SolidBgra(720, 1280, 255, 255, 255), 720, 1280, 2880, output));
    // Portrait is 608x1080, aligned to even NV12 coordinates, with 656px bars.
    for (std::uint32_t y : {0u, 539u, 1079u}) {
        Check(output.nv12[y * 1920 + 655] == 16);
        Check(output.nv12[y * 1920 + 656] == 235);
        Check(output.nv12[y * 1920 + 1263] == 235);
        Check(output.nv12[y * 1920 + 1264] == 16);
    }
    Check(ConvertBgraToOutput(SolidBgra(1280, 360, 255, 255, 255), 1280, 360, 5120, output));
    Check(output.nv12[269 * 1920] == 16 && output.nv12[270 * 1920] == 235);
    Check(output.nv12[809 * 1920 + 1919] == 235 && output.nv12[810 * 1920] == 16);
    Check(ConvertBgraToOutput(SolidBgra(3, 3, 255, 255, 255), 3, 3, 12, output));
    Check(output.nv12[420] == 235);
}

void convertsBt709ColourAndAveragesChroma() {
    OutputFrame output;
    // Red (BT.709 limited range) catches swapped BGRA channels and U/V order.
    Check(ConvertBgraToOutput(SolidBgra(1920, 1080, 0, 0, 255), 1920, 1080, 7680, output));
    Check(output.nv12[0] == 63 && output.nv12[yBytes] == 102 && output.nv12[yBytes + 1] == 240);
    auto pixels = SolidBgra(1920, 1080, 0, 0, 0);
    pixels[2] = 255;
    Check(ConvertBgraToOutput(pixels, 1920, 1080, 7680, output));
    Check(output.nv12[0] == 63 && output.nv12[1] == 16);
    Check(output.nv12[yBytes] == 122 && output.nv12[yBytes + 1] == 156);
}

void generationRejectsOldFrames() {
    FrameMetadata metadata{7, 99, 1000, FrameState::Live};
    Check(IsFresh(metadata, 7, 1000, 1000));
    Check(!IsFresh(metadata, 8, 1000, 1000));
    Check(!IsFresh(metadata, 6, 1000, 1000));
    metadata.state = FrameState::Disconnected;
    Check(!IsFresh(metadata, 7, 1000, 1000));
    metadata.state = FrameState::Reconnecting;
    Check(!IsFresh(metadata, 7, 1000, 1000));
}

void freshnessDeadline() {
    FrameMetadata metadata{1, 1, 1000, FrameState::Live};
    Check(IsFresh(metadata, 1, 1749, 1000));
    Check(!IsFresh(metadata, 1, 1750, 1000));
    Check(!IsFresh(metadata, 1, 999, 1000));
    Check(!IsFresh(metadata, 1, 1000, 0));
    // At frequency 3, ages 2 and 3 are 667 ms and 1 s; avoid rounded cutoffs.
    metadata.capturedQpc = 0;
    Check(IsFresh(metadata, 1, 2, 3));
    Check(!IsFresh(metadata, 1, 3, 3));
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    Check(IsFresh(metadata, 1, max / 2, max));
    Check(!IsFresh(metadata, 1, max, max));
    metadata.capturedQpc = max - 100;
    Check(IsFresh(metadata, 1, max, 1000));
}

void sampleClockHasExactCadence() {
    SampleClock clock;
    SampleClock independent;
    std::int64_t end = 0;
    for (int frame = 0; frame < 30; ++frame) {
        const auto timing = clock.Next();
        Check(timing.timestamp == end);
        Check(timing.duration == 333'333 || timing.duration == 333'334);
        end = timing.timestamp + timing.duration;
        Check(end > timing.timestamp);
    }
    Check(end == 10'000'000);
    Check(clock.Next().timestamp == 10'000'000);
    Check(independent.Next().timestamp == 0);
    clock.Reset();
    Check(clock.Next().timestamp == 0);
}

void neutralFramesReplaceOldVideoDeterministically() {
    OutputFrame disconnected{{11, 12, 13, FrameState::Live}, {255}};
    GenerateNeutralFrame(FrameState::Disconnected, disconnected);
    Check(disconnected.nv12.size() == 3'110'400);
    Check(disconnected.metadata.state == FrameState::Disconnected);
    Check(disconnected.metadata.generation == 11 && disconnected.metadata.sequence == 12);
    Check(disconnected.metadata.capturedQpc == 13);
    Check(std::all_of(disconnected.nv12.begin() + yBytes, disconnected.nv12.end(),
                      [](auto uv) { return uv == 128; }));
    Check(disconnected.nv12.front() == disconnected.nv12[yBytes - 1]);
    Check(*std::max_element(disconnected.nv12.begin(), disconnected.nv12.begin() + yBytes) >
          disconnected.nv12.front());  // State text is visible.
    OutputFrame repeated;
    GenerateNeutralFrame(FrameState::Disconnected, repeated);
    Check(disconnected.nv12 == repeated.nv12);
    GenerateNeutralFrame(FrameState::Reconnecting, repeated);
    Check(repeated.metadata.state == FrameState::Reconnecting);
    Check(disconnected.nv12 != repeated.nv12);
    GenerateNeutralFrame(FrameState::Live, repeated);
    Check(repeated.metadata.state == FrameState::Disconnected);
    Check(disconnected.nv12 == repeated.nv12);
}

void oldCallbackCannotPublishIntoNewSession() {
    GUID guid{};
    Check(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[39]{};
    StringFromGUID2(guid, id, 39);

    ReceiverOutput output;
    Check(SUCCEEDED(output.Start(id)));
    output.BeginSession(1);

    clone_video::DecodedFrame frame{2, 2, SolidBgra(2, 2, 255, 255, 255)};
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    output.OnDecodedFrame(frame, 1, static_cast<uint64_t>(qpc.QuadPart));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    FrameReader reader;
    Check(SUCCEEDED(reader.Open(id)));
    OutputFrame readFrame;
    Check(reader.ReadLatest(static_cast<uint64_t>(qpc.QuadPart), readFrame));
    Check(readFrame.metadata.generation == 1);

    // Start generation 2
    output.BeginSession(2);
    // Old callback with generation 1 arrives -> must be rejected
    output.OnDecodedFrame(frame, 1, static_cast<uint64_t>(qpc.QuadPart));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Generation 2 frame arrives -> accepted
    QueryPerformanceCounter(&qpc);
    output.OnDecodedFrame(frame, 2, static_cast<uint64_t>(qpc.QuadPart));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    Check(reader.ReadLatest(static_cast<uint64_t>(qpc.QuadPart), readFrame));
    Check(readFrame.metadata.generation == 2);
    output.Stop();
}

void disconnectInvalidatesPublishedFrame() {
    GUID guid{};
    Check(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[39]{};
    StringFromGUID2(guid, id, 39);

    ReceiverOutput output;
    Check(SUCCEEDED(output.Start(id)));
    output.BeginSession(1);

    clone_video::DecodedFrame frame{2, 2, SolidBgra(2, 2, 255, 255, 255)};
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    output.OnDecodedFrame(frame, 1, static_cast<uint64_t>(qpc.QuadPart));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    FrameReader reader;
    Check(SUCCEEDED(reader.Open(id)));
    OutputFrame readFrame;
    Check(reader.ReadLatest(static_cast<uint64_t>(qpc.QuadPart), readFrame));
    Check(readFrame.metadata.state == FrameState::Live);

    // Disconnect invalidates published frame
    output.SetState(FrameState::Disconnected);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    QueryPerformanceCounter(&qpc);
    Check(reader.ReadLatest(static_cast<uint64_t>(qpc.QuadPart), readFrame));
    Check(readFrame.metadata.state == FrameState::Disconnected);
    output.Stop();
}

void conversionFailureEmitsNeutral() {
    GUID guid{};
    Check(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[39]{};
    StringFromGUID2(guid, id, 39);

    ReceiverOutput output;
    Check(SUCCEEDED(output.Start(id)));
    output.BeginSession(1);

    // Corrupted frame with 0 dimensions
    clone_video::DecodedFrame corruptFrame{0, 0, {}};
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    output.OnDecodedFrame(corruptFrame, 1, static_cast<uint64_t>(qpc.QuadPart));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    FrameReader reader;
    Check(SUCCEEDED(reader.Open(id)));
    OutputFrame readFrame;
    Check(reader.ReadLatest(static_cast<uint64_t>(qpc.QuadPart), readFrame));
    // Must have emitted a neutral frame with state Disconnected
    Check(readFrame.metadata.state == FrameState::Disconnected);
    output.Stop();
}

void slowOutputDoesNotBlockDecode() {
    GUID guid{};
    Check(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t id[39]{};
    StringFromGUID2(guid, id, 39);

    ReceiverOutput output;
    Check(SUCCEEDED(output.Start(id)));
    output.BeginSession(1);

    clone_video::DecodedFrame frame{1920, 1080, SolidBgra(1920, 1080, 100, 150, 200)};
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) {
        output.OnDecodedFrame(frame, 1, static_cast<uint64_t>(qpc.QuadPart));
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    Check(elapsed < 200);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    output.Stop();
}
}  // namespace

int main() {
    std::puts("rejectsInvalidDimensionsAndStride...");
    rejectsInvalidDimensionsAndStride();
    std::puts("letterboxes720pAndPortrait...");
    letterboxes720pAndPortrait();
    std::puts("convertsKnownBlackWhiteNv12...");
    convertsKnownBlackWhiteNv12();
    std::puts("convertsBt709ColourAndAveragesChroma...");
    convertsBt709ColourAndAveragesChroma();
    std::puts("generationRejectsOldFrames...");
    generationRejectsOldFrames();
    std::puts("freshnessDeadline...");
    freshnessDeadline();
    std::puts("sampleClockHasExactCadence...");
    sampleClockHasExactCadence();
    std::puts("neutralFramesReplaceOldVideoDeterministically...");
    neutralFramesReplaceOldVideoDeterministically();
    std::puts("oldCallbackCannotPublishIntoNewSession...");
    oldCallbackCannotPublishIntoNewSession();
    std::puts("disconnectInvalidatesPublishedFrame...");
    disconnectInvalidatesPublishedFrame();
    std::puts("conversionFailureEmitsNeutral...");
    conversionFailureEmitsNeutral();
    std::puts("slowOutputDoesNotBlockDecode...");
    slowOutputDoesNotBlockDecode();
    std::puts("PASS all virtual_camera_frame tests");
}
