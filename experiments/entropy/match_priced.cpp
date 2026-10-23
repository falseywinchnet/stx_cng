#include "match_split.hpp"
#include "huffman_kernel.hpp"
#include "rans.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
namespace cng_match_trial {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t get32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t result = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { result |= static_cast<std::uint32_t>(bytes[at + byte]) << (8 * byte); }
    return result;
}
void put32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte)); }
}
std::size_t hash(std::uint32_t word) {
    std::size_t result = (word * 2654435761U) >> 18;
    return result;
}
std::size_t integer_size(std::size_t value) {
    std::size_t size = 1;
    while (value >= 128) { value >>= 7; ++size; }
    return size;
}
void integer(std::span<std::uint8_t> output, std::size_t& position, std::size_t value) {
    do {
        require(position < output.size(), "match integer capacity");
        std::uint8_t byte = static_cast<std::uint8_t>(value & 127U);
        value >>= 7;
        if (value != 0) { byte |= 128U; }
        output[position] = byte;
        ++position;
    } while (value != 0);
}
std::size_t integer(std::span<const std::uint8_t> input, std::size_t& position) {
    std::size_t result = 0;
    for (unsigned shift = 0; shift <= 21; shift += 7) {
        require(position < input.size(), "match integer extent");
        std::uint8_t byte = input[position];
        ++position;
        result |= static_cast<std::size_t>(byte & 127U) << shift;
        if (byte < 128) {
            require(shift == 0 || byte != 0, "match integer canonical form");
            return result;
        }
    }
    throw std::runtime_error("match integer width");
}
std::size_t extend(std::span<const std::uint8_t> input, std::size_t previous, std::size_t position, std::size_t end) {
    std::size_t length = 4;
    std::size_t remaining = end - position;
    while (remaining - length >= 8) {
        std::uint64_t first = 0;
        std::uint64_t second = 0;
        std::memcpy(&first, input.data() + previous + length, 8);
        std::memcpy(&second, input.data() + position + length, 8);
        std::uint64_t difference = first ^ second;
        if (difference != 0) {
            unsigned equal = 0;
            if constexpr (std::endian::native == std::endian::little) {
                equal = static_cast<unsigned>(std::countr_zero(difference)) / 8;
            } else { equal = static_cast<unsigned>(std::countl_zero(difference)) / 8; }
            length += equal;
            return length;
        }
        length += 8;
    }
    while (length < remaining && input[previous + length] == input[position + length]) { ++length; }
    return length;
}
struct Distances {
    std::size_t recent = 0;
    std::size_t older = 0;
};
std::size_t distance_code(std::size_t distance, Distances& distances) {
    if (distance == distances.recent) { return 0; }
    std::size_t code = distance == distances.older ? 1 : distance + 1;
    distances.older = distances.recent;
    distances.recent = distance;
    return code;
}
std::size_t restore_distance(std::size_t code, Distances& distances) {
    std::size_t distance = code == 0 ? distances.recent : (code == 1 ? distances.older : code - 1);
    require(distance != 0, "initialized repeated distance");
    if (code != 0) {
        distances.older = distances.recent;
        distances.recent = distance;
    }
    return distance;
}
struct ParseResult {
    std::size_t size;
    std::size_t literals;
    bool references;
};
ParseResult parse(std::span<const std::uint8_t> input, std::size_t begin, std::size_t count,
                  std::size_t mask, Distances& distances, EncodeWorkspace& workspace) {
    std::size_t end = begin + count;
    std::span<std::uint8_t> output(workspace.parsed);
    bool references = false;
    std::size_t literal_count = 0;
    std::array<std::uint32_t, 256> populations{};
    for (std::size_t index = begin; index < end; ++index) { ++populations[input[index]]; }
    std::array<std::uint8_t, 256> costs{};
    for (std::size_t symbol = 0; symbol < 256; ++symbol) {
        if (populations[symbol] == 0) { continue; }
        if (populations[symbol] == count) { return ParseResult{0, 0, false}; }
        std::size_t ratio = count / populations[symbol];
        costs[symbol] = static_cast<std::uint8_t>(std::max<std::size_t>(1, std::bit_width(ratio) - 1));
    }
    std::size_t written = 0;
    std::size_t anchor = begin;
    std::size_t position = begin;
    std::size_t misses = 0;
    while (end - position >= 4) {
        std::uint32_t word = get32(input, position);
        std::size_t slot = hash(word) & mask;
        std::uint32_t entry = workspace.index[slot];
        workspace.index[slot] = static_cast<std::uint32_t>(position + 1);
        std::size_t previous = 0;
        bool found = false;
        if (misses < 2 && distances.recent != 0 && distances.recent <= position) {
            previous = position - distances.recent;
            found = get32(input, previous) == word;
        }
        if (!found && misses < 2 && distances.older != 0 && distances.older <= position) {
            previous = position - distances.older;
            found = get32(input, previous) == word;
        }
        if (!found && entry != 0) {
            previous = entry - 1;
            found = get32(input, previous) == word;
        }
        if (!found) {
            ++misses;
            position += std::min<std::size_t>(1 + (misses >> 6), end - position);
            continue;
        }
        std::size_t distance = position - previous;
        std::size_t length = extend(input, previous, position, end);
        std::size_t literals = position - anchor;
        std::size_t code = distance == distances.recent ? 0 : (distance == distances.older ? 1 : distance + 1);
        std::size_t overhead = 1 + integer_size(code);
        if (literals >= 15) { overhead += integer_size(literals - 15); }
        if (length >= 19) { overhead += integer_size(length - 19); }
        // Literal prices estimate floor(-log2(p)), with a one-bit floor for
        // nonconstant blocks. Controls are stored verbatim, so their byte cost
        // is exact. The final literal model changes after parsing: this is a
        // deterministic estimate, not a proof of an optimal Huffman parse.
        std::size_t control_bits = 8 * overhead;
        std::size_t saved_bits = length;
        if (length <= control_bits) {
            saved_bits = 0;
            for (std::size_t index = 0; index < length; ++index) {
                saved_bits += costs[input[position + index]];
            }
        }
        if (saved_bits <= control_bits) {
            ++misses;
            position += std::min<std::size_t>(1 + (misses >> 6), end - position);
            continue;
        }
        references = true;
        misses = 0;
        require(written < output.size(), "match token capacity");
        std::size_t token = (std::min<std::size_t>(15, literals) << 4) | std::min<std::size_t>(15, length - 4);
        output[written] = static_cast<std::uint8_t>(token);
        ++written;
        if (literals >= 15) { integer(output, written, literals - 15); }
        require(literals <= workspace.literals.size() - literal_count, "match literal capacity");
        if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
        literal_count += literals;
        code = distance_code(distance, distances);
        integer(output, written, code);
        if (length >= 19) { integer(output, written, length - 19); }
        position += length;
        anchor = position;
        // Retain the last complete short word before the next query. Interior
        // positions need no index work once this exact match consumes them.
        std::size_t last = position - 2;
        if (input.size() - last >= 4) { workspace.index[hash(get32(input, last)) & mask] = static_cast<std::uint32_t>(last + 1); }
    }
    if (!references) { return ParseResult{0, 0, false}; }
    if (anchor != end) {
        std::size_t literals = end - anchor;
        require(written < output.size(), "match terminal token capacity");
        output[written] = static_cast<std::uint8_t>(std::min<std::size_t>(15, literals) << 4);
        ++written;
        if (literals >= 15) { integer(output, written, literals - 15); }
        require(literals <= workspace.literals.size() - literal_count, "match tail capacity");
        if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
        literal_count += literals;
    }
    require(written <= count + 4, "match parse bound");
    return ParseResult{written, literal_count, true};
}
void copy_match(std::span<std::uint8_t> output, std::size_t position, std::size_t distance, std::size_t length) {
    if (distance >= length) {
        std::memcpy(output.data() + position, output.data() + position - distance, length);
        return;
    }
    if (distance == 1) {
        std::fill_n(output.data() + position, length, output[position - 1]);
        return;
    }
    // Seed one period, then double already reconstructed material. Every copy
    // is disjoint even when the logical reference overlaps its destination.
    std::memcpy(output.data() + position, output.data() + position - distance, distance);
    std::size_t copied = distance;
    while (copied < length) {
        std::size_t count = std::min(copied, length - copied);
        std::memcpy(output.data() + position + copied, output.data() + position, count);
        copied += count;
    }
}
void reconstruct(std::span<const std::uint8_t> input, std::span<const std::uint8_t> source, std::span<std::uint8_t> output,
                 std::size_t begin, std::size_t count, Distances& distances) {
    std::size_t end = begin + count;
    std::size_t literal_position = 0;
    std::size_t read = 0;
    std::size_t written = begin;
    while (written < end) {
        require(read < input.size(), "match token extent");
        std::uint8_t token = input[read];
        ++read;
        std::size_t literals = token >> 4;
        if (literals == 15) { literals += integer(input, read); }
        require(literals <= source.size() - literal_position && literals <= end - written, "match literal extent");
        if (literals != 0) { std::memcpy(output.data() + written, source.data() + literal_position, literals); }
        literal_position += literals;
        written += literals;
        if (written == end) { break; }
        std::size_t code = integer(input, read);
        std::size_t distance = restore_distance(code, distances);
        std::size_t length = (token & 15U) + 4;
        if (length == 19) { length += integer(input, read); }
        require(distance != 0 && distance <= written && length <= end - written, "match reference extent");
        copy_match(output, written, distance, length);
        written += length;
    }
    require(read == input.size() && literal_position == source.size(), "match exact consumption");
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace) {
    require(input.size() <= cng_entropy_trial::maximum_input && output.size() >= 2 * input.size() + 1024, "match packet capacity");
    output[0] = 19;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    Distances distances{};
    std::size_t position = 5;
    std::size_t slots = std::bit_floor(std::min<std::size_t>(16384, std::max<std::size_t>(16, input.size() / 4)));
    std::fill_n(workspace.index.data(), slots, 0);
    std::size_t mask = slots - 1;
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t count = std::min(block_size, input.size() - offset);
        std::span<const std::uint8_t> block = input.subspan(offset, count);
        Distances saved_distances = distances;
        ParseResult parsed = parse(input, offset, count, mask, distances, workspace);
        std::size_t coded = 0;
        std::uint32_t flags = 0;
        if (!parsed.references) {
            coded = cng_huffman_kernel::encode(block, output.subspan(position + 4));
            flags = 0x40000000U;
        } else {
            std::span<const std::uint8_t> literals = std::span<const std::uint8_t>(workspace.literals).first(parsed.literals);
            std::size_t literal_bytes = cng_huffman_kernel::encode(literals, output.subspan(position + 12 + parsed.size));
            put32(output, position + 4, static_cast<std::uint32_t>(parsed.size));
            put32(output, position + 8, static_cast<std::uint32_t>(parsed.literals));
            std::memcpy(output.data() + position + 12, workspace.parsed.data(), parsed.size);
            coded = 8 + parsed.size + literal_bytes;
        }
        if (coded >= count) {
            distances = saved_distances;
            put32(output, position, 0x80000000U | static_cast<std::uint32_t>(count));
            std::memcpy(output.data() + position + 4, block.data(), count);
            position += 4 + count;
        } else {
            put32(output, position, flags | static_cast<std::uint32_t>(coded));
            position += 4 + coded;
        }
        offset += count;
    }
    if (position >= input.size() + 5) {
        output[0] = 0;
        if (!input.empty()) { std::memcpy(output.data() + 5, input.data(), input.size()); }
        position = input.size() + 5;
    }
    return position;
}
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, DecodeWorkspace& workspace) {
    require(input.size() >= 5 && output.size() <= cng_entropy_trial::maximum_input && get32(input, 1) == output.size(), "match packet output extent");
    if (input[0] == 0) {
        require(input.size() == output.size() + 5, "match stored extent");
        if (!output.empty()) { std::memcpy(output.data(), input.data() + 5, output.size()); }
        return;
    }
    require(input[0] == 19 && !output.empty(), "match packet coding");
    Distances distances{};
    std::size_t position = 5;
    for (std::size_t offset = 0; offset < output.size();) {
        require(input.size() - position >= 4, "match block header extent");
        std::uint32_t header = get32(input, position);
        position += 4;
        std::size_t size = header & 0x3fffffffU;
        std::size_t count = std::min(block_size, output.size() - offset);
        require(size <= input.size() - position, "match block extent");
        if ((header & 0x80000000U) != 0) {
            require(size == count, "match stored block extent");
            std::memcpy(output.data() + offset, input.data() + position, count);
        } else if ((header & 0x40000000U) != 0) {
            cng_huffman_kernel::decode(input.subspan(position, size), output.subspan(offset, count));
        } else {
            require(size >= 13, "match coded block header");
            std::size_t parsed = get32(input, position);
            std::size_t literals = get32(input, position + 4);
            require(parsed <= count + 4 && parsed <= size - 8 && literals <= count, "match parsed bound");
            std::span<const std::uint8_t> controls = input.subspan(position + 8, parsed);
            std::span<std::uint8_t> source = std::span<std::uint8_t>(workspace.literals).first(literals);
            cng_huffman_kernel::decode(input.subspan(position + 8 + parsed, size - 8 - parsed), source);
            reconstruct(controls, source, output, offset, count, distances);
        }
        position += size;
        offset += count;
    }
    require(position == input.size(), "match exact packet extent");
}
}
