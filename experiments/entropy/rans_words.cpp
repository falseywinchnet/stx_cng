#include "rans.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace cng_entropy_trial {
namespace {
constexpr std::uint32_t lower = 1U << 15;
constexpr std::uint32_t total = 4096;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
bool overlap(std::span<const std::uint8_t> first, std::span<std::uint8_t> second) {
    if (first.empty() || second.empty()) { return false; }
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(first.data());
    std::uintptr_t b = reinterpret_cast<std::uintptr_t>(second.data());
    bool result = a <= b ? b - a < first.size() : a - b < second.size();
    return result;
}
void write_u32(std::span<std::uint8_t> bytes, std::size_t position, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[position + index] = static_cast<std::uint8_t>(value >> (8 * index));
    }
}
std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t position) {
    std::uint32_t result = 0;
    for (unsigned index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(bytes[position + index]) << (8 * index);
    }
    return result;
}
std::uint8_t decode_symbol(std::uint32_t& state, const std::array<std::uint32_t, total>& table,
                          std::span<const std::uint8_t> input, std::size_t& position) {
    // One table load: frequency-minus-one (12 bits), slot bias (12), symbol (8).
    std::uint32_t entry = table[state & (total - 1)];
    std::uint32_t frequency = (entry >> 20) + 1;
    std::uint32_t bias = (entry >> 8) & 4095U;
    state = frequency * (state >> 12) + bias;
    if (state < lower) {
        require(position <= input.size() && input.size() - position >= 2, "rANS word refill extent");
        std::uint32_t word = input[position] | (static_cast<std::uint32_t>(input[position + 1]) << 8);
        state = (state << 16) | word;
        position += 2;
    }
    std::uint8_t result = static_cast<std::uint8_t>(entry);
    return result;
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() <= maximum_input && output.size() >= 2 * input.size() + 1024,
            "rANS encode capacity");
    require(!overlap(input, output), "rANS disjoint storage");
    std::array<std::uint32_t, 256> counts{};
    for (std::uint8_t value : input) { ++counts[value]; }
    std::uint32_t active = 0;
    for (std::uint32_t count : counts) { active += count != 0; }
    output[0] = 0;
    write_u32(output, 1, static_cast<std::uint32_t>(input.size()));
    if (input.empty()) { return 5; }
    std::array<std::uint32_t, 256> frequencies{};
    std::array<std::uint32_t, 256> starts{};
    std::array<std::uint64_t, 256> reciprocals{};
    std::uint64_t accumulated = 0;
    std::uint32_t previous = 0;
    std::uint32_t cumulative = 0;
    std::size_t header = 7;
    // Reserve one slot per observed symbol, then apportion the remaining slots
    // by cumulative population. Every frequency is positive and the sum is exact.
    for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
        if (counts[symbol] == 0) { continue; }
        accumulated += static_cast<std::uint64_t>(counts[symbol]) * (total - active);
        std::uint32_t apportioned = static_cast<std::uint32_t>(accumulated / input.size());
        std::uint32_t frequency = 1 + apportioned - previous;
        previous = apportioned;
        frequencies[symbol] = frequency;
        reciprocals[symbol] = ((1ULL << 32) + frequency - 1) / frequency;
        starts[symbol] = cumulative;
        cumulative += frequency;
        output[header] = static_cast<std::uint8_t>(symbol);
        output[header + 1] = static_cast<std::uint8_t>(frequency);
        output[header + 2] = static_cast<std::uint8_t>(frequency >> 8);
        header += 3;
    }
    require(cumulative == total, "rANS frequency total");
    output[5] = static_cast<std::uint8_t>(active);
    output[6] = static_cast<std::uint8_t>(active >> 8);
    std::array<std::uint32_t, 4> states{lower, lower, lower, lower};
    std::size_t tail = output.size();
    for (std::size_t remaining = input.size(); remaining != 0; --remaining) {
        std::size_t index = remaining - 1;
        std::uint8_t symbol = input[index];
        std::uint32_t frequency = frequencies[symbol];
        std::uint32_t state = states[index & 3U];
        std::uint32_t threshold = ((lower >> 12) << 16) * frequency;
        if (state >= threshold) {
            require(tail >= header + 18, "rANS encoded word extent");
            tail -= 2;
            output[tail] = static_cast<std::uint8_t>(state);
            output[tail + 1] = static_cast<std::uint8_t>(state >> 8);
            state >>= 16;
        }
        // Ceil(2^32/f) gives floor(state/f) or one larger for state < 2^31.
        // Correct the single possible overshoot; the product remains in range.
        std::uint64_t product = static_cast<std::uint64_t>(state) * reciprocals[symbol];
        std::uint32_t quotient = static_cast<std::uint32_t>(product >> 32);
        if (quotient * frequency > state) { --quotient; }
        std::uint32_t remainder = state - quotient * frequency;
        states[index & 3U] = (quotient << 12) + remainder + starts[symbol];
    }
    std::size_t payload = output.size() - tail;
    std::size_t size = header + 16 + payload;
    if (size >= input.size() + 5) {
        std::memcpy(output.data() + 5, input.data(), input.size());
        std::size_t result = input.size() + 5;
        return result;
    }
    output[0] = 2;
    for (std::size_t index = 0; index < 4; ++index) { write_u32(output, header + 4 * index, states[index]); }
    std::memmove(output.data() + header + 16, output.data() + tail, payload);
    return size;
}
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    require(input.size() >= 5 && output.size() <= maximum_input, "rANS decode extent");
    require(!overlap(input, output), "rANS disjoint storage");
    require(read_u32(input, 1) == output.size(), "rANS output extent");
    if (input[0] == 0) {
        require(input.size() - 5 == output.size(), "rANS stored extent");
        if (!output.empty()) { std::memcpy(output.data(), input.data() + 5, output.size()); }
        return;
    }
    require(input[0] == 2 && input.size() >= 23, "rANS coding");
    std::size_t active = input[5] | (static_cast<std::size_t>(input[6]) << 8);
    require(active >= 1 && active <= 256 && 7 + 3 * active + 16 <= input.size(), "rANS model extent");
    std::array<std::uint32_t, total> table{};
    std::size_t position = 7;
    std::uint32_t cumulative = 0;
    int previous = -1;
    for (std::size_t index = 0; index < active; ++index) {
        std::uint8_t value = input[position];
        std::uint32_t frequency = input[position + 1] | (static_cast<std::uint32_t>(input[position + 2]) << 8);
        require(value > previous && frequency != 0 && frequency <= total - cumulative, "rANS model population");
        previous = value;
        for (std::uint32_t slot = cumulative; slot < cumulative + frequency; ++slot) {
            table[slot] = ((frequency - 1) << 20) | ((slot - cumulative) << 8) | value;
        }
        cumulative += frequency;
        position += 3;
    }
    require(cumulative == total, "rANS model sum");
    std::array<std::uint32_t, 4> states{};
    for (std::size_t index = 0; index < 4; ++index) {
        states[index] = read_u32(input, position);
        require(states[index] >= lower && states[index] < (lower << 16), "rANS state extent");
        position += 4;
    }
    std::size_t index = 0;
    // Four independent recurrence states, with one shared byte cursor. Explicit
    // groups expose instruction-level parallelism without architecture intrinsics.
    for (; output.size() - index >= 4; index += 4) {
        output[index] = decode_symbol(states[0], table, input, position);
        output[index + 1] = decode_symbol(states[1], table, input, position);
        output[index + 2] = decode_symbol(states[2], table, input, position);
        output[index + 3] = decode_symbol(states[3], table, input, position);
    }
    for (; index < output.size(); ++index) {
        output[index] = decode_symbol(states[index & 3U], table, input, position);
    }
    require(position == input.size(), "rANS exact input consumption");
    for (std::uint32_t state : states) { require(state == lower, "rANS terminal state"); }
}
}
