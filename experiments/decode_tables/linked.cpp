#include "huffman_kernel.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
// Same private interface, linked to the separately compiled preceding object.
namespace cng_huffman_prior {
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output);
}
namespace cng_baseline {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t get32(std::span<const std::uint8_t> bytes, std::size_t position) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(bytes[position + byte]) << (8 * byte);
    }
    return value;
}
}
#include <fstream>
#include <iostream>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
double median(std::array<double, 7> values) {
    std::sort(values.begin(), values.end());
    return values[3];
}
}
int main(int argc, char** argv) {
    try {
        cng_baseline::require(argc == 3, "packets and profile output required");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        cng_baseline::require(static_cast<bool>(input) && static_cast<bool>(rows), "profile files open");
        rows.precision(12);
        rows << "packet,bytes,symbols,baseline,batch\n";
        std::size_t ordinal = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            cng_baseline::require(static_cast<bool>(input), "profile header");
            std::size_t size = cng_baseline::get32(header, 0);
            cng_baseline::require(size >= 5 && size <= 2097160U + 512, "profile packet extent");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            cng_baseline::require(static_cast<bool>(input), "profile packet read");
            std::size_t count = cng_baseline::get32(packet, 1);
            cng_baseline::require(count <= 2097160U, "profile output extent");
            std::vector<std::uint8_t> expected(count);
            std::vector<std::uint8_t> decoded(count);
            cng_huffman_prior::decode(packet, expected);
            std::array<double, 7> baseline{};
            std::array<double, 7> width{};
            for (std::size_t trial = 0; trial < 7; ++trial) {
                for (unsigned order = 0; order < 2; ++order) {
                    bool proposed = ((trial + order) & 1U) != 0;
                    Clock::time_point start = Clock::now();
                    if (proposed) { cng_huffman_kernel::decode(packet, decoded); }
                    else { cng_huffman_prior::decode(packet, decoded); }
                    double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                    if (proposed) { width[trial] = elapsed; }
                    else { baseline[trial] = elapsed; }
                    cng_baseline::require(decoded == expected, "paired packet inverse");
                }
            }
            rows << ordinal << ',' << size << ',' << count << ',' << median(baseline) << ',' << median(width) << '\n';
            ++ordinal;
        }
        rows.close();
        cng_baseline::require(static_cast<bool>(rows), "profile output complete");
        std::cout << "PASS: " << ordinal << " separately linked decoder comparisons\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
