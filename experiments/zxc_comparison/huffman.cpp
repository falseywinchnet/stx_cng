#include "huffman_bridge.h"
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
int main(int argc, char** argv) {
    try {
        require(argc == 3, "packet bundle and new result CSV required");
        std::ifstream existing(argv[2]);
        require(!existing.good(), "refuse result overwrite");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        require(static_cast<bool>(input) && static_cast<bool>(rows), "layout files open");
        rows.precision(12);
        rows << "packet,symbols,common_bits,cap_changed,classic_bytes,pivco_bytes,classic_decode,pivco_decode\n";
        std::size_t ordinal = 0;
        std::size_t compared = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            require(static_cast<bool>(input), "packet header");
            std::size_t size = cng_model::get32(header, 0);
            require(size >= 5 && size <= cng_entropy_trial::maximum_input + 512, "packet extent");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            require(static_cast<bool>(input), "packet read");
            std::size_t count = cng_model::get32(packet, 1);
            require(count <= cng_entropy_trial::maximum_input, "symbol extent");
            std::vector<std::uint8_t> raw(count);
            cng_huffman_kernel::decode(packet, raw);
            std::array<std::uint32_t, 256> populations{};
            for (std::uint8_t value : raw) { ++populations[value]; }
            std::size_t active = 0;
            for (std::uint32_t population : populations) { active += population != 0; }
            if (active < 2) { ++ordinal; continue; }
            Lengths lengths{};
            cng_model::make_lengths(populations, lengths);
            unsigned maximum = 0;
            for (std::uint8_t length : lengths) { maximum = std::max(maximum, static_cast<unsigned>(length)); }
            bool cap_changed = maximum > 11;
            if (cap_changed) {
                std::array<cng_model::Leaf, 256> leaves{};
                std::size_t live = 0;
                for (std::size_t symbol = 0; symbol < 256; ++symbol) {
                    if (populations[symbol] == 0) { continue; }
                    leaves[live] = cng_model::Leaf{populations[symbol], static_cast<std::uint16_t>(symbol)};
                    ++live;
                }
                std::sort(leaves.begin(), leaves.begin() + live, cng_model::Before{});
                std::array<cng_package_merge::Symbol, 256> sorted{};
                for (std::size_t index = 0; index < live; ++index) { sorted[index] = cng_package_merge::Symbol{leaves[index].weight, leaves[index].symbol}; }
                cng_package_merge::lengths(std::span<const cng_package_merge::Symbol>(sorted).first(live), 11, lengths);
            }
            std::uint64_t bits = 0;
            for (std::size_t symbol = 0; symbol < 256; ++symbol) { bits += static_cast<std::uint64_t>(populations[symbol]) * lengths[symbol]; }
            std::vector<std::uint8_t> classic(2 * count + 1024);
            std::vector<std::uint8_t> pivco(2 * count + 1024);
            std::vector<std::uint8_t> decoded(count + 32);
            std::vector<std::uint8_t> scratch(count + 32);
            std::size_t classic_size = emit(raw, lengths, classic);
            int pivco_result = cng_zxc_huffman_encode(raw.data(), count, populations.data(), lengths.data(), pivco.data(), pivco.size());
            require(pivco_result > 0, "PivCo encode");
            std::size_t pivco_size = static_cast<std::size_t>(pivco_result);
            std::array<double, 7> classic_times{};
            std::array<double, 7> pivco_times{};
            for (std::size_t trial = 0; trial < 8; ++trial) {
                for (std::size_t order = 0; order < 2; ++order) {
                    bool proposed = ((ordinal + trial + order) & 1U) != 0;
                    Clock::time_point start = Clock::now();
                    if (proposed) {
                        int result = cng_zxc_huffman_decode(pivco.data(), pivco_size, decoded.data(), count, scratch.data());
                        require(result == 0, "PivCo decode");
                    } else {
                        cng_huffman_kernel::decode(std::span<const std::uint8_t>(classic).first(classic_size), std::span<std::uint8_t>(decoded).first(count));
                    }
                    double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                    require(std::equal(raw.begin(), raw.end(), decoded.begin()), "common-length layout inverse");
                    if (trial != 0) {
                        if (proposed) { pivco_times[trial - 1] = elapsed; }
                        else { classic_times[trial - 1] = elapsed; }
                    }
                }
            }
            rows << ordinal << ',' << count << ',' << bits << ',' << cap_changed << ',' << classic_size << ',' << pivco_size
                 << ',' << median(classic_times) << ',' << median(pivco_times) << '\n';
            ++ordinal;
            ++compared;
        }
        rows.close();
        require(static_cast<bool>(rows), "layout CSV complete");
        std::cout << "PASS: " << compared << " common-code layout comparisons from " << ordinal << " packets\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
