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
    for (unsigned shift = 0; shift <= 14; shift += 7) {
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
std::size_t extend(std::span<const std::uint8_t> input, std::size_t previous, std::size_t position) {
    std::size_t length = 8;
    std::size_t remaining = input.size() - position;
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
    std::size_t literals;
    bool references;
};
ParseResult parse(std::span<const std::uint8_t> input, EncodeWorkspace& workspace) {
    std::size_t slots = std::bit_floor(std::min<std::size_t>(16384, std::max<std::size_t>(16, input.size() / 4)));
    std::fill_n(workspace.index.data(), slots, 0);
    std::size_t mask = slots - 1;
    std::span<std::uint8_t> output(workspace.parsed);
    bool references = false;
    std::size_t literal_count = 0;
    std::size_t written = 0;
    std::size_t anchor = 0;
    std::size_t position = 0;
    std::size_t misses = 0;
    while (input.size() - position >= 8) {
        std::uint32_t word = get32(input, position);
        std::size_t slot = hash(word) & mask;
        std::uint32_t entry = workspace.index[slot];
        workspace.index[slot] = static_cast<std::uint32_t>(position + 1);
        if (entry == 0) {
            ++misses;
            position += std::min<std::size_t>(1 + (misses >> 6), input.size() - position);
            continue;
        }
        std::size_t previous = entry - 1;
        std::size_t distance = position - previous;
        if (distance > 65535 || get32(input, previous) != word
            || get32(input, previous + 4) != get32(input, position + 4)) {
            ++misses;
            position += std::min<std::size_t>(1 + (misses >> 6), input.size() - position);
            continue;
        }
        references = true;
        misses = 0;
        std::size_t length = extend(input, previous, position);
        std::size_t literals = position - anchor;
        integer(output, written, literals);
        require(literals <= workspace.literals.size() - literal_count, "match literal capacity");
        if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
        literal_count += literals;
        require(output.size() - written >= 2, "match distance capacity");
        output[written] = static_cast<std::uint8_t>(distance);
        output[written + 1] = static_cast<std::uint8_t>(distance >> 8);
        written += 2;
        integer(output, written, length - 8);
        position += length;
        anchor = position;
        // Retain the last complete short word before the next query. Interior
        // positions need no index work once this exact match consumes them.
        std::size_t last = position - 2;
        if (input.size() - last >= 4) { workspace.index[hash(get32(input, last)) & mask] = static_cast<std::uint32_t>(last + 1); }
    }
    if (!references) { return ParseResult{0, 0, false}; }
    if (anchor != input.size() || input.empty()) {
        std::size_t literals = input.size() - anchor;
        integer(output, written, literals);
        require(literals <= workspace.literals.size() - literal_count, "match tail capacity");
        if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
        literal_count += literals;
    }
    require(written <= input.size() + 3, "match parse bound");
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
void reconstruct(std::span<const std::uint8_t> input, std::span<const std::uint8_t> source, std::span<std::uint8_t> output) {
    std::size_t literal_position = 0;
    std::size_t read = 0;
    std::size_t written = 0;
    while (written < output.size()) {
        std::size_t literals = integer(input, read);
        require(literals <= source.size() - literal_position && literals <= output.size() - written, "match literal extent");
        if (literals != 0) { std::memcpy(output.data() + written, source.data() + literal_position, literals); }
        literal_position += literals;
        written += literals;
        if (written == output.size()) { break; }
        require(input.size() - read >= 2, "match distance extent");
        std::size_t distance = input[read] | (static_cast<std::size_t>(input[read + 1]) << 8);
        read += 2;
        std::size_t length = integer(input, read) + 8;
        require(distance != 0 && distance <= written && length <= output.size() - written, "match reference extent");
        copy_match(output, written, distance, length);
        written += length;
    }
    require(read == input.size() && literal_position == source.size(), "match exact consumption");
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace) {
    require(input.size() <= cng_entropy_trial::maximum_input && output.size() >= 2 * input.size() + 1024, "match packet capacity");
    output[0] = 15;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t position = 5;
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t count = std::min(block_size, input.size() - offset);
        std::span<const std::uint8_t> block = input.subspan(offset, count);
        ParseResult parsed = parse(block, workspace);
        std::size_t coded = 0;
        std::uint32_t flags = 0;
        if (!parsed.references) {
            coded = cng_huffman_kernel::encode(block, output.subspan(position + 4));
            flags = 0x40000000U;
        } else {
            std::span<const std::uint8_t> controls = std::span<const std::uint8_t>(workspace.parsed).first(parsed.size);
            std::span<const std::uint8_t> literals = std::span<const std::uint8_t>(workspace.literals).first(parsed.literals);
            std::size_t control_bytes = cng_huffman_kernel::encode(controls, output.subspan(position + 16));
            std::size_t literal_bytes = cng_huffman_kernel::encode(literals, output.subspan(position + 16 + control_bytes));
            put32(output, position + 4, static_cast<std::uint32_t>(parsed.size));
            put32(output, position + 8, static_cast<std::uint32_t>(parsed.literals));
            put32(output, position + 12, static_cast<std::uint32_t>(control_bytes));
            coded = 12 + control_bytes + literal_bytes;
        }
        if (coded >= count) {
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
    require(input[0] == 15 && !output.empty(), "match packet coding");
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
            require(size >= 22, "match coded block header");
            std::size_t parsed = get32(input, position);
            std::size_t literals = get32(input, position + 4);
            std::size_t control_bytes = get32(input, position + 8);
            require(parsed <= count + 3 && literals <= count && control_bytes <= size - 12, "match parsed bound");
            std::span<std::uint8_t> controls = std::span<std::uint8_t>(workspace.parsed).first(parsed);
            std::span<std::uint8_t> source = std::span<std::uint8_t>(workspace.literals).first(literals);
            cng_huffman_kernel::decode(input.subspan(position + 12, control_bytes), controls);
            cng_huffman_kernel::decode(input.subspan(position + 12 + control_bytes, size - 12 - control_bytes), source);
            reconstruct(controls, source, output.subspan(offset, count));
        }
        position += size;
        offset += count;
    }
    require(position == input.size(), "match exact packet extent");
}
}
