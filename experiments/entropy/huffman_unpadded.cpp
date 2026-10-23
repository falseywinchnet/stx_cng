#include "huffman_kernel.hpp"
#include "rans.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace cng_huffman_kernel {
using cng_entropy_trial::maximum_input;
namespace {
#if defined(CNG_SHORT_STREAM)
constexpr std::uint8_t packet_mode = 36;
#else
constexpr std::uint8_t packet_mode = 35;
#endif
std::size_t stream_count(std::size_t count) {
#if defined(CNG_SHORT_STREAM)
    if (count <= 1024) { return 1; }
#else
    static_cast<void>(count);
#endif
    return 4;
}
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
void append_nibble(std::span<std::uint8_t> output, std::size_t& count, std::uint8_t value) {
    require(count < 2 * output.size() && value < 16, "Huffman table capacity");
    if ((count & 1U) == 0) { output[count / 2] = value; }
    else { output[count / 2] |= static_cast<std::uint8_t>(value << 4); }
    ++count;
}
std::size_t write_lengths(const std::array<std::uint8_t, 256>& lengths, std::span<std::uint8_t> output) {
    std::size_t nibbles = 0;
    std::size_t position = 0;
    std::uint8_t previous = 0;
    while (position < lengths.size()) {
        std::uint8_t length = lengths[position];
        std::size_t end = position + 1;
        while (end < lengths.size() && lengths[end] == length) { ++end; }
        std::size_t count = end - position;
        if (length != previous) {
            append_nibble(output, nibbles, length);
            previous = length;
            --count;
        }
        // Every repeat token consumes no more nibbles than the literal lengths
        // it replaces. The entire model therefore occupies at most 128 bytes.
        if (count >= 19) {
            append_nibble(output, nibbles, 15);
            append_nibble(output, nibbles, static_cast<std::uint8_t>((count - 19) & 15U));
            append_nibble(output, nibbles, static_cast<std::uint8_t>((count - 19) >> 4));
        } else if (count >= 3) {
            append_nibble(output, nibbles, 14);
            append_nibble(output, nibbles, static_cast<std::uint8_t>(count - 3));
        } else {
            for (std::size_t index = 0; index < count; ++index) { append_nibble(output, nibbles, length); }
        }
        position = end;
    }
    std::size_t bytes = (nibbles + 1) / 2;
    return bytes;
}
std::uint8_t take_nibble(std::span<const std::uint8_t> input, std::size_t& position) {
    require(position / 2 < input.size(), "Huffman table extent");
    unsigned shift = static_cast<unsigned>(position & 1U) * 4;
    std::uint8_t value = static_cast<std::uint8_t>((input[position / 2] >> shift) & 15U);
    ++position;
    return value;
}
std::size_t read_lengths(std::span<const std::uint8_t> input, std::array<std::uint8_t, 256>& lengths) {
    std::size_t nibbles = 0;
    std::size_t written = 0;
    std::uint8_t previous = 0;
    while (written < lengths.size()) {
        std::uint8_t token = take_nibble(input, nibbles);
        if (token <= 12) { previous = token; lengths[written] = token; ++written; continue; }
        require(token != 13, "Huffman reserved table token");
        std::size_t count = take_nibble(input, nibbles);
        if (token == 15) {
            count |= static_cast<std::size_t>(take_nibble(input, nibbles)) << 4;
            count += 19;
        } else { count += 3; }
        require(count <= lengths.size() - written, "Huffman table run");
        std::fill_n(lengths.data() + written, count, previous);
        written += count;
    }
    require(nibbles <= 256, "Huffman table bound");
    if ((nibbles & 1U) != 0) { require((input[nibbles / 2] >> 4) == 0, "Huffman table padding"); }
    std::size_t bytes = (nibbles + 1) / 2;
    return bytes;
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position = 0;
    std::uint64_t pending = 0;
    unsigned live = 0;
    std::uint32_t peek() {
        if (live < 12) {
            std::uint32_t word = 0;
            if (position <= bytes.size() && bytes.size() - position >= 4) {
                word = get32(bytes, position);
            } else {
                // Virtual zero extension permits the same lookup at the tail.
                // Position counts supplied bits, including these local zeros;
                // finish checks that only real encoded bits were consumed.
                std::size_t available = position < bytes.size() ? bytes.size() - position : 0;
                for (std::size_t byte = 0; byte < available; ++byte) {
                    word |= static_cast<std::uint32_t>(bytes[position + byte]) << (8 * byte);
                }
            }
            pending |= static_cast<std::uint64_t>(word) << live;
            position += 4;
            live += 32;
        }
        std::uint32_t value = static_cast<std::uint32_t>(pending) & 4095U;
        return value;
    }
    void advance(unsigned bits) {
        pending >>= bits;
        live -= bits;
    }
    void finish() const {
        std::size_t consumed = position * 8 - live;
        std::size_t size = (consumed + 7) / 8;
        require(size == bytes.size(), "Huffman exact byte extent");
        if (consumed % 8 != 0) { require((bytes[size - 1] >> (consumed % 8)) == 0, "Huffman bit padding"); }
    }
};
void pair(Reader& reader, const std::array<std::uint32_t, 4096>& table,
          std::uint8_t* __restrict output, std::size_t& position) {
    std::uint32_t entry = table[reader.peek()];
    reader.advance((entry >> 16) & 255U);
    // The restricted output borrows at least two writable bytes, disjoint from
    // the reader, table and position. All callers establish this before entry.
    // The caller establishes at least two writable bytes. A single-symbol
    // entry writes a temporary next byte which the next operation overwrites.
    output[position] = static_cast<std::uint8_t>(entry);
    output[position + 1] = static_cast<std::uint8_t>(entry >> 8);
    position += entry >> 24;
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() <= maximum_input && output.size() >= 2 * input.size() + 512, "Huffman capacity");
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
    std::size_t table_bytes = write_lengths(lengths, output.subspan(5, 128));
    std::size_t lanes = stream_count(input.size());
    std::size_t header = 5 + table_bytes + 4 * (lanes - 1);
    // The final stream extent is implied by the packet. No zero pads are stored.
    if (header + bits / 8 >= input.size() + 5) {
        std::memcpy(output.data() + 5, input.data(), input.size());
        std::size_t result = input.size() + 5;
        return result;
    }
    std::size_t position = header;
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
        if (lane + 1 != lanes) { put32(output, 5 + table_bytes + 4 * lane, static_cast<std::uint32_t>(position - begin)); }
    }
    if (position >= input.size() + 5) {
        std::memcpy(output.data() + 5, input.data(), input.size());
        std::size_t result = input.size() + 5;
        return result;
    }
    output[0] = packet_mode;
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
    require(input[0] == packet_mode && input.size() >= 7, "Huffman coding");
    std::size_t lanes = stream_count(output.size());
    std::array<std::uint8_t, 256> lengths{};
    std::array<std::uint16_t, 256> codes{};
    std::size_t table_bytes = read_lengths(input.subspan(5), lengths);
    require(input.size() >= 5 + table_bytes + 4 * (lanes - 1), "Huffman coded extent");
    canonical(lengths, codes);
    std::array<std::uint16_t, 4096> single{};
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        unsigned length = lengths[symbol];
        if (length == 0) { continue; }
        for (std::size_t index = codes[symbol]; index < 4096; index += 1U << length) {
            single[index] = static_cast<std::uint16_t>((length << 8) | symbol);
        }
    }
    if (lanes == 1) {
        Reader reader{input.subspan(5 + table_bytes)};
        for (std::size_t index = 0; index < output.size(); ++index) {
            std::uint32_t entry = single[reader.peek()];
            output[index] = static_cast<std::uint8_t>(entry);
            reader.advance(entry >> 8);
        }
        reader.finish();
        return;
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
    std::size_t position = 5 + table_bytes + 12;
    std::size_t quarter = output.size() / 4;
    for (std::size_t lane = 0; lane < 4; ++lane) {
        std::size_t size = lane == 3 ? input.size() - position : get32(input, 5 + table_bytes + 4 * lane);
        require(size <= input.size() - position, "Huffman stream extent");
        readers[lane].bytes = input.subspan(position, size);
        position += size;
        std::size_t count = lane == 3 ? output.size() - 3 * quarter : quarter;
        outputs[lane] = output.subspan(lane * quarter, count);
    }
    require(position == input.size(), "Huffman packet extent");
    // Named readers keep the four hot recurrence states separate from the
    // indexed tail traversal; publish them back once after the interior.
    Reader reader0 = readers[0];
    Reader reader1 = readers[1];
    Reader reader2 = readers[2];
    Reader reader3 = readers[3];
    while (outputs[0].size() - positions[0] >= 2 && outputs[1].size() - positions[1] >= 2
           && outputs[2].size() - positions[2] >= 2 && outputs[3].size() - positions[3] >= 2) {
        pair(reader0, paired, outputs[0].data(), positions[0]);
        pair(reader1, paired, outputs[1].data(), positions[1]);
        pair(reader2, paired, outputs[2].data(), positions[2]);
        pair(reader3, paired, outputs[3].data(), positions[3]);
    }
    readers[0] = reader0;
    readers[1] = reader1;
    readers[2] = reader2;
    readers[3] = reader3;
    for (std::size_t lane = 0; lane < 4; ++lane) {
        while (outputs[lane].size() - positions[lane] >= 2) { pair(readers[lane], paired, outputs[lane].data(), positions[lane]); }
        if (positions[lane] < outputs[lane].size()) {
            std::uint32_t entry = single[readers[lane].peek()];
            outputs[lane][positions[lane]] = static_cast<std::uint8_t>(entry);
            readers[lane].advance(entry >> 8);
        }
        readers[lane].finish();
    }
}
}
