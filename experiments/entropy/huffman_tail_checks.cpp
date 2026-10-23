#include "huffman_kernel.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void put_size(std::ostream& output, std::size_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { output.put(static_cast<char>(value >> (8 * byte))); }
}
void check(std::size_t size, unsigned pattern, std::ostream& vectors) {
    std::vector<std::uint8_t> raw(size);
    std::vector<std::uint8_t> coded(2 * size + 512);
    std::vector<std::uint8_t> restored(size);
    std::uint32_t state = 20261023;
    for (std::size_t index = 0; index < size; ++index) {
        state = state * 1664525U + 1013904223U;
        std::uint8_t value = static_cast<std::uint8_t>(state >> 24);
        switch (pattern) {
        case 0: value = 77; break;
        case 1: value &= 1U; break;
        case 2: value = value < 240 ? 0 : static_cast<std::uint8_t>(value - 239); break;
        case 3: break;
        default: value = static_cast<std::uint8_t>(index % 16); break;
        }
        raw[index] = value;
    }
    std::size_t count = cng_huffman_kernel::encode(raw, coded);
    cng_huffman_kernel::decode(std::span<const std::uint8_t>(coded).first(count), restored);
    if (raw != restored || count > size + 5) { throw std::runtime_error("Huffman tail exactness/size"); }
    put_size(vectors, size);
    put_size(vectors, count);
    if (size != 0) { vectors.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(size)); }
    vectors.write(reinterpret_cast<const char*>(coded.data()), static_cast<std::streamsize>(count));
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("Huffman tail vector path required"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("Huffman tail vector open"); }
        constexpr std::array<std::size_t, 15> boundaries{255, 256, 257, 1023, 1024, 1025, 4095, 4096, 4097,
            65535, 65536, 65537, 131071, 131072, 131073};
        for (unsigned pattern = 0; pattern < 5; ++pattern) {
            for (std::size_t size = 0; size <= 65; ++size) { check(size, pattern, vectors); }
            for (std::size_t size : boundaries) { check(size, pattern, vectors); }
        }
        vectors.close();
        if (!vectors) { throw std::runtime_error("Huffman tail vector write"); }
        std::cout << "PASS: 405 Huffman tail/stream-boundary cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
