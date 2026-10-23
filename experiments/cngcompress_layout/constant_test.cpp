#include "constant.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
void fixture(std::uint32_t pattern, std::size_t count) {
    for (unsigned side = 0; side < 2; ++side) {
        std::array<std::uint8_t, 4> bitmap{};
        std::array<std::uint8_t, 32> values{};
        std::array<std::uint8_t, 32> expected{};
        std::array<std::uint8_t, 32> output{};
        std::uint8_t constant = static_cast<std::uint8_t>(pattern * 37 + 19);
        std::size_t source = 0;
        for (std::size_t position = 0; position < count; ++position) {
            unsigned bit = (pattern >> position) & 1U;
            bitmap[position / 8] |= static_cast<std::uint8_t>(bit << (position % 8));
            if (bit == side) { expected[position] = constant; }
            else {
                values[source] = static_cast<std::uint8_t>(position * 13 + 47);
                expected[position] = values[source];
                ++source;
            }
        }
        output.fill(255);
        cngcompress_layout::merge_constant_words(std::span<const std::uint8_t>(bitmap).first((count + 7) / 8),
            std::span<const std::uint8_t>(values).first(source), constant, side, std::span<std::uint8_t>(output).first(count));
        if (!std::equal(expected.begin(), expected.begin() + count, output.begin())) { throw std::runtime_error("constant merge equality"); }
        for (std::size_t position = count; position < output.size(); ++position) {
            if (output[position] != 255) { throw std::runtime_error("constant output extent"); }
        }
    }
}
}
int main() {
    for (std::uint32_t mask = 0; mask < 65536; ++mask) { fixture(mask, 16); }
    for (std::uint32_t mask = 0; mask < 256; ++mask) {
        for (std::size_t count = 0; count <= 32; ++count) { fixture(mask * UINT32_C(0x01010101), count); }
    }
    std::cout << "PASS: 147968 constant-child mask/boundary fixtures\n";
    return 0;
}
