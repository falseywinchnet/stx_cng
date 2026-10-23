#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace cngcompress_layout {
// Private experimental packet, no public format promise. Complete Huffman lengths
// 1..12 with >=2 symbols. Disjoint input/output/workspace; no retained borrows.
// Encode: input <=2097160, output >=2*n+1024, workspace >=2*n.
// Decode: output exact n, workspace >=n. Validation can leave partial output.
// No allocation. Errors throw at this experimental boundary.
std::size_t encode(std::span<const std::uint8_t> input,
                   const std::array<std::uint8_t, 256>& lengths,
                   std::span<std::uint8_t> output, std::span<std::uint8_t> workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
            std::span<std::uint8_t> workspace);
}
