#include "rans.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void write_size(std::ostream& output, std::size_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        output.put(static_cast<char>((value >> (8 * index)) & 255U));
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("test vector output path required"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("test vector file"); }
        constexpr std::array<std::size_t, 17> sizes{0, 1, 2, 3, 4, 5, 7, 15, 16, 31,
            255, 256, 257, 4095, 4096, 65537, cng_entropy_trial::maximum_input};
        std::size_t cases = 0;
        for (std::size_t size : sizes) {
            for (unsigned pattern = 0; pattern < 6; ++pattern) {
                std::vector<std::uint8_t> input(size);
                std::uint32_t state = 20261022;
                for (std::size_t index = 0; index < size; ++index) {
                    state = state * 1664525U + 1013904223U;
                    std::uint8_t value = static_cast<std::uint8_t>(state >> 24);
                    if (pattern == 0) { value = 0; }
                    if (pattern == 1) { value = static_cast<std::uint8_t>(index & 255U); }
                    if (pattern == 2) { value &= 1U; }
                    if (pattern == 3) { value = index % 997 == 0 ? value : 255; }
                    if (pattern == 4) { value &= 31U; }
                    input[index] = value;
                }
                std::vector<std::uint8_t> encoded(2 * size + 1024);
                std::vector<std::uint8_t> output(size);
                std::size_t count = cng_entropy_trial::encode(input, encoded);
                cng_entropy_trial::decode(std::span<const std::uint8_t>(encoded).first(count), output);
                if (output != input || count > size + 5) { throw std::runtime_error("entropy exactness/size bound"); }
                if (size <= 65537) {
                    write_size(vectors, size);
                    write_size(vectors, count);
                    if (size != 0) { vectors.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(size)); }
                    vectors.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(count));
                }
                ++cases;
            }
        }
        vectors.close();
        if (!vectors) { throw std::runtime_error("test vector write"); }
        std::cout << "PASS: " << cases << " deterministic entropy cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
