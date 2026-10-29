#include "frame_conversion.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace clone_vcam {
namespace {
constexpr std::size_t yBytes = static_cast<std::size_t>(kOutputWidth) * kOutputHeight;

std::vector<std::uint8_t> BlankNv12(std::uint8_t luminance) {
    std::vector<std::uint8_t> pixels(kOutputFrameBytes, 128);
    std::fill_n(pixels.begin(), yBytes, luminance);
    return pixels;
}

std::uint32_t RoundToEven(std::uint64_t numerator, std::uint32_t denominator) {
    return static_cast<std::uint32_t>((numerator + denominator) / (2ull * denominator) * 2);
}

// Fixed BT.709 limited-range coefficients, rounded at 16 fractional bits.
std::uint8_t Luma(int b, int g, int r) {
    return static_cast<std::uint8_t>(16 + (11966 * r + 40254 * g + 4064 * b + 32768) / 65536);
}

// Five columns, eight rows. Only glyphs used by the two state messages are needed.
// Accented e includes its accent; no system font or locale affects rasterization.
std::array<std::uint8_t, 8> Glyph(char32_t character) {
    switch (character) {
    case U'C': return {14, 17, 16, 16, 16, 17, 14, 0};
    case U'R': return {30, 17, 17, 30, 20, 18, 17, 0};
    case U'a': return {0, 0, 14, 1, 15, 17, 15, 0};
    case U'c': return {0, 0, 14, 16, 16, 17, 14, 0};
    case U'd': return {1, 1, 15, 17, 17, 17, 15, 0};
    case U'e': return {0, 0, 14, 17, 31, 16, 14, 0};
    case U'\u00e9': return {2, 4, 14, 17, 31, 16, 14, 0};
    case U'i': return {4, 0, 12, 4, 4, 4, 14, 0};
    case U'm': return {0, 0, 26, 21, 21, 17, 17, 0};
    case U'n': return {0, 0, 30, 17, 17, 17, 17, 0};
    case U'o': return {0, 0, 14, 17, 17, 17, 14, 0};
    case U'r': return {0, 0, 22, 25, 16, 16, 16, 0};
    case U's': return {0, 0, 15, 16, 14, 1, 30, 0};
    case U't': return {8, 8, 30, 8, 8, 9, 6, 0};
    case U'u': return {0, 0, 17, 17, 17, 19, 13, 0};
    case U'x': return {0, 0, 17, 10, 4, 10, 17, 0};
    default: return {};
    }
}

void DrawStateText(std::vector<std::uint8_t>& pixels, std::u32string_view text) {
    constexpr std::uint32_t scale = 8;
    const auto textWidth = static_cast<std::uint32_t>(text.size() * 6 - 1) * scale;
    const auto left = (kOutputWidth - textWidth) / 2;
    const auto top = (kOutputHeight - 8 * scale) / 2;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto glyph = Glyph(text[index]);
        for (std::uint32_t row = 0; row < glyph.size(); ++row) {
            for (std::uint32_t column = 0; column < 5; ++column) {
                if ((glyph[row] & (1u << (4 - column))) == 0) continue;
                const auto x = left + index * 6 * scale + column * scale;
                for (std::uint32_t line = 0; line < scale; ++line) {
                    const auto y = top + row * scale + line;
                    std::fill_n(pixels.begin() + y * kOutputWidth + x, scale, 210);
                }
            }
        }
    }
}
}  // namespace

bool IsFresh(const FrameMetadata& metadata, std::uint64_t currentGeneration,
             std::uint64_t nowQpc, std::uint64_t qpcFrequency) {
    if (metadata.state != FrameState::Live || metadata.generation != currentGeneration ||
        qpcFrequency == 0 || metadata.capturedQpc > nowQpc) return false;

    // age < 3*frequency/4, with an exact integer ceiling and no multiplication
    // overflow even for adversarial 64-bit metadata/frequency values.
    const auto deadline = (qpcFrequency / 4) * 3 + ((qpcFrequency % 4) * 3 + 3) / 4;
    return nowQpc - metadata.capturedQpc < deadline;
}

bool ConvertBgraToOutput(std::span<const std::uint8_t> bgra, std::uint32_t width,
                         std::uint32_t height, std::uint32_t stride, OutputFrame& output) {
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(width) * 4;
    if (width == 0 || height == 0 || rowBytes > stride) return false;
    const auto maxSize = (std::numeric_limits<std::size_t>::max)();
    if (height - 1 > (maxSize - rowBytes) / stride) return false;
    const auto required = static_cast<std::size_t>(height - 1) * stride + rowBytes;
    if (bgra.size() < required) return false;

    std::uint32_t scaledWidth = kOutputWidth;
    std::uint32_t scaledHeight = kOutputHeight;
    if (static_cast<std::uint64_t>(width) * kOutputHeight >
        static_cast<std::uint64_t>(height) * kOutputWidth) {
        scaledHeight = (std::max)(2u, RoundToEven(static_cast<std::uint64_t>(height) * kOutputWidth, width));
    } else {
        scaledWidth = (std::max)(2u, RoundToEven(static_cast<std::uint64_t>(width) * kOutputHeight, height));
    }
    // Round centering down to a chroma pair, keeping all bars neutral in NV12.
    const auto left = ((kOutputWidth - scaledWidth) / 2) & ~1u;
    const auto top = ((kOutputHeight - scaledHeight) / 2) & ~1u;
    auto converted = BlankNv12(16);
    for (std::uint32_t y = 0; y < scaledHeight; y += 2) {
        for (std::uint32_t x = 0; x < scaledWidth; x += 2) {
            int bSum = 0;
            int gSum = 0;
            int rSum = 0;
            for (std::uint32_t dy = 0; dy < 2; ++dy) {
                const auto sourceY = static_cast<std::uint64_t>(y + dy) * height / scaledHeight;
                for (std::uint32_t dx = 0; dx < 2; ++dx) {
                    const auto sourceX = static_cast<std::uint64_t>(x + dx) * width / scaledWidth;
                    const auto source = static_cast<std::size_t>(sourceY) * stride + sourceX * 4;
                    const int b = bgra[source];
                    const int g = bgra[source + 1];
                    const int r = bgra[source + 2];
                    bSum += b;
                    gSum += g;
                    rSum += r;
                    converted[static_cast<std::size_t>(top + y + dy) * kOutputWidth + left + x + dx] = Luma(b, g, r);
                }
            }
            const auto chroma = yBytes + static_cast<std::size_t>((top + y) / 2) * kOutputWidth + left + x;
            // Average all four RGB pixels before chroma quantization.
            constexpr int divisor = 4 * 65536;
            converted[chroma] = static_cast<std::uint8_t>(
                (128 * divisor - 6596 * rSum - 22189 * gSum + 28784 * bSum + divisor / 2) / divisor);
            converted[chroma + 1] = static_cast<std::uint8_t>(
                (128 * divisor + 28784 * rSum - 26145 * gSum - 2639 * bSum + divisor / 2) / divisor);
        }
    }
    output.nv12.swap(converted);
    return true;
}

void GenerateNeutralFrame(FrameState state, OutputFrame& output) {
    const auto neutralState = state == FrameState::Reconnecting ? FrameState::Reconnecting : FrameState::Disconnected;
    auto pixels = BlankNv12(32);
    DrawStateText(pixels, neutralState == FrameState::Reconnecting
        ? U"Reconnexion en cours" : U"Cam\u00e9ra d\u00e9connect\u00e9e");
    output.nv12.swap(pixels);
    output.metadata.state = neutralState;
}

}  // namespace clone_vcam
