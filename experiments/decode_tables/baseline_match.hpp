// Diagnostic declaration copy; compile under the retained namespace.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_match_trial {
inline constexpr std::size_t block_size = 131072;
struct Entry {
    std::uint32_t word = 0;
    std::uint32_t position = 0;
};
// Two complete prefix/position records occupy one aligned 16-byte bucket.
struct alignas(16) Bucket {
    Entry recent;
    Entry older;
};
struct EncodeWorkspace {
    std::array<Bucket, 16384> index;
    std::array<std::uint8_t, block_size + 16> parsed;
    std::array<std::uint8_t, 2 * block_size + 1024> coded;
};
struct DecodeWorkspace {
    std::array<std::uint8_t, block_size + 16> parsed;
};
// Independent experimental packets. Disjoint input, output and workspace;
// input <= 2097160 bytes, encode output >= input.size()+4*ceil(input.size()/131072)+5. Decode output
// has the exact source size. Caller owns/reuses workspace; no allocation or
// retained borrows. Failure may leave partial output and scratch contents.
[[nodiscard]] std::size_t encode(std::span<const std::uint8_t> input,
                                 std::span<std::uint8_t> output, EncodeWorkspace& workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
            DecodeWorkspace& workspace);
}
