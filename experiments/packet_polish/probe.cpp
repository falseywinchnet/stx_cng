#include "polish.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#define cng_huffman_kernel cng_exact_huffman
#include "../length_limited/huffman_exact.cpp"
#undef cng_huffman_kernel
#define cng_huffman_kernel cng_old_huffman
#include "../entropy_reduction/huffman_reduced.cpp"
#undef cng_huffman_kernel
namespace {
using namespace cng_packet_polish;
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint64_t native_cost(const Counts& counts, const Lengths& lengths) {
    std::array<std::uint8_t, 128> table{};
    std::uint64_t cost = cng_exact_huffman::write_lengths(lengths, table);
    for (unsigned lane = 0; lane < 4; ++lane) {
        std::uint64_t bits = 0;
        for (unsigned symbol = 0; symbol < 256; ++symbol) { bits += static_cast<std::uint64_t>(counts[lane][symbol]) * lengths[symbol]; }
        cost += (bits + 7) / 8;
    }
    return cost;
}
std::uint32_t next(std::uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}
void check_sparse() {
    std::uint32_t state = 0x43504f4cU;
    for (unsigned trial = 0; trial < 30000; ++trial) {
        Lengths lengths{};
        Counts counts{};
        unsigned position = 0;
        while (position < 256) {
            unsigned length = next(state) % 13;
            unsigned count = 1 + next(state) % 40;
            unsigned end = std::min(256U, position + count);
            for (; position < end; ++position) { lengths[position] = static_cast<std::uint8_t>(length); }
        }
        for (unsigned lane = 0; lane < 4; ++lane) {
            for (unsigned symbol = 0; symbol < 256; ++symbol) { counts[lane][symbol] = next(state) % 37; }
        }
        Patch patch{};
        patch.size = 1 + next(state) % 3;
        for (unsigned edit = 0; edit < patch.size; ++edit) {
            // Disjoint thirds give sorted, distinct positions without retries.
            patch.at[edit] = static_cast<std::uint16_t>(85 * edit + next(state) % 85);
            patch.value[edit] = static_cast<std::uint8_t>(next(state) % 13);
        }
        Lengths changed = lengths;
        for (unsigned edit = 0; edit < patch.size; ++edit) { changed[patch.at[edit]] = patch.value[edit]; }
        Map map(lengths);
        Bits bits = lane_bits(counts, lengths);
        require(patch_score(counts, lengths, map, bits, patch) == native_cost(counts, changed), "sparse edit exact native cost");
    }
}
// Emit exactly the existing grammar, with caller-selected complete lengths.
std::size_t emit(std::span<const std::uint8_t> input, const Lengths& lengths, std::span<std::uint8_t> output) {
    std::array<std::uint16_t, 256> codes{};
    cng_exact_huffman::canonical(lengths, codes);
    output[0] = 36;
    cng_exact_huffman::put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t table = cng_exact_huffman::write_lengths(lengths, output.subspan(5, 128));
    std::size_t lanes = cng_exact_huffman::stream_count(input.size());
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
        if (lane + 1 != lanes) { cng_exact_huffman::put32(output, 5 + table + 4 * lane, static_cast<std::uint32_t>(position - begin)); }
    }
    return position;
}
double median(std::array<double, 7> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[3];
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "packet bundle and result CSV required");
        check_sparse();
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        require(static_cast<bool>(input) && static_cast<bool>(rows), "open packet files");
        rows.precision(12);
        rows << "packet,symbols,passes,old_bytes,base_bytes,selected_bytes,table_bytes,payload_bits,padding_bits,lower_bound,gap,proposals,accepted,local_optimum,polish_seconds,count_seconds,baseline_encode_seconds\n";
        std::size_t ordinal = 0;
        std::size_t compressed = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            require(static_cast<bool>(input), "packet header");
            std::size_t size = cng_exact_huffman::get32(header, 0);
            require(size >= 5 && size <= cng_entropy_trial::maximum_input + 512, "packet extent");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            require(static_cast<bool>(input), "packet contents");
            std::size_t count = cng_exact_huffman::get32(packet, 1);
            require(count <= cng_entropy_trial::maximum_input, "decoded extent");
            std::vector<std::uint8_t> raw(count);
            std::vector<std::uint8_t> inverse(count);
            std::vector<std::uint8_t> output(2 * count + 512);
            cng_exact_huffman::decode(packet, raw);
            std::size_t old_size = cng_old_huffman::encode(raw, output);
            std::array<double, 7> baseline_times{};
            for (unsigned trial = 0; trial < 7; ++trial) {
                Clock::time_point start = Clock::now();
                std::size_t written = cng_exact_huffman::encode(raw, output);
                baseline_times[trial] = std::chrono::duration<double>(Clock::now() - start).count();
                require(written == packet.size() && std::equal(packet.begin(), packet.end(), output.begin()), "baseline packet reproduction");
            }
            if (packet[0] != 36) { ++ordinal; continue; }
            ++compressed;
            Counts counts{};
            Lengths original{};
            cng_exact_huffman::read_lengths(std::span<const std::uint8_t>(packet).subspan(5), original);
            std::size_t lanes = cng_exact_huffman::stream_count(count);
            std::size_t quarter = count / lanes;
            Clock::time_point count_start = Clock::now();
            for (std::size_t lane = 0; lane < lanes; ++lane) {
                std::size_t first = lane * quarter;
                std::size_t last = lane + 1 == lanes ? count : first + quarter;
                for (std::size_t index = first; index < last; ++index) { ++counts[lane][raw[index]]; }
            }
            double count_seconds = std::chrono::duration<double>(Clock::now() - count_start).count();
            Bits original_bits = lane_bits(counts, original);
            std::uint64_t minimum_bits = 0;
            for (std::int64_t bits : original_bits) { minimum_bits += static_cast<std::uint64_t>(bits); }
            Lengths mask{};
            unsigned active = 0;
            for (unsigned symbol = 0; symbol < 256; ++symbol) {
                if (original[symbol] == 0) { continue; }
                mask[symbol] = 1;
                ++active;
            }
            std::array<std::uint8_t, 128> table{};
            std::uint64_t lower_bound = cng_exact_huffman::write_lengths(mask, table);
            lower_bound += (minimum_bits + 7) / 8;
            std::uint64_t fixed = 5 + 4 * (lanes - 1);
            lower_bound += fixed;
            lower_bound = std::min(lower_bound, static_cast<std::uint64_t>(count + 5));
            std::uint64_t preceding_size = size;
            for (unsigned passes = 0; passes <= 2; ++passes) {
                Lengths changed{};
                Result result{};
                std::array<double, 7> times{};
                for (unsigned trial = 0; trial < 7; ++trial) {
                    changed = original;
                    Clock::time_point start = Clock::now();
                    result = improve(counts, changed, passes);
                    times[trial] = std::chrono::duration<double>(Clock::now() - start).count();
                }
                require(result.proposals <= passes * (4 * active - 7), "bounded proposals");
                require(result.after == native_cost(counts, changed), "native packet score");
                require(result.after <= result.before, "polish nonregression");
                for (unsigned symbol = 0; symbol < 256; ++symbol) {
                    require((original[symbol] == 0) == (changed[symbol] == 0), "fixed support");
                }
                std::size_t emitted = emit(raw, changed, output);
                require(emitted == result.after + fixed, "exact serialized size");
                cng_exact_huffman::decode(std::span<const std::uint8_t>(output).first(emitted), inverse);
                require(raw == inverse, "polished native inverse");
                if (passes == 0) { require(emitted == size && std::equal(packet.begin(), packet.end(), output.begin()), "zero pass native bytes"); }
                std::uint64_t selected = std::min(static_cast<std::uint64_t>(emitted), static_cast<std::uint64_t>(count + 5));
                require(selected <= preceding_size && lower_bound <= selected, "monotone size and lower bound");
                preceding_size = selected;
                Bits bits = lane_bits(counts, changed);
                std::uint64_t payload = 0;
                std::uint64_t padding = 0;
                for (std::int64_t lane : bits) {
                    payload += static_cast<std::uint64_t>(lane);
                    padding += static_cast<std::uint64_t>((8 - lane % 8) % 8);
                }
                std::size_t table_bytes = cng_exact_huffman::write_lengths(changed, table);
                rows << ordinal << ',' << count << ',' << passes << ',' << old_size << ',' << size << ',' << selected
                     << ',' << table_bytes << ',' << payload << ',' << padding << ',' << lower_bound << ',' << selected - lower_bound
                     << ',' << result.proposals << ',' << result.accepted << ',' << result.local_optimum << ',' << median(times)
                     << ',' << count_seconds << ',' << median(baseline_times) << '\n';
            }
            ++ordinal;
        }
        rows.close();
        require(static_cast<bool>(rows), "complete result write");
        std::cout << "PASS: 30000 sparse scores; " << ordinal << " retained packets; " << compressed << " compressed packets at zero, one and two passes\n";
        std::cout << "map_bytes=" << sizeof(Map) << " lane_counts_bytes=" << sizeof(Counts) << " patch_bytes=" << sizeof(Patch) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
