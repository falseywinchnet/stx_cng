#pragma once
#include "prediction.hpp"

namespace stx_cng_detail {
// Dispatch RGB to scalar-channel recurrence and RGBA to byte-word recurrence.
// Caller owns disjoint buffers; no allocation or retained borrows.
void restore_prediction_direct_words(std::span<const Sequence> sequences,
                             std::span<const std::uint8_t> literals,
                             std::span<const std::uint8_t> previous,
                             std::size_t channels,
                             std::span<std::uint8_t> output);
}
