#include "events.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void write_size(std::ostream& output, std::size_t size) {
    for (unsigned byte = 0; byte < 4; ++byte) { output.put(static_cast<char>(size >> (8 * byte))); }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("event vector output required"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("event vectors open"); }
        constexpr std::array<std::size_t, 16> counts{0, 1, 2, 3, 4, 5, 7, 15, 16, 31, 255, 256, 1023, 1024, 8193, 87382};
        constexpr std::array<std::uint32_t, 18> boundaries{0, 1, 3, 4, 7, 8, 15, 16, 17, 31, 32, 63, 64, 255, 256, 65535, 65536, 1048576};
        std::size_t cases = 0;
        for (std::size_t count : counts) {
            for (unsigned pattern = 0; pattern < 7; ++pattern) {
                std::vector<std::uint8_t> raw(12 * count);
                std::vector<std::uint8_t> decoded(raw.size());
                std::vector<std::uint8_t> encoded(2 * raw.size() + 2048);
                std::vector<std::uint8_t> scratch(2 * raw.size() + 64);
                std::uint32_t state = 20261022;
                for (std::size_t field = 0; field < 3; ++field) {
                    for (std::size_t index = 0; index < count; ++index) {
                        state = state * 1664525U + 1013904223U;
                        std::uint32_t value = (state >> 8) % 1048577U;
                        if (pattern == 0) { value = 0; }
                        if (pattern == 1) { value = 1048576; }
                        if (pattern == 2) { value = boundaries[index % boundaries.size()]; }
                        if (pattern == 3) { value = static_cast<std::uint32_t>(index % 256); }
                        if (pattern == 4) { value = index % 3 == 0 ? 0 : 255; }
                        if (pattern == 5) { value &= 15U; }
                        if (field == 1 && value != 0 && value < 4) { value = 4; }
                        for (unsigned byte = 0; byte < 4; ++byte) {
                            raw[(4 * field + byte) * count + index] = static_cast<std::uint8_t>(value >> (8 * byte));
                        }
                    }
                }
                std::size_t size = cng_event_trial::encode(raw, encoded, scratch);
                cng_event_trial::decode(std::span<const std::uint8_t>(encoded).first(size), decoded);
                if (raw != decoded) { throw std::runtime_error("event exact inverse"); }
                if (count <= 8193) {
                    write_size(vectors, raw.size());
                    write_size(vectors, size);
                    if (!raw.empty()) { vectors.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size())); }
                    vectors.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(size));
                }
                ++cases;
            }
        }
        vectors.close();
        if (!vectors) { throw std::runtime_error("event vectors write"); }
        std::cout << "PASS: " << cases << " direct-event inverses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
