#include "match_events_bounded.hpp"
#define cng_huffman_kernel cng_huffman_reference
// Diagnostic inclusion exposes the exact existing length and table rules.
#include "../entropy/huffman_tail_split.cpp"
#undef cng_huffman_kernel
#include <fstream>
#include <iostream>
#include <vector>
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void write32(std::ostream& stream, std::size_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { stream.put(static_cast<char>(value >> (8 * byte))); }
}
// Count only: no canonical codes, bit emission or encoded-body buffer. Four
// contiguous stream populations capture the only order-sensitive size term.
std::size_t huffman_size(std::span<const std::uint8_t> input) {
    if (input.empty()) { return 5; }
    std::array<std::array<std::uint32_t, 256>, 4> lane_counts{};
    std::size_t lanes = cng_huffman_reference::stream_count(input.size());
    std::size_t quarter = input.size() / lanes;
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        std::size_t first = lane * quarter;
        std::size_t last = lane + 1 == lanes ? input.size() : first + quarter;
        for (std::size_t index = first; index < last; ++index) { ++lane_counts[lane][input[index]]; }
    }
    std::array<std::uint32_t, 256> counts{};
    std::size_t active = 0;
    for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
        for (std::size_t lane = 0; lane < lanes; ++lane) { counts[symbol] += lane_counts[lane][symbol]; }
        if (counts[symbol] != 0) { ++active; }
    }
    if (active == 1) { return 6; }
    std::array<std::uint8_t, 256> lengths{};
    cng_huffman_reference::make_lengths(counts, lengths);
    std::array<std::uint8_t, 128> table{};
    std::size_t table_size = cng_huffman_reference::write_lengths(lengths, table);
    std::size_t size = 5 + table_size + 4 * (lanes - 1);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        std::size_t bits = 0;
        for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
            bits += static_cast<std::size_t>(lane_counts[lane][symbol]) * lengths[symbol];
        }
        size += (bits + 7) / 8;
    }
    std::size_t result = std::min(size, input.size() + 5);
    return result;
}
void exact_count(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> output(2 * input.size() + 512);
    std::size_t expected = cng_huffman_reference::encode(input, output);
    std::size_t counted = huffman_size(input);
    require(expected == counted, "exact Huffman count");
}
std::size_t check_packets(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "packet bundle open");
    std::size_t cases = 0;
    while (stream.peek() != std::char_traits<char>::eof()) {
        std::array<std::uint8_t, 4> header{};
        stream.read(reinterpret_cast<char*>(header.data()), 4);
        require(static_cast<bool>(stream), "packet bundle header");
        std::size_t size = cng_huffman_reference::get32(header, 0);
        require(size >= 5 && size <= 2 * cng_entropy_trial::maximum_input + 512, "packet bundle extent");
        std::vector<std::uint8_t> packet(size);
        stream.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
        require(static_cast<bool>(stream), "packet bundle read");
        std::size_t count = cng_huffman_reference::get32(packet, 1);
        require(count <= cng_entropy_trial::maximum_input, "packet sample bound");
        std::vector<std::uint8_t> input(count);
        cng_huffman_reference::decode(packet, input);
        require(huffman_size(input) == size, "corpus exact Huffman count");
        ++cases;
    }
    return cases;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3, "witness output path and optional packet bundle required");
        std::ofstream witness(argv[1], std::ios::binary);
        require(static_cast<bool>(witness), "witness output open");
        constexpr std::array<std::array<std::uint32_t, 18>, 2> literals{{
            {1,0,0,0,0,0,1000,0,0,16,0,1,1000,16,0,0,1,0},
            {1,0,0,0,16,0,0,0,0,0,0,1000,1,1000,0,0,16,1}
        }};
        std::array<std::size_t, 2> sizes{};
        for (std::size_t variant = 0; variant < literals.size(); ++variant) {
            std::array<cng_match_event::Event, 18> events{};
            std::array<cng_match_event::Event, 18> decoded{};
            std::array<std::uint8_t, 14 * 18 + 1024> coded{};
            std::array<std::uint8_t, 11 * 18 + 64> scratch{};
            std::size_t samples = 0;
            for (std::size_t index = 0; index < events.size(); ++index) {
                events[index] = cng_match_event::Event{literals[variant][index], 3, 1};
                samples += literals[variant][index];
                require(samples >= 1, "legal zero-image reference");
                samples += 3;
            }
            require(samples == 2089, "same witness source");
            sizes[variant] = cng_match_event::encode(events, coded, scratch);
            cng_match_event::decode(std::span<const std::uint8_t>(coded).first(sizes[variant]), decoded);
            for (std::size_t index = 0; index < events.size(); ++index) {
                require(events[index].literals == decoded[index].literals && decoded[index].length == 3
                    && decoded[index].distance == 1, "witness inverse");
            }
            write32(witness, sizes[variant]);
            witness.write(reinterpret_cast<const char*>(coded.data()), static_cast<std::streamsize>(sizes[variant]));
        }
        require(sizes[0] == 42 && sizes[1] == 44, "rANS order witness");
        witness.close();
        require(static_cast<bool>(witness), "witness output write");
        std::size_t cases = 0;
        for (std::size_t size : {0U,1U,2U,3U,4U,7U,15U,255U,256U,1023U,1024U,1025U,131071U,131072U,131073U,2097160U}) {
            for (std::uint32_t alphabet : {1U,2U,3U,7U,16U,31U,127U,255U,256U}) {
                for (unsigned pattern = 0; pattern < 3; ++pattern) {
                    std::vector<std::uint8_t> input(size);
                    std::uint32_t state = 20261023;
                    for (std::size_t index = 0; index < size; ++index) {
                        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                        std::uint8_t value = static_cast<std::uint8_t>(state % alphabet);
                        if (pattern == 1 && index < size / 2) { value = 0; }
                        if (pattern == 2 && index % 31 != 0) { value = 0; }
                        input[index] = value;
                    }
                    exact_count(input);
                    ++cases;
                }
            }
        }
        std::cout << "PASS: native rANS witness 42/44 bytes; " << cases << " exact Huffman size counts\n";
        if (argc == 3) {
            std::size_t packets = check_packets(argv[2]);
            std::cout << "PASS: " << packets << " actual corpus Huffman packet sizes\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
