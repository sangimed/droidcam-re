#pragma once

#include "frame_contract.h"

#include <span>

namespace clone_vcam {

// BGRA is top-down. Row padding is permitted; alpha is ignored. On rejection,
// output is unchanged. Successful conversion also preserves caller metadata.
bool ConvertBgraToOutput(std::span<const std::uint8_t> bgra, std::uint32_t width,
                         std::uint32_t height, std::uint32_t stride, OutputFrame& output);

// Replaces all pixels and the state, preserving other metadata. Any state other
// than Reconnecting produces the disconnected image (never a neutral Live frame).
void GenerateNeutralFrame(FrameState state, OutputFrame& output);

}  // namespace clone_vcam
