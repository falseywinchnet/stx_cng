#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_event_trial {
// Experimental sequence block, not CSP. Input/output of the inverse use CNG's
// twelve byte planes. Three fields: literal count, match length, copy distance.
// At most 87382 records, fields bounded by 1048576, positive lengths >= 4.
// Caller buffers are disjoint. Encode output >= 2*input.size()+2048 and scratch
// >= 2*input.size()+64. No allocation or retained borrows; failure leaves partial
// output. Decode output has the exact original size.
[[nodiscard]] std::size_t encode(std::span<const std::uint8_t> input,
                                 std::span<std::uint8_t> output,
                                 std::span<std::uint8_t> scratch);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);
}
