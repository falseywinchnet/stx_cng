#include "grammar.hpp"
#include "huffman_kernel.hpp"
#include "rans.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>
namespace CNG_NAMESPACE {
using cng_match_trial::Bucket;
using cng_match_trial::Entry;
using cng_match_trial::block_size;
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
#if CNG_PREFIX
void integer(std::span<std::uint8_t> output, std::size_t& position, std::size_t value) {
    unsigned width = value < 128 ? 1 : value < 16384 ? 2 : value < 2097152 ? 3 : 4;
    require(value < (std::size_t{1} << 28) && output.size() - position >= width, "prefix integer capacity");
    unsigned first_bits = 8 - width;
    unsigned prefix = width == 1 ? 0 : 256 - (1U << (9 - width));
    output[position] = static_cast<std::uint8_t>(prefix | (value & ((1U << first_bits) - 1)));
    value >>= first_bits;
    for (unsigned byte = 1; byte < width; ++byte) {
        output[position + byte] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
    position += width;
}
std::size_t integer(std::span<const std::uint8_t> input, std::size_t& position) {
    require(position < input.size(), "prefix integer extent");
    unsigned first = input[position];
    unsigned width = first < 128 ? 1 : first < 192 ? 2 : first < 224 ? 3 : 4;
    require(first < 240 && input.size() - position >= width, "prefix integer width");
    unsigned first_bits = 8 - width;
    std::size_t result = first & ((1U << first_bits) - 1);
    for (unsigned byte = 1; byte < width; ++byte) {
        result |= static_cast<std::size_t>(input[position + byte]) << (first_bits + 8 * (byte - 1));
    }
    require(width == 1 || result >= (std::size_t{1} << (7 * (width - 1))), "prefix integer canonical form");
    position += width;
    return result;
}
#else
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
#endif
std::size_t extend(std::span<const std::uint8_t> input, std::size_t previous, std::size_t position, std::size_t end) {
    std::size_t length = 8;
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
struct ParseResult {
    std::size_t size;
    bool references;
};
ParseResult parse(std::span<const std::uint8_t> input, std::size_t begin, std::size_t count,
                  std::size_t mask, EncodeWorkspace& workspace) {
    std::size_t end = begin + count;
    std::span<std::uint8_t> output(workspace.parsed);
    bool references = false;
    std::size_t written = CNG_SPLIT ? 4 : 0;
#if CNG_SPLIT
    std::size_t literal_count = 0;
#endif
    std::size_t anchor = begin;
    std::size_t position = begin;
    std::size_t misses = 0;
    while (end - position >= 8) {
        std::uint32_t word = get32(input, position);
        std::size_t slot = hash(word) & mask;
        Bucket previous_bucket = workspace.index[slot];
        workspace.index[slot].older = previous_bucket.recent;
        workspace.index[slot].recent = Entry{word, static_cast<std::uint32_t>(position + 1)};
        std::array<Entry, 2> candidates{previous_bucket.recent, previous_bucket.older};
        std::size_t length = 0;
        std::size_t previous = 0;
        std::uint32_t following = get32(input, position + 4);
        for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate) {
            Entry entry = candidates[candidate];
            if (entry.position == 0 || entry.word != word) { continue; }
            if (candidate == 1 && entry.position == previous_bucket.recent.position) { continue; }
            std::size_t source = entry.position - 1;
            if (length != 0) {
                // A candidate that loses inside the current best span cannot
                // tie or improve it. The sampled byte is in bounds because
                // source < position and length <= end-position.
                if (input[source + length - 1] != input[position + length - 1]) { continue; }
                if (length == end - position && source <= previous) { continue; }
            }
            if (get32(input, source + 4) != following) { continue; }
            std::size_t candidate_length = extend(input, source, position, end);
            if (candidate_length > length || (candidate_length == length && source > previous)) {
                previous = source;
                length = candidate_length;
            }
        }
        if (length == 0) {
            ++misses;
            position += std::min<std::size_t>(1 + (misses >> 6), end - position);
            continue;
        }
        references = true;
        misses = 0;
        std::size_t distance = position - previous;
        std::size_t literals = position - anchor;
        integer(output, written, literals);
#if CNG_SPLIT
        require(literals <= workspace.literals.size() - literal_count, "split literal capacity");
        if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
        literal_count += literals;
#else
        require(literals <= output.size() - written, "match literal capacity");
        if (literals != 0) { std::memcpy(output.data() + written, input.data() + anchor, literals); }
        written += literals;
#endif
        integer(output, written, distance);
        integer(output, written, length - 8);
        position += length;
        anchor = position;
        // Retain the last complete short word before the next query. Interior
        // positions need no index work once this exact match consumes them.
        std::size_t last = position - 2;
        if (input.size() - last >= 4) {
            std::uint32_t last_word = get32(input, last);
            std::size_t last_slot = hash(last_word) & mask;
            workspace.index[last_slot].older = workspace.index[last_slot].recent;
            workspace.index[last_slot].recent = Entry{last_word, static_cast<std::uint32_t>(last + 1)};
        }
    }
    if (!references) { return ParseResult{0, false}; }
    if (anchor != end) {
        std::size_t literals = end - anchor;
        integer(output, written, literals);
#if CNG_SPLIT
        require(literals <= workspace.literals.size() - literal_count, "split literal capacity");
        if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
        literal_count += literals;
#else
        require(literals <= output.size() - written, "match tail capacity");
        if (literals != 0) { std::memcpy(output.data() + written, input.data() + anchor, literals); }
        written += literals;
#endif
    }
#if CNG_SPLIT
    put32(output, 0, static_cast<std::uint32_t>(written));
    require(literal_count <= output.size() - written, "split combined capacity");
    if (literal_count != 0) { std::memcpy(output.data() + written, workspace.literals.data(), literal_count); }
    written += literal_count;
#endif
    require(written <= count + 3 + (CNG_SPLIT ? 4 : 0), "match parse bound");
    return ParseResult{written, true};
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
void reconstruct(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
                 std::size_t begin, std::size_t count) {
    std::size_t end = begin + count;
    std::size_t read = 0;
    std::size_t written = begin;
#if CNG_SPLIT
    require(input.size() >= 4, "split header extent");
    std::size_t control_end = get32(input, 0);
    require(control_end >= 4 && control_end <= input.size(), "split control extent");
    std::span<const std::uint8_t> controls = input.first(control_end);
    std::size_t literal_position = control_end;
    read = 4;
#else
    std::span<const std::uint8_t> controls = input;
#endif
    while (written < end) {
        std::size_t literals = integer(controls, read);
#if CNG_SPLIT
        require(literals <= input.size() - literal_position && literals <= end - written, "split literal extent");
        if (literals != 0) { std::memcpy(output.data() + written, input.data() + literal_position, literals); }
        literal_position += literals;
#else
        require(literals <= input.size() - read && literals <= end - written, "match literal extent");
        if (literals != 0) { std::memcpy(output.data() + written, input.data() + read, literals); }
        read += literals;
#endif
        written += literals;
        if (written == end) { break; }
        std::size_t distance = integer(controls, read);
        std::size_t length = integer(controls, read) + 8;
        require(distance != 0 && distance <= written && length <= end - written, "match reference extent");
        copy_match(output, written, distance, length);
        written += length;
    }
    require(read == controls.size(), "match exact control consumption");
#if CNG_SPLIT
    require(literal_position == input.size(), "split exact literal consumption");
#endif
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace) {
    require(input.size() <= cng_entropy_trial::maximum_input && output.size() >= input.size() + 4 * ((input.size() + block_size - 1) / block_size) + 5, "match packet capacity");
    output[0] = CNG_MODE;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t position = 5;
    std::size_t slots = std::bit_floor(std::min<std::size_t>(16384, std::max<std::size_t>(16, input.size() / 4)));
    std::fill_n(workspace.index.data(), slots, Bucket{});
    std::size_t mask = slots - 1;
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t count = std::min(block_size, input.size() - offset);
        std::span<const std::uint8_t> block = input.subspan(offset, count);
        ParseResult parsed = parse(input, offset, count, mask, workspace);
        std::span<const std::uint8_t> symbols = parsed.references
            ? std::span<const std::uint8_t>(workspace.parsed).first(parsed.size) : block;
        std::size_t coded = cng_huffman_kernel::encode(symbols, workspace.coded);
        std::size_t prefix = parsed.references ? 4 : 0;
        if (coded + prefix >= count) {
            put32(output, position, 0x80000000U | static_cast<std::uint32_t>(count));
            std::memcpy(output.data() + position + 4, block.data(), count);
            position += 4 + count;
        } else if (!parsed.references) {
            put32(output, position, 0x40000000U | static_cast<std::uint32_t>(coded));
            std::memcpy(output.data() + position + 4, workspace.coded.data(), coded);
            position += 4 + coded;
        } else {
            put32(output, position, static_cast<std::uint32_t>(coded + 4));
            put32(output, position + 4, static_cast<std::uint32_t>(parsed.size));
            std::memcpy(output.data() + position + 8, workspace.coded.data(), coded);
            position += 8 + coded;
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
    require(input[0] == CNG_MODE && !output.empty(), "match packet coding");
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
            require(size >= 9, "match coded block header");
            std::size_t parsed = get32(input, position);
            require(parsed <= count + 3 + (CNG_SPLIT ? 4 : 0), "match parsed bound");
            std::span<std::uint8_t> stream = std::span<std::uint8_t>(workspace.parsed).first(parsed);
            cng_huffman_kernel::decode(input.subspan(position + 4, size - 4), stream);
            reconstruct(stream, output, offset, count);
        }
        position += size;
        offset += count;
    }
    require(position == input.size(), "match exact packet extent");
}
}
