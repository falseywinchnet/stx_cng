#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace cng_huffman_kernel {
// Reconstruct the same bounded lengths used for raw-input Huffman coding.
// Input <= 2097160 bytes and disjoint output; zero lengths for absent symbols
// and for a constant or empty source. No allocation or retained borrows.
void price_lengths(std::span<const std::uint8_t> input, std::array<std::uint8_t, 256>& lengths);
}
