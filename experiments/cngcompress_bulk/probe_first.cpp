#include "merge.hpp"
#include "huffman_kernel.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#define cng_huffman_kernel cng_model
#include "../length_limited/huffman_exact.cpp"
#undef cng_huffman_kernel
namespace {
using Clock = std::chrono::steady_clock;
using Kernel = decltype(&cngcompress_bulk::merge_scalar);
constexpr std::array<Kernel, 4> kernels{cngcompress_bulk::merge_scalar, cngcompress_bulk::merge_indexed,
                                      cngcompress_bulk::merge_words, cngcompress_bulk::merge_spread};
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void fixture(std::uint32_t pattern, std::size_t count) {
    std::array<std::uint8_t, 4> bitmap{};
    std::array<std::uint8_t, 32> left{};
    std::array<std::uint8_t, 32> right{};
    std::array<std::uint8_t, 32> expected{};
    std::array<std::uint8_t, 32> decoded{};
    std::size_t l = 0;
    std::size_t r = 0;
    for (std::size_t index = 0; index < count; ++index) {
        std::uint8_t value = static_cast<std::uint8_t>(17 * index + 39);
        expected[index] = value;
        unsigned bit = (pattern >> index) & 1U;
        bitmap[index / 8] |= static_cast<std::uint8_t>(bit << (index % 8));
        if (bit == 0) { left[l] = value; ++l; }
        else { right[r] = value; ++r; }
    }
    for (Kernel kernel : kernels) {
        decoded.fill(255);
        kernel(std::span<const std::uint8_t>(bitmap).first((count + 7) / 8),
               std::span<const std::uint8_t>(left).first(l), std::span<const std::uint8_t>(right).first(r),
               std::span<std::uint8_t>(decoded).first(count));
        require(std::equal(expected.begin(), expected.begin() + count, decoded.begin()), "merge exactness");
        for (std::size_t index = count; index < decoded.size(); ++index) { require(decoded[index] == 255, "exact output extent"); }
    }
}
std::size_t checks() {
    std::size_t count = 0;
    for (std::uint32_t pattern = 0; pattern < 65536; ++pattern) { fixture(pattern, 16); ++count; }
    for (std::uint32_t pattern = 0; pattern < 256; ++pattern) {
        for (std::size_t size = 0; size <= 32; ++size) { fixture(pattern * UINT32_C(0x01010101), size); ++count; }
    }
    return count;
}
struct Node {
    std::array<std::uint16_t, 2> child{511, 511};
    unsigned depth = 0;
    unsigned flat = 0;
    std::uint16_t symbol = 256;
};
struct Prepared {
    std::vector<std::uint8_t> values;
    std::vector<std::uint8_t> bitmap;
    std::vector<std::uint8_t> decoded;
};
struct CorpusCase {
    std::array<Node, 511> nodes{};
    std::array<Prepared, 511> data{};
    std::array<std::uint16_t, 255> runs{};
    std::size_t node_count = 1;
    std::size_t run_count = 0;
    std::size_t constant_children = 0;
    cngcompress_bulk::Traffic accesses{};
};
void prepare(std::span<const std::uint8_t> raw, const std::array<std::uint8_t, 256>& lengths,
             CorpusCase& result) {
    std::array<std::uint16_t, 256> codes{};
    cng_model::canonical(lengths, codes);
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        unsigned length = lengths[symbol];
        if (length == 0) { continue; }
        std::uint16_t node = 0;
        for (unsigned depth = 0; depth < length; ++depth) {
            unsigned bit = (codes[symbol] >> depth) & 1U;
            if (result.nodes[node].child[bit] == 511) {
                require(result.node_count < 511, "tree node capacity");
                result.nodes[node].child[bit] = static_cast<std::uint16_t>(result.node_count);
                result.nodes[result.node_count].depth = depth + 1;
                ++result.node_count;
            }
            node = result.nodes[node].child[bit];
        }
        result.nodes[node].symbol = static_cast<std::uint16_t>(symbol);
    }
    for (std::size_t index = result.node_count; index != 0;) {
        --index;
        Node& node = result.nodes[index];
        if (node.symbol != 256) { continue; }
        require(node.child[0] < result.node_count && node.child[1] < result.node_count, "complete tree children");
        const Node& left = result.nodes[node.child[0]];
        const Node& right = result.nodes[node.child[1]];
        bool left_flat = left.symbol != 256 || left.flat != 0;
        bool right_flat = right.symbol != 256 || right.flat != 0;
        if (left_flat && right_flat && left.flat == right.flat) { node.flat = left.flat + 1; }
    }
    result.data[0].values.assign(raw.begin(), raw.end());
    for (std::size_t index = 0; index < result.node_count; ++index) {
        const Node& node = result.nodes[index];
        Prepared& data = result.data[index];
        if (data.values.empty() || node.symbol != 256 || node.flat != 0) { continue; }
        Prepared& left = result.data[node.child[0]];
        Prepared& right = result.data[node.child[1]];
        std::size_t right_count = 0;
        for (std::uint8_t value : data.values) { right_count += (codes[value] >> node.depth) & 1U; }
        left.values.resize(data.values.size() - right_count);
        right.values.resize(right_count);
        data.bitmap.resize((data.values.size() + 7) / 8, 0);
        data.decoded.resize(data.values.size());
        std::size_t l = 0;
        std::size_t r = 0;
        for (std::size_t position = 0; position < data.values.size(); ++position) {
            std::uint8_t value = data.values[position];
            unsigned bit = (codes[value] >> node.depth) & 1U;
            data.bitmap[position / 8] |= static_cast<std::uint8_t>(bit << (position % 8));
            if (bit == 0) { left.values[l] = value; ++l; }
            else { right.values[r] = value; ++r; }
        }
        require(l == left.values.size() && r == right.values.size(), "prepared run counts");
        require(result.run_count < result.runs.size(), "run capacity");
        result.runs[result.run_count] = static_cast<std::uint16_t>(index);
        ++result.run_count;
        result.constant_children += result.nodes[node.child[0]].symbol != 256;
        result.constant_children += result.nodes[node.child[1]].symbol != 256;
        cngcompress_bulk::Traffic traffic = cngcompress_bulk::traffic(data.bitmap, l, r);
        result.accesses.bitmap += traffic.bitmap;
        result.accesses.output += traffic.output;
        result.accesses.selected_input += traffic.selected_input;
        result.accesses.word_input += traffic.word_input;
        result.accesses.selector_table += traffic.selector_table;
        result.accesses.spread_table += traffic.spread_table;
        result.accesses.mixed_groups += traffic.mixed_groups;
        result.accesses.pure_groups += traffic.pure_groups;
    }
}
double execute(CorpusCase& sample, Kernel kernel) {
    Clock::time_point start = Clock::now();
    for (std::size_t run = 0; run < sample.run_count; ++run) {
        std::size_t index = sample.runs[run];
        const Node& node = sample.nodes[index];
        Prepared& data = sample.data[index];
        kernel(data.bitmap, sample.data[node.child[0]].values, sample.data[node.child[1]].values, data.decoded);
    }
    double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    for (std::size_t run = 0; run < sample.run_count; ++run) {
        const Prepared& data = sample.data[sample.runs[run]];
        require(data.values == data.decoded, "corpus merge inverse");
    }
    return elapsed;
}
double median(std::array<double, 7> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[3];
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "packet bundle and fresh CSV required");
        std::ifstream existing(argv[2]);
        require(!existing.good(), "refuse result overwrite");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        require(static_cast<bool>(input) && static_cast<bool>(rows), "probe files");
        std::size_t fixtures = checks();
        rows.precision(12);
        rows << "packet,symbols,runs,constant_children,merge_bytes,bitmap_bytes,word_input_bytes,selector_bytes,spread_bytes,mixed_groups,pure_groups,scalar,indexed,words,spread\n";
        std::size_t ordinal = 0;
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
            require(count <= cng_entropy_trial::maximum_input, "symbol count");
            std::vector<std::uint8_t> raw(count);
            cng_huffman_kernel::decode(packet, raw);
            std::array<std::uint32_t, 256> populations{};
            for (std::uint8_t value : raw) { ++populations[value]; }
            unsigned active = 0;
            for (std::uint32_t population : populations) { active += population != 0; }
            require(active >= 2, "nondegenerate retained corpus");
            std::array<std::uint8_t, 256> lengths{};
            cng_model::make_lengths(populations, lengths);
            // Large preparation ownership is local to this packet and ends only
            // after all borrowed child runs and output comparisons are finished.
            CorpusCase sample{};
            prepare(raw, lengths, sample);
            std::array<std::array<double, 7>, 4> times{};
            for (std::size_t trial = 0; trial < 8; ++trial) {
                for (std::size_t order = 0; order < 4; ++order) {
                    std::size_t method = (ordinal + trial + order) % 4;
                    double elapsed = execute(sample, kernels[method]);
                    if (trial != 0) { times[method][trial - 1] = elapsed; }
                }
            }
            const cngcompress_bulk::Traffic& traffic = sample.accesses;
            rows << ordinal << ',' << count << ',' << sample.run_count << ',' << sample.constant_children
                 << ',' << traffic.output << ',' << traffic.bitmap << ',' << traffic.word_input
                 << ',' << traffic.selector_table << ',' << traffic.spread_table << ',' << traffic.mixed_groups << ',' << traffic.pure_groups;
            for (std::size_t method = 0; method < 4; ++method) { rows << ',' << median(times[method]); }
            rows << '\n';
            ++ordinal;
            if (ordinal % 500 == 0) { std::cout << "Compared " << ordinal << " packets\n" << std::flush; }
        }
        rows.close();
        require(static_cast<bool>(rows), "CSV complete");
        std::cout << "PASS: " << fixtures << " boundary/mask fixtures; " << ordinal << " fixed-code corpus preparations\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
