#include "runs.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace cng_entropy_trial {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}
void write_run_integer(std::span<std::uint8_t> storage, std::size_t& position, std::size_t value) {
    while (value >= 128) {
        require(position != 0, "run descriptor capacity");
        --position;
        storage[position] = static_cast<std::uint8_t>((value & 127U) | 128U);
        value >>= 7;
    }
    require(position != 0, "run descriptor capacity");
    --position;
    storage[position] = static_cast<std::uint8_t>(value);
}
std::size_t read_run_integer(std::span<const std::uint8_t> raw, std::size_t& position, std::size_t literal) {
    std::size_t value = 0;
    for (unsigned shift = 0; shift <= 28; shift += 7) {
        require(position > literal, "run integer extent");
        --position;
        unsigned byte = raw[position];
        value |= static_cast<std::size_t>(byte & 127U) << shift;
        if (byte < 128) {
            require(shift == 0 || byte != 0, "noncanonical run integer");
            return value;
        }
    }
    throw std::runtime_error("run integer extent");
}
std::size_t separate_runs(std::span<const std::uint8_t> packed, std::span<std::uint8_t> separated) {
    require(packed.size() <= 1048576 && separated.size() >= 2 * packed.size() + 4, "run separation capacity");
    // Literals occupy the first N bytes; metadata grows backward in the
    // remaining N+4 bytes. The regions are disjoint throughout construction.
    std::span<std::uint8_t> descriptors = separated.subspan(packed.size());
    std::size_t descriptor_position = descriptors.size();
    std::size_t literal_count = 0;
    std::size_t consumed = 0;
    std::size_t position = 0;
    while (packed.size() - position >= 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, packed.data() + position, sizeof(word));
        // Equality of adjacent octets is independent of byte order/alignment.
        if (((word ^ (word >> 8)) & 0x00ffffffU) != 0) {
            ++position;
            continue;
        }
        std::size_t begin = position;
        std::uint8_t value = packed[position];
        position += 4;
        while (position < packed.size() && packed[position] == value) { ++position; }
        std::size_t literal_size = begin - consumed;
        if (literal_size != 0) {
            write_run_integer(descriptors, descriptor_position, 2 * (literal_size - 1));
            std::memcpy(separated.data() + literal_count, packed.data() + consumed, literal_size);
            literal_count += literal_size;
        }
        std::size_t run_size = position - begin;
        write_run_integer(descriptors, descriptor_position, 2 * (run_size - 1) + 1);
        require(descriptor_position != 0, "run value capacity");
        --descriptor_position;
        descriptors[descriptor_position] = value;
        consumed = position;
    }
    if (consumed < packed.size()) {
        std::size_t literal_size = packed.size() - consumed;
        write_run_integer(descriptors, descriptor_position, 2 * (literal_size - 1));
        std::memcpy(separated.data() + literal_count, packed.data() + consumed, literal_size);
        literal_count += literal_size;
    }
    // Move the reversed metadata beside the literals; these ranges can overlap.
    std::size_t descriptor_count = descriptors.size() - descriptor_position;
    std::memmove(separated.data() + literal_count, descriptors.data() + descriptor_position, descriptor_count);
    std::size_t result = descriptor_count + literal_count;
    return result;
}
void restore_runs(std::span<const std::uint8_t> raw, std::span<std::uint8_t> packed) {
    require(raw.size() <= 2 * packed.size() + 4, "run stream bound");
    std::size_t position = raw.size();
    std::size_t literal = 0;
    std::size_t written = 0;
    while (written < packed.size()) {
        std::size_t code = read_run_integer(raw, position, literal);
        std::size_t length = (code >> 1) + 1;
        require(length <= packed.size() - written, "run output extent");
        if ((code & 1U) != 0) {
            require(length >= 4 && position > literal, "run value extent");
            --position;
            std::fill_n(packed.data() + written, length, raw[position]);
        } else {
            require(length <= position - literal, "literal stream extent");
            std::memcpy(packed.data() + written, raw.data() + literal, length);
            literal += length;
        }
        written += length;
    }
    require(position == literal, "unconsumed run bytes");
}

}
