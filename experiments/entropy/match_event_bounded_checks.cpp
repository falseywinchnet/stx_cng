#include "match_events_bounded.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void write32(std::ostream& output, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { output.put(static_cast<char>(value >> (8 * byte))); }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("event vector output"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("event vector open"); }
        constexpr std::array<std::size_t, 12> sizes{0, 1, 2, 3, 4, 5, 7, 31, 128, 1024, 43691, 87382};
        constexpr std::array<std::uint32_t, 18> edges{0, 1, 2, 3, 15, 16, 17, 31, 32, 33, 65535, 65536, 65537, 1048575, 1048576, 2097151, 2097152, 2097160};
        std::size_t cases = 0;
        for (std::size_t count : sizes) {
            for (unsigned pattern = 0; pattern < 5; ++pattern) {
                std::vector<cng_match_event::Event> input(count);
                std::vector<cng_match_event::Event> output(count);
                std::vector<std::uint8_t> packet(14 * count + 1024);
                std::vector<std::uint8_t> scratch(11 * count + 64);
                std::uint32_t state = 20261023;
                for (std::size_t index = 0; index < count; ++index) {
                    cng_match_event::Event& event = input[index];
                    if (pattern == 1) { event = cng_match_event::Event{0, 3, 1}; }
                    if (pattern == 2) {
                        state = state * 1664525U + 1013904223U;
                        event.literals = state % 2097161U;
                        state = state * 1664525U + 1013904223U;
                        event.length = state % 2097158U + 3;
                        state = state * 1664525U + 1013904223U;
                        event.distance = state % 2097161U;
                    }
                    if (pattern == 4) {
                        constexpr std::array<std::uint32_t, 7> repeated{17, 23, 31, 17, 31, 23, 0};
                        event = cng_match_event::Event{static_cast<std::uint32_t>(index % 5), 3, repeated[index % repeated.size()]};
                    }
                    if (pattern == 3) {
                        event.literals = edges[index % edges.size()];
                        event.length = edges[(index + 5) % edges.size()];
                        if (event.length != 0 && event.length < 3) { event.length += 3; }
                        event.distance = edges[(index / 3) % edges.size()];
                    }
                }
                std::size_t size = cng_match_event::encode(input, packet, scratch);
                cng_match_event::decode(std::span<const std::uint8_t>(packet).first(size), output);
                for (std::size_t index = 0; index < count; ++index) {
                    if (input[index].literals != output[index].literals || input[index].length != output[index].length
                        || input[index].distance != output[index].distance) { throw std::runtime_error("event exactness"); }
                }
                write32(vectors, static_cast<std::uint32_t>(12 * count));
                write32(vectors, static_cast<std::uint32_t>(size));
                for (const cng_match_event::Event& event : input) {
                    write32(vectors, event.literals); write32(vectors, event.length); write32(vectors, event.distance);
                }
                vectors.write(reinterpret_cast<const char*>(packet.data()), static_cast<std::streamsize>(size));
                ++cases;
            }
        }
        vectors.close();
        if (!vectors) { throw std::runtime_error("event vector write"); }
        std::cout << "PASS: " << cases << " integer-event cases\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
