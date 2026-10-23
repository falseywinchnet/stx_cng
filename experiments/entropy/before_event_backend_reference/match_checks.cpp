#ifdef CNG_SPLIT_MATCH
#include "match_split.hpp"
#else
#include "match_huffman.hpp"
#endif
#include "rans.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
void put_size(std::ostream& output, std::size_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { output.put(static_cast<char>(value >> (8 * byte))); }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("match vector output required"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("match vector open"); }
        std::unique_ptr<cng_match_trial::EncodeWorkspace> encoder = std::make_unique<cng_match_trial::EncodeWorkspace>();
        std::unique_ptr<cng_match_trial::DecodeWorkspace> decoder = std::make_unique<cng_match_trial::DecodeWorkspace>();
        constexpr std::array<std::size_t, 24> sizes{0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 255, 256, 257, 65534, 65535, 65536, 65537,
            131071, 131072, 131073, 262143, 262144, 262145, cng_entropy_trial::maximum_input};
        constexpr std::array<std::size_t, 9> periods{0, 1, 2, 3, 7, 257, 65535, 65536, 131072};
        std::size_t cases = 0;
        for (std::size_t size : sizes) {
            for (std::size_t period : periods) {
                for (unsigned disturbances = 0; disturbances < 2; ++disturbances) {
                    std::vector<std::uint8_t> raw(size);
                    std::vector<std::uint8_t> coded(2 * size + 1024);
                    std::vector<std::uint8_t> decoded(size);
                    std::uint32_t state = 20261022;
                    for (std::size_t index = 0; index < size; ++index) {
                        state = state * 1664525U + 1013904223U;
                        std::uint8_t value = static_cast<std::uint8_t>(state >> 24);
                        if (period != 0 && index >= period) { value = raw[index - period]; }
                        if (disturbances != 0 && index % 1031 == 0) { value ^= 77U; }
                        raw[index] = value;
                    }
                    std::size_t count = cng_match_trial::encode(raw, coded, *encoder);
                    cng_match_trial::decode(std::span<const std::uint8_t>(coded).first(count), decoded, *decoder);
                    if (raw != decoded || count > size + 5) { throw std::runtime_error("match exactness/size bound"); }
                    if (size <= 262145) {
                        put_size(vectors, size);
                        put_size(vectors, count);
                        if (!raw.empty()) { vectors.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size())); }
                        vectors.write(reinterpret_cast<const char*>(coded.data()), static_cast<std::streamsize>(count));
                    }
                    ++cases;
                }
            }
        }
        vectors.close();
        if (!vectors) { throw std::runtime_error("match vector write"); }
        std::cout << "PASS: " << cases << " deterministic match cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
