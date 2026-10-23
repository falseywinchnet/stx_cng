#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_huffman_kernel {
// Private bounded block kernel. Same inverse format as the first Huffman trial.
// Disjoint caller buffers; output capacity >= 2*input.size()+512. No allocation
// or retained borrows; partial output on failure. Input <= 2097160 bytes.
[[nodiscard]] std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);
}
