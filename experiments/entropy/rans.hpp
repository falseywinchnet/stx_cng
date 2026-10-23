#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace cng_entropy_trial {
inline constexpr std::size_t maximum_input = 2097160;
// Independent experimental block format, not CSP. Four interleaved states and
// one transmitted 256-symbol histogram; stored fallback bounds expansion.
// Caller supplies disjoint storage. Encode capacity >= 2*input.size()+1024.
// Decode output has the exact expected raw size. No allocations/retained borrows.
// Failures throw and may leave partial output. Input is bounded by maximum_input.
[[nodiscard]] std::size_t encode(std::span<const std::uint8_t> input,
                                 std::span<std::uint8_t> output);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);
}
