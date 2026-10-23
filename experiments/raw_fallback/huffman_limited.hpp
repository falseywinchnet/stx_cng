#pragma once
#include "huffman_kernel.hpp"
namespace cng_huffman_kernel {
// Same buffer, shape and overlap contract as encode. Return the identical
// packet size only when it is strictly below limit; otherwise return zero.
// Rejected output is partial scratch and must not be decoded or transmitted.
[[nodiscard]] std::size_t encode_limited(std::span<const std::uint8_t> input,
    std::span<std::uint8_t> output, std::size_t limit);
}
