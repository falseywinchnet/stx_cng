// Deliberately compile the private model grammar in this test translation unit.
// No test-only interface enters the library or the measured executable.
#include "huffman_compact_table.cpp"
#include <fstream>
#include <iostream>
namespace {
void check(const std::array<std::uint8_t, 256>& lengths, std::ostream& vectors) {
    std::array<std::uint8_t, 128> packet{};
    std::array<std::uint8_t, 256> decoded{};
    std::size_t size = cng_huffman_kernel::write_lengths(lengths, packet);
    std::size_t used = cng_huffman_kernel::read_lengths(std::span<const std::uint8_t>(packet).first(size), decoded);
    if (size > 128 || used != size || lengths != decoded) { throw std::runtime_error("model exactness and bound"); }
    vectors.put(static_cast<char>(size));
    vectors.write(reinterpret_cast<const char*>(lengths.data()), 256);
    vectors.write(reinterpret_cast<const char*>(packet.data()), static_cast<std::streamsize>(size));
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("model vector output"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("model vector open"); }
        std::array<std::uint8_t, 256> lengths{};
        std::size_t cases = 0;
        for (unsigned first = 0; first <= 12; ++first) {
            for (unsigned second = 0; second <= 12; ++second) {
                for (std::size_t split = 0; split <= 256; ++split) {
                    for (std::size_t index = 0; index < lengths.size(); ++index) {
                        lengths[index] = static_cast<std::uint8_t>(index < split ? first : second);
                    }
                    check(lengths, vectors);
                    ++cases;
                }
            }
        }
        for (std::size_t index = 0; index < lengths.size(); ++index) { lengths[index] = static_cast<std::uint8_t>(index % 13); }
        check(lengths, vectors);
        ++cases;
        vectors.close();
        if (!vectors) { throw std::runtime_error("model vector write"); }
        std::cout << "PASS: " << cases << " model grammar cases\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
