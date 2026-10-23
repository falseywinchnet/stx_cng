#include "rans.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace cng_entropy_trial {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void put32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte)); }
}
std::uint32_t get32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { value |= static_cast<std::uint32_t>(bytes[at + byte]) << (8 * byte); }
    return value;
}
struct Leaf {
    std::uint32_t weight = 0;
    std::uint16_t symbol = 0;
};
struct Before {
    bool operator()(const Leaf& first, const Leaf& second) const {
        if (first.weight != second.weight) { return first.weight < second.weight; }
        return first.symbol < second.symbol;
    }
};
std::size_t take_minimum(const std::array<std::uint32_t, 511>& weights, std::size_t count,
                         std::size_t built, std::size_t& leaf, std::size_t& branch) {
    std::size_t result = 0;
    if (leaf < count && (branch == built || weights[leaf] <= weights[branch])) { result = leaf; ++leaf; }
    else { result = branch; ++branch; }
    return result;
}
void make_lengths(const std::array<std::uint32_t, 256>& populations, std::array<std::uint8_t, 256>& lengths) {
    std::array<Leaf, 256> leaves{};
    std::array<std::uint32_t, 511> weights{};
    std::array<std::size_t, 511> parents{};
    // Rebuild with a common positive frequency floor only if the unconstrained
    // tree exceeds the 12-bit lookup. This is bounded smoothing, not an optimal
    // length-limited Huffman construction; its actual size is measured.
    for (unsigned shift = 0; shift <= 22; ++shift) {
        std::size_t count = 0;
        for (std::size_t symbol = 0; symbol < 256; ++symbol) {
            if (populations[symbol] == 0) { continue; }
            std::uint32_t weight = shift == 0 ? populations[symbol] : (populations[symbol] >> shift) + 1;
            leaves[count] = Leaf{weight, static_cast<std::uint16_t>(symbol)};
            ++count;
        }
        require(count >= 2, "Huffman nondegenerate population");
        std::sort(leaves.begin(), leaves.begin() + count, Before{});
        for (std::size_t index = 0; index < count; ++index) { weights[index] = leaves[index].weight; }
        std::size_t leaf = 0;
        std::size_t branch = count;
        for (std::size_t built = count; built < 2 * count - 1; ++built) {
            std::size_t first = take_minimum(weights, count, built, leaf, branch);
            std::size_t second = take_minimum(weights, count, built, leaf, branch);
            weights[built] = weights[first] + weights[second];
            parents[first] = built;
            parents[second] = built;
        }
        unsigned maximum = 0;
        std::size_t root = 2 * count - 2;
        for (std::size_t index = 0; index < count; ++index) {
            unsigned depth = 0;
            for (std::size_t node = index; node != root; node = parents[node]) { ++depth; }
            maximum = std::max(maximum, depth);
            lengths[leaves[index].symbol] = static_cast<std::uint8_t>(depth);
        }
        if (maximum <= 12) { return; }
    }
    throw std::runtime_error("Huffman depth convergence");
}
void canonical(const std::array<std::uint8_t, 256>& lengths, std::array<std::uint16_t, 256>& codes) {
    std::array<std::uint32_t, 13> counts{};
    std::array<std::uint32_t, 13> next{};
    for (std::uint8_t length : lengths) {
        require(length <= 12, "Huffman code length");
        if (length != 0) { ++counts[length]; }
    }
    std::uint32_t code = 0;
    for (unsigned length = 1; length <= 12; ++length) {
        code = (code + counts[length - 1]) << 1;
        require(code + counts[length] <= (1U << length), "Huffman prefix population");
        next[length] = code;
    }
    require(code + counts[12] == 4096, "Huffman complete tree");
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        unsigned length = lengths[symbol];
        if (length == 0) { continue; }
        std::uint32_t forward = next[length];
        ++next[length];
        std::uint16_t reversed = 0;
        for (unsigned bit = 0; bit < length; ++bit) {
            reversed = static_cast<std::uint16_t>((reversed << 1) | (forward & 1U));
            forward >>= 1;
        }
        codes[symbol] = reversed;
    }
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position = 0;
    std::uint64_t pending = 0;
    unsigned live = 0;
    std::size_t consumed = 0;
    std::uint32_t peek() {
        if (live < 12) {
            require(bytes.size() - position >= 4, "Huffman input refill");
            pending |= static_cast<std::uint64_t>(get32(bytes, position)) << live;
            position += 4;
            live += 32;
        }
        std::uint32_t value = static_cast<std::uint32_t>(pending) & 4095U;
        return value;
    }
    void advance(unsigned bits) {
        pending >>= bits;
        live -= bits;
        consumed += bits;
    }
    void finish() const {
        std::size_t size = (consumed + 7) / 8;
        require(size + 8 == bytes.size(), "Huffman exact byte extent");
        if (consumed % 8 != 0) { require((bytes[size - 1] >> (consumed % 8)) == 0, "Huffman bit padding"); }
        for (std::size_t index = size; index < bytes.size(); ++index) { require(bytes[index] == 0, "Huffman byte padding"); }
    }
};
void pair(Reader& reader, const std::array<std::uint32_t, 4096>& table,
          std::span<std::uint8_t> output, std::size_t& position) {
    std::uint32_t entry = table[reader.peek()];
    reader.advance((entry >> 16) & 255U);
    // The caller establishes at least two writable bytes. A single-symbol
    // entry writes a temporary next byte which the next operation overwrites.
    output[position] = static_cast<std::uint8_t>(entry);
    output[position + 1] = static_cast<std::uint8_t>(entry >> 8);
    position += entry >> 24;
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() <= maximum_input && output.size() >= 2 * input.size() + 1024, "Huffman capacity");
    output[0] = 0;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    if (input.empty()) { return 5; }
    std::array<std::uint32_t, 256> populations{};
    for (std::uint8_t value : input) { ++populations[value]; }
    std::size_t active = 0;
    std::uint8_t only = 0;
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        if (populations[symbol] != 0) { ++active; only = static_cast<std::uint8_t>(symbol); }
    }
    if (active == 1) { output[0] = 4; output[5] = only; return 6; }
    std::array<std::uint8_t, 256> lengths{};
    std::array<std::uint16_t, 256> codes{};
    make_lengths(populations, lengths);
    canonical(lengths, codes);
    std::size_t bits = 0;
    for (std::size_t symbol = 0; symbol < 256; ++symbol) { bits += populations[symbol] * lengths[symbol]; }
    constexpr std::size_t header = 5 + 128 + 16;
    // The fixed header, four stream pads and rounding already exceed raw data:
    // return after counting, before performing the per-symbol bit packing.
    if (header + 32 + bits / 8 >= input.size() + 5) {
        std::memcpy(output.data() + 5, input.data(), input.size());
        std::size_t result = input.size() + 5;
        return result;
    }
    for (std::size_t pair_index = 0; pair_index < 128; ++pair_index) {
        output[5 + pair_index] = static_cast<std::uint8_t>(lengths[2 * pair_index] | (lengths[2 * pair_index + 1] << 4));
    }
    std::size_t position = header;
    std::size_t quarter = input.size() / 4;
    for (std::size_t lane = 0; lane < 4; ++lane) {
        std::size_t begin = position;
        std::size_t first = lane * quarter;
        std::size_t last = lane == 3 ? input.size() : first + quarter;
        std::uint64_t pending = 0;
        unsigned live = 0;
        for (std::size_t index = first; index < last; ++index) {
            std::uint8_t symbol = input[index];
            pending |= static_cast<std::uint64_t>(codes[symbol]) << live;
            live += lengths[symbol];
            if (live >= 32) {
                put32(output, position, static_cast<std::uint32_t>(pending));
                position += 4;
                pending >>= 32;
                live -= 32;
            }
        }
        while (live != 0) {
            output[position] = static_cast<std::uint8_t>(pending);
            ++position;
            pending >>= 8;
            live = live > 8 ? live - 8 : 0;
        }
        std::fill_n(output.data() + position, 8, 0);
        position += 8;
        put32(output, 133 + 4 * lane, static_cast<std::uint32_t>(position - begin));
    }
    if (position >= input.size() + 5) {
        std::memcpy(output.data() + 5, input.data(), input.size());
        std::size_t result = input.size() + 5;
        return result;
    }
    output[0] = 7;
    return position;
}
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() >= 5 && output.size() <= maximum_input && get32(input, 1) == output.size(), "Huffman output extent");
    if (input[0] == 0) {
        require(input.size() == output.size() + 5, "Huffman stored extent");
        if (!output.empty()) { std::memcpy(output.data(), input.data() + 5, output.size()); }
        return;
    }
    if (input[0] == 4) {
        require(input.size() == 6 && !output.empty(), "Huffman constant extent");
        std::fill(output.begin(), output.end(), input[5]);
        return;
    }
    require(input[0] == 7 && input.size() >= 181, "Huffman coding");
    std::array<std::uint8_t, 256> lengths{};
    std::array<std::uint16_t, 256> codes{};
    for (std::size_t index = 0; index < 128; ++index) {
        lengths[2 * index] = input[5 + index] & 15U;
        lengths[2 * index + 1] = input[5 + index] >> 4;
    }
    canonical(lengths, codes);
    std::array<std::uint16_t, 4096> single{};
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        unsigned length = lengths[symbol];
        if (length == 0) { continue; }
        for (std::size_t index = codes[symbol]; index < 4096; index += 1U << length) {
            single[index] = static_cast<std::uint16_t>((length << 8) | symbol);
        }
    }
    std::array<std::uint32_t, 4096> paired{};
    for (std::size_t index = 0; index < 4096; ++index) {
        std::uint32_t first = single[index];
        unsigned bits = first >> 8;
        std::uint32_t second = single[index >> bits];
        unsigned combined = bits + (second >> 8);
        if (combined <= 12) { paired[index] = (first & 255U) | ((second & 255U) << 8) | (combined << 16) | (2U << 24); }
        else { paired[index] = (first & 255U) | (bits << 16) | (1U << 24); }
    }
    std::array<Reader, 4> readers{};
    std::array<std::span<std::uint8_t>, 4> outputs{};
    std::array<std::size_t, 4> positions{};
    std::size_t position = 149;
    std::size_t quarter = output.size() / 4;
    for (std::size_t lane = 0; lane < 4; ++lane) {
        std::size_t size = get32(input, 133 + 4 * lane);
        require(size >= 8 && size <= input.size() - position, "Huffman stream extent");
        readers[lane].bytes = input.subspan(position, size);
        position += size;
        std::size_t count = lane == 3 ? output.size() - 3 * quarter : quarter;
        outputs[lane] = output.subspan(lane * quarter, count);
    }
    require(position == input.size(), "Huffman packet extent");
    while (outputs[0].size() - positions[0] >= 2 && outputs[1].size() - positions[1] >= 2
           && outputs[2].size() - positions[2] >= 2 && outputs[3].size() - positions[3] >= 2) {
        pair(readers[0], paired, outputs[0], positions[0]);
        pair(readers[1], paired, outputs[1], positions[1]);
        pair(readers[2], paired, outputs[2], positions[2]);
        pair(readers[3], paired, outputs[3], positions[3]);
    }
    for (std::size_t lane = 0; lane < 4; ++lane) {
        while (outputs[lane].size() - positions[lane] >= 2) { pair(readers[lane], paired, outputs[lane], positions[lane]); }
        if (positions[lane] < outputs[lane].size()) {
            std::uint32_t entry = single[readers[lane].peek()];
            outputs[lane][positions[lane]] = static_cast<std::uint8_t>(entry);
            readers[lane].advance(entry >> 8);
        }
        readers[lane].finish();
    }
}
}
