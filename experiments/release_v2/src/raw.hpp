#pragma once
#include "prediction.hpp"

namespace stx_cng_detail {
// Same extent, storage and failure contract as restore_prediction(), but the
// literal buffer contains original RGB/RGBA samples, not modular residuals.
// Copy and exact-prediction sequence semantics are unchanged.
void restore_raw(std::span<const Sequence> sequences,
                 std::span<const std::uint8_t> literals,
                 std::span<const std::uint8_t> previous,
                 std::size_t channels,
                 std::span<std::uint8_t> output);
}
