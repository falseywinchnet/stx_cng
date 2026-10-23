#pragma once
#include "parse.hpp"

namespace stx_cng_detail {
// Source is a complete RGB/RGBA band of at most band_capacity bytes; previous
// is one full row (zeros at image start). Borrow all arguments synchronously.
// Index and sequences require the same capacities as parse(), and must be
// disjoint from each other and both read-only inputs. Source/previous may alias.
// Positive length with zero distance denotes exact gradient prediction.
// Validation failure leaves caller storage intact; no successful-call allocation.
[[nodiscard]] Result parse_prediction(std::span<const std::uint8_t> source,
                                      std::span<const std::uint8_t> previous,
                                      std::size_t channels,
                                      std::span<std::int32_t> index,
                                      std::span<Sequence> output);

// The restore() storage and extent contract also applies here, with distance
// zero admitted for exact prediction runs. Validate all sequences before writes.
void restore_prediction(std::span<const Sequence> sequences,
                        std::span<const std::uint8_t> literals,
                        std::span<const std::uint8_t> previous,
                        std::size_t channels,
                        std::span<std::uint8_t> output);
}
