#include "rans.hpp"
#include "huffman_kernel.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace cng_entropy_trial {
namespace {
constexpr std::size_t block_size = 131072;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void put32(std::span<std::uint8_t> bytes, std::size_t position, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { bytes[position + byte] = static_cast<std::uint8_t>(value >> (8 * byte)); }
}
std::uint32_t get32(std::span<const std::uint8_t> bytes, std::size_t position) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { value |= static_cast<std::uint32_t>(bytes[position + byte]) << (8 * byte); }
    return value;
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() <= maximum_input && output.size() >= 2 * input.size() + 1024, "block Huffman capacity");
    output[0] = 8;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t position = 5;
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t count = std::min(block_size, input.size() - offset);
        // Each completed inner packet is at most count+5 bytes. The admitted
        // outer capacity leaves at least 2*count+512 bytes for every next call.
        std::size_t coded = cng_huffman_kernel::encode(input.subspan(offset, count), output.subspan(position + 4));
        put32(output, position, static_cast<std::uint32_t>(coded));
        position += 4 + coded;
        offset += count;
    }
    if (position >= input.size() + 5) {
        output[0] = 0;
        if (!input.empty()) { std::memcpy(output.data() + 5, input.data(), input.size()); }
        position = input.size() + 5;
    }
    return position;
}
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() >= 5 && output.size() <= maximum_input && get32(input, 1) == output.size(), "block Huffman output extent");
    if (input[0] == 0) {
        require(input.size() == output.size() + 5, "block Huffman stored extent");
        if (!output.empty()) { std::memcpy(output.data(), input.data() + 5, output.size()); }
        return;
    }
    require(input[0] == 8 && !output.empty(), "block Huffman coding");
    std::size_t position = 5;
    for (std::size_t offset = 0; offset < output.size();) {
        require(input.size() - position >= 4, "block Huffman length extent");
        std::size_t coded = get32(input, position);
        position += 4;
        require(coded <= input.size() - position, "block Huffman packet extent");
        std::size_t count = std::min(block_size, output.size() - offset);
        cng_huffman_kernel::decode(input.subspan(position, coded), output.subspan(offset, count));
        position += coded;
        offset += count;
    }
    require(position == input.size(), "block Huffman exact extent");
}
}
