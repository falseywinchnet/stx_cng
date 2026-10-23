#include <iostream>
#define cng_huffman_kernel cng_cost_model
#include "../length_limited/huffman_exact.cpp"
#undef cng_huffman_kernel
int main() {
    std::array<std::uint32_t, 256> populations{};
    populations.fill(1);
    populations[0] = 2000;
    std::array<std::uint8_t, 256> lengths{};
    cng_cost_model::make_lengths(populations, lengths);
    std::array<std::uint16_t, 256> codes{};
    cng_cost_model::canonical(lengths, codes);
    // Zero literals, distance one; lengths 8 and 9 encode ML-8 as 0 and 1.
    // Both use a one-byte integer. No width boundary lies between them.
    unsigned length8 = lengths[0] + lengths[1] + lengths[0];
    unsigned length9 = lengths[0] + lengths[1] + lengths[1];
    if (length8 >= length9) { throw std::runtime_error("expected within-width cost witness"); }
    std::cout << "Fixed valid Huffman table: byte 0 length " << static_cast<unsigned>(lengths[0])
              << ", byte 1 length " << static_cast<unsigned>(lengths[1]) << '\n';
    std::cout << "Same-distance match costs: length 8 = " << length8 << " bits; length 9 = " << length9 << " bits\n";
    std::cout << "Equal predecessor costs satisfy the covered-position premise, but the later length-8 match wins at their common endpoint.\n";
    std::cout << "PASS: width-only dominance does not transfer, even with fixed code lengths\n";
    return 0;
}
