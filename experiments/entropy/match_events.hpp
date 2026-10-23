#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_match_event {
struct Event {
    std::uint32_t literals = 0;
    std::uint32_t length = 0;
    std::uint32_t distance = 0;
};
// Experimental integer event coding, not a production packet. Each field is
// <= 2097160, positive lengths >= 3, at most 87382 events. Input, output and
// scratch are disjoint caller storage; no allocation or retained borrows.
// Encode capacity >= 24*count+2048, scratch >= 24*count+64. Decode supplies the
// exact event count. Failure may leave partial output and scratch contents.
[[nodiscard]] std::size_t encode(std::span<const Event> input, std::span<std::uint8_t> output,
                                 std::span<std::uint8_t> scratch);
void decode(std::span<const std::uint8_t> input, std::span<Event> output);
}
