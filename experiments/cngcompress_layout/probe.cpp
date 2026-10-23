#include "layout.hpp"
#include "huffman_kernel.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#define cng_huffman_kernel cng_model
#include "../length_limited/huffman_exact.cpp"
#undef cng_huffman_kernel
namespace {
using Lengths = std::array<std::uint8_t, 256>;
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
// Emit exactly the existing grammar, with caller-selected complete lengths.
std::size_t emit(std::span<const std::uint8_t> input, const Lengths& lengths, std::span<std::uint8_t> output) {
    std::array<std::uint16_t, 256> codes{};
    cng_model::canonical(lengths, codes);
    output[0] = 36;
    cng_model::put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t table = cng_model::write_lengths(lengths, output.subspan(5, 128));
    std::size_t lanes = cng_model::stream_count(input.size());
    std::size_t position = 5 + table + 4 * (lanes - 1);
    std::size_t quarter = input.size() / lanes;
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        std::size_t begin = position;
        std::size_t first = lane * quarter;
        std::size_t last = lane + 1 == lanes ? input.size() : first + quarter;
        std::uint64_t pending = 0;
        unsigned live = 0;
        for (std::size_t index = first; index < last; ++index) {
            std::uint8_t symbol = input[index];
            pending |= static_cast<std::uint64_t>(codes[symbol]) << live;
            live += lengths[symbol];
            while (live >= 8) {
                output[position] = static_cast<std::uint8_t>(pending);
                ++position;
                pending >>= 8;
                live -= 8;
            }
        }
        if (live != 0) { output[position] = static_cast<std::uint8_t>(pending); ++position; }
        if (lane + 1 != lanes) { cng_model::put32(output, 5 + table + 4 * lane, static_cast<std::uint32_t>(position - begin)); }
    }
    return position;
}
double median(std::array<double, 7> values) {
    std::sort(values.begin(), values.end());
    return values[3];
}
}
namespace {
void fixtures() {
    for (unsigned width = 1; width <= 8; ++width) {
        Lengths lengths{};
        std::fill_n(lengths.data(), 1U << width, static_cast<std::uint8_t>(width));
        for (std::size_t count = 0; count < 35; ++count) {
            std::size_t size = count < 33 ? count : count == 33 ? 1025 : 131073;
            std::vector<std::uint8_t> input(size);
            for (std::size_t position = 0; position < size; ++position) {
                input[position] = static_cast<std::uint8_t>((position * 71 + position / 3) & ((1U << width) - 1));
            }
            std::vector<std::uint8_t> encoded(2 * size + 1024);
            std::vector<std::uint8_t> workspace(2 * size);
            std::vector<std::uint8_t> decoded(size);
            std::size_t bytes = cngcompress_layout::encode(input, lengths, encoded, workspace);
            cngcompress_layout::decode(std::span<const std::uint8_t>(encoded).first(bytes), decoded, workspace);
            require(input == decoded, "flat boundary fixture");
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "packet bundle and fresh CSV required");
        std::ifstream existing(argv[2]);
        require(!existing.good(), "refuse result overwrite");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        require(static_cast<bool>(input) && static_cast<bool>(rows), "layout files");
        fixtures();
        rows.precision(12);
        rows << "packet,symbols,common_bits,classic_bytes,layout_bytes,classic_encode,layout_encode,classic_decode,layout_decode\n";
        std::size_t ordinal = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            require(static_cast<bool>(input), "packet header");
            std::size_t size = cng_model::get32(header, 0);
            require(size >= 5 && size <= cng_entropy_trial::maximum_input + 512, "packet size");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            require(static_cast<bool>(input), "packet body");
            std::size_t count = cng_model::get32(packet, 1);
            require(count <= cng_entropy_trial::maximum_input, "raw size");
            std::vector<std::uint8_t> raw(count);
            cng_huffman_kernel::decode(packet, raw);
            std::array<std::uint32_t, 256> populations{};
            for (std::uint8_t value : raw) { ++populations[value]; }
            Lengths lengths{};
            cng_model::make_lengths(populations, lengths);
            std::size_t bits = 0;
            for (unsigned symbol = 0; symbol < 256; ++symbol) { bits += populations[symbol] * lengths[symbol]; }
            std::vector<std::uint8_t> classic(2 * count + 1024);
            std::vector<std::uint8_t> layout(2 * count + 1024);
            std::vector<std::uint8_t> workspace(2 * count);
            std::vector<std::uint8_t> decoded(count);
            std::size_t classic_bytes = emit(raw, lengths, classic);
            std::size_t layout_bytes = cngcompress_layout::encode(raw, lengths, layout, workspace);
            std::array<std::array<double, 7>, 4> times{};
            for (std::size_t trial = 0; trial < 8; ++trial) {
                for (std::size_t order = 0; order < 4; ++order) {
                    std::size_t method = (trial + ordinal + order) % 4;
                    Clock::time_point start = Clock::now();
                    switch (method) {
                        case 0: classic_bytes = emit(raw, lengths, classic); break;
                        case 1: layout_bytes = cngcompress_layout::encode(raw, lengths, layout, workspace); break;
                        case 2: cng_huffman_kernel::decode(std::span<const std::uint8_t>(classic).first(classic_bytes), decoded); break;
                        case 3: cngcompress_layout::decode(std::span<const std::uint8_t>(layout).first(layout_bytes), decoded, workspace); break;
                        default: require(false, "timing method"); break;
                    }
                    double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                    if (trial != 0) { times[method][trial - 1] = elapsed; }
                    if (method >= 2) { require(decoded == raw, "complete layout inverse"); }
                }
            }
            rows << ordinal << ',' << count << ',' << bits << ',' << classic_bytes << ',' << layout_bytes;
            for (unsigned method = 0; method < 4; ++method) { rows << ',' << median(times[method]); }
            rows << '\n';
            ++ordinal;
            if (ordinal % 500 == 0) { std::cout << "Compared " << ordinal << " packets\n" << std::flush; }
        }
        rows.close();
        require(static_cast<bool>(rows), "complete CSV");
        std::cout << "PASS: 280 flat/boundary fixtures; " << ordinal << " complete packet comparisons\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
