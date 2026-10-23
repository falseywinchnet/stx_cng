#pragma once
#include "match_events_bounded.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_match_trial {
inline constexpr std::size_t block_size = 262144;
inline constexpr std::uint8_t packet_mode = 34;
inline constexpr std::size_t maximum_events = block_size / 8 + 1;
struct EncodeWorkspace {
    std::array<std::uint32_t, 16384> index;
    std::array<cng_match_event::Event, maximum_events> events;
    std::array<std::uint8_t, block_size> literals;
    std::array<std::uint8_t, 14 * maximum_events + 1024> controls;
    std::array<std::uint8_t, 11 * maximum_events + 64> scratch;
    std::array<std::uint8_t, 2 * block_size + 1024> coded_literals;
};
struct DecodeWorkspace {
    std::array<cng_match_event::Event, maximum_events> events;
    std::array<std::uint8_t, block_size> literals;
};
// Disjoint caller-owned input, output and scratch. Input <= 2097160 bytes;
// encode output >= 2*input.size()+1024, decode output exactly source size.
// No retained borrows or allocation. Failure may leave partial output/scratch.
[[nodiscard]] std::size_t encode(std::span<const std::uint8_t> input,
                                 std::span<std::uint8_t> output, EncodeWorkspace& workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
            DecodeWorkspace& workspace);
}
