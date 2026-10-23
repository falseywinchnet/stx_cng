#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_match_trial {
inline constexpr std::size_t block_size = 131072;
struct EncodeWorkspace {
    std::array<std::uint32_t, 16384> index;
    std::array<std::uint8_t, block_size + 16> parsed;
    std::array<std::uint8_t, 2 * block_size + 1024> coded;
};
struct DecodeWorkspace {
    std::array<std::uint8_t, block_size + 16> parsed;
};
// Independent experimental packets. Disjoint input, output and workspace;
// input <= 2097160 bytes, encode output >= 2*input.size()+1024. Decode output
// has the exact source size. Caller owns/reuses workspace; no allocation or
// retained borrows. Failure may leave partial output and scratch contents.
[[nodiscard]] std::size_t encode(std::span<const std::uint8_t> input,
                                 std::span<std::uint8_t> output, EncodeWorkspace& workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
            DecodeWorkspace& workspace);
}
