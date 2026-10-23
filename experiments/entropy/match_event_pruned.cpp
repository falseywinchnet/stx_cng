#include "match_event_backend.hpp"
#include "huffman_kernel.hpp"
#include "huffman_prices.hpp"
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
std::size_t extend(std::span<const std::uint8_t> input, std::size_t previous, std::size_t position, std::size_t end) {
    std::size_t length = 3;
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
    std::size_t events;
    std::size_t literals;
};
ParseResult collect(std::span<const std::uint8_t> input, std::size_t begin, std::size_t count,
                  std::size_t mask, EncodeWorkspace& workspace) {
    std::size_t end = begin + count;
    std::size_t event_count = 0;
    std::size_t literal_count = 0;
    std::size_t anchor = begin;
    std::size_t position = begin;
    std::size_t misses = 0;
    // Hash exactly three bytes; the fourth byte is only a bounded load aid.
    // An unmatched final three-byte tail stays literal in this first trial.
    while (end - position >= 4) {
        std::uint32_t word = get32(input, position) & 0xffffffU;
        std::size_t slot = hash(word) & mask;
        std::uint32_t entry = workspace.index[slot];
        workspace.index[slot] = static_cast<std::uint32_t>(position + 1);
        if (entry == 0 || (get32(input, entry - 1) & 0xffffffU) != word) {
            ++misses;
            position += std::min<std::size_t>(1 + (misses >> 6), end - position);
            continue;
        }
        std::size_t previous = entry - 1;
        std::size_t length = extend(input, previous, position, end);
        std::size_t literals = position - anchor;
        require(event_count < workspace.events.size() && literals <= workspace.literals.size() - literal_count, "match event capacity");
        workspace.events[event_count] = cng_match_event::Event{static_cast<std::uint32_t>(literals),
            static_cast<std::uint32_t>(length), static_cast<std::uint32_t>(position - previous)};
        ++event_count;
        literal_count += literals;
        position += length;
        anchor = position;
        misses = 0;
        std::size_t last = position - 2;
        if (input.size() - last >= 4) { workspace.index[hash(get32(input, last) & 0xffffffU) & mask] = static_cast<std::uint32_t>(last + 1); }
    }
    if (event_count == 0) { return ParseResult{0, 0}; }
    if (anchor != end) {
        std::size_t literals = end - anchor;
        require(event_count < workspace.events.size() && literals <= workspace.literals.size() - literal_count, "match event tail capacity");
        workspace.events[event_count] = cng_match_event::Event{static_cast<std::uint32_t>(literals), 0, 0};
        ++event_count;
        literal_count += literals;
    }
    return ParseResult{event_count, literal_count};
}
// Q8 binary logarithm, rounded down. Each squaring keeps the normalized value
// in [2^31,2^32), so its unsigned 64-bit product cannot overflow.
std::uint32_t logarithm_q8(std::uint32_t value) {
    require(value != 0, "positive cost population");
    unsigned exponent = std::bit_width(value) - 1;
    std::uint64_t normalized = static_cast<std::uint64_t>(value) << (31 - exponent);
    std::uint32_t result = exponent << 8;
    for (unsigned bit = 128; bit != 0; bit >>= 1) {
        normalized = (normalized * normalized) >> 31;
        if (normalized >= (1ULL << 32)) { normalized >>= 1; result |= bit; }
    }
    return result;
}
struct PriceBucket {
    std::size_t symbol;
    unsigned suffix;
};
PriceBucket price_bucket(std::uint32_t value) {
    if (value < 16) { return PriceBucket{value, 0}; }
    unsigned exponent = std::bit_width(value) - 1;
    unsigned suffix = exponent - 2;
    std::size_t symbol = 16 + 4 * (exponent - 4) + (value >> suffix) - 4;
    return PriceBucket{symbol, suffix};
}
std::uint32_t distance_price_code(std::uint32_t distance, std::array<std::uint32_t, 3>& recent) {
    std::size_t found = 0;
    while (found < recent.size() && recent[found] != distance) { ++found; }
    std::uint32_t code = found < recent.size() ? static_cast<std::uint32_t>(found + 1) : distance + 3;
    std::size_t move = found < recent.size() ? found : recent.size() - 1;
    while (move != 0) { recent[move] = recent[move - 1]; --move; }
    recent[0] = distance;
    return code;
}
using PriceTable = std::array<std::uint32_t, 88>;
void event_prices(std::span<const cng_match_event::Event> events, std::array<PriceTable, 3>& prices) {
    std::array<PriceTable, 3> counts{};
    std::array<std::uint32_t, 3> recent{};
    for (const cng_match_event::Event& event : events) {
        std::uint32_t length = event.length == 0 ? 0 : event.length - 2;
        std::uint32_t distance = event.distance == 0 ? 0 : distance_price_code(event.distance, recent);
        ++counts[0][price_bucket(event.literals).symbol];
        ++counts[1][price_bucket(length).symbol];
        ++counts[2][price_bucket(distance).symbol];
    }
    for (std::size_t field = 0; field < 3; ++field) {
        std::uint32_t active = 0;
        for (std::uint32_t count : counts[field]) { active += count != 0; }
        std::uint64_t accumulated = 0;
        std::uint32_t previous = 0;
        for (std::size_t symbol = 0; symbol < 88; ++symbol) {
            // A newly created literal-count bucket gets the one-slot price.
            prices[field][symbol] = 2560;
            if (counts[field][symbol] == 0) { continue; }
            accumulated += static_cast<std::uint64_t>(counts[field][symbol]) * (1024 - active);
            std::uint32_t apportioned = static_cast<std::uint32_t>(accumulated / events.size());
            std::uint32_t frequency = 1 + apportioned - previous;
            previous = apportioned;
            prices[field][symbol] = 2560 - logarithm_q8(frequency);
        }
    }
}
std::int32_t event_price(std::uint32_t value, const PriceTable& table) {
    PriceBucket code = price_bucket(value);
    std::int32_t result = static_cast<std::int32_t>(table[code.symbol] + 256 * code.suffix);
    return result;
}
ParseResult parse(std::span<const std::uint8_t> input, std::size_t begin, std::size_t count,
                  std::size_t mask, EncodeWorkspace& workspace) {
    ParseResult initial = collect(input, begin, count, mask, workspace);
    if (initial.events == 0) { return initial; }
    std::array<std::uint8_t, 256> literal_prices{};
    cng_huffman_kernel::price_lengths(input.subspan(begin, count), literal_prices);
    unsigned minimum_price = 13;
    for (std::uint8_t price : literal_prices) {
        if (price != 0) { minimum_price = std::min<unsigned>(minimum_price, price); }
    }
    if (minimum_price == 13) { return ParseResult{0, 0}; }
    std::array<PriceTable, 3> prices{};
    event_prices(std::span<const cng_match_event::Event>(workspace.events).first(initial.events), prices);
    std::array<std::uint32_t, 3> recent{};
    std::size_t position = begin;
    std::size_t anchor = begin;
    std::size_t kept = 0;
    std::size_t literal_count = 0;
    for (std::size_t index = 0; index < initial.events; ++index) {
        // Read before compacting: kept <= index, so later source records survive.
        cng_match_event::Event event = workspace.events[index];
        position += event.literals;
        if (event.length == 0) { break; }
        std::size_t literals = position - anchor;
        std::array<std::uint32_t, 3> candidate_recent = recent;
        std::uint32_t distance_code = distance_price_code(event.distance, candidate_recent);
        std::int32_t cost = event_price(static_cast<std::uint32_t>(literals), prices[0]);
        cost += event_price(event.length - 2, prices[1]);
        cost += event_price(distance_code, prices[2]);
        cost -= event_price(static_cast<std::uint32_t>(literals + event.length), prices[0]);
        std::uint64_t saved = static_cast<std::uint64_t>(event.length) * minimum_price * 256;
        if (cost >= 0 && saved <= static_cast<std::uint32_t>(cost)) {
            saved = 0;
            for (std::size_t byte = 0; byte < event.length; ++byte) { saved += 256U * literal_prices[input[position + byte]]; }
        }
        if (cost < 0 || saved > static_cast<std::uint32_t>(cost)) {
            require(kept < workspace.events.size() && literals <= workspace.literals.size() - literal_count, "pruned event capacity");
            workspace.events[kept] = cng_match_event::Event{static_cast<std::uint32_t>(literals), event.length, event.distance};
            ++kept;
            if (literals != 0) { std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals); }
            literal_count += literals;
            anchor = position + event.length;
            recent = candidate_recent;
        }
        position += event.length;
    }
    if (kept == 0) { return ParseResult{0, 0}; }
    std::size_t end = begin + count;
    if (anchor != end) {
        std::size_t literals = end - anchor;
        require(kept < workspace.events.size() && literals <= workspace.literals.size() - literal_count, "pruned terminal capacity");
        workspace.events[kept] = cng_match_event::Event{static_cast<std::uint32_t>(literals), 0, 0};
        ++kept;
        std::memcpy(workspace.literals.data() + literal_count, input.data() + anchor, literals);
        literal_count += literals;
    }
    return ParseResult{kept, literal_count};
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
void reconstruct(std::span<const cng_match_event::Event> events, std::span<const std::uint8_t> literals,
                 std::span<std::uint8_t> output, std::size_t begin, std::size_t count) {
    std::size_t end = begin + count;
    std::size_t written = begin;
    std::size_t read = 0;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const cng_match_event::Event& event = events[index];
        require(event.literals <= literals.size() - read && event.literals <= end - written, "match event literals");
        if (event.literals != 0) { std::memcpy(output.data() + written, literals.data() + read, event.literals); }
        read += event.literals;
        written += event.literals;
        if (event.length == 0) {
            require(index + 1 == events.size() && event.distance == 0, "match terminal event");
            continue;
        }
        require(event.length >= 3 && event.length <= end - written && event.distance > 0 && event.distance <= written, "match event reference");
        copy_match(output, written, event.distance, event.length);
        written += event.length;
    }
    require(written == end && read == literals.size(), "match event exact reconstruction");
}
}
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace) {
    require(input.size() <= cng_entropy_trial::maximum_input && output.size() >= 2 * input.size() + 1024, "match event packet capacity");
    output[0] = 31;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t slots = std::bit_floor(std::min<std::size_t>(16384, std::max<std::size_t>(16, input.size() / 4)));
    std::fill_n(workspace.index.data(), slots, 0);
    std::size_t written = 5;
    for (std::size_t begin = 0; begin < input.size();) {
        std::size_t count = std::min(block_size, input.size() - begin);
        ParseResult parsed = parse(input, begin, count, slots - 1, workspace);
        std::size_t controls = 0;
        std::size_t literal_bytes = 0;
        std::size_t size = 0;
        if (parsed.events == 0) {
            literal_bytes = cng_huffman_kernel::encode(input.subspan(begin, count), workspace.coded_literals);
            size = literal_bytes;
        } else {
            controls = cng_match_event::encode(std::span<const cng_match_event::Event>(workspace.events).first(parsed.events), workspace.controls, workspace.scratch);
            literal_bytes = cng_huffman_kernel::encode(std::span<const std::uint8_t>(workspace.literals).first(parsed.literals), workspace.coded_literals);
            size = 8 + controls + literal_bytes;
        }
        if (size >= count) {
            put32(output, written, 0x80000000U | static_cast<std::uint32_t>(count));
            std::memcpy(output.data() + written + 4, input.data() + begin, count);
            written += 4 + count;
        } else if (parsed.events == 0) {
            put32(output, written, 0x40000000U | static_cast<std::uint32_t>(size));
            std::memcpy(output.data() + written + 4, workspace.coded_literals.data(), literal_bytes);
            written += 4 + size;
        } else {
            put32(output, written, static_cast<std::uint32_t>(size));
            put32(output, written + 4, static_cast<std::uint32_t>(parsed.events));
            put32(output, written + 8, static_cast<std::uint32_t>(controls));
            std::memcpy(output.data() + written + 12, workspace.controls.data(), controls);
            std::memcpy(output.data() + written + 12 + controls, workspace.coded_literals.data(), literal_bytes);
            written += 4 + size;
        }
        begin += count;
    }
    if (written >= input.size() + 5) {
        output[0] = 0;
        if (!input.empty()) { std::memcpy(output.data() + 5, input.data(), input.size()); }
        written = input.size() + 5;
    }
    return written;
}
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, DecodeWorkspace& workspace) {
    require(input.size() >= 5 && output.size() <= cng_entropy_trial::maximum_input && get32(input, 1) == output.size(), "match event output extent");
    if (input[0] == 0) {
        require(input.size() == output.size() + 5, "match event raw extent");
        if (!output.empty()) { std::memcpy(output.data(), input.data() + 5, output.size()); }
        return;
    }
    require(input[0] == 31 && !output.empty(), "match event coding");
    std::size_t position = 5;
    for (std::size_t begin = 0; begin < output.size();) {
        require(input.size() - position >= 4, "match event block header");
        std::uint32_t header = get32(input, position);
        position += 4;
        std::size_t size = header & 0x3fffffffU;
        std::size_t count = std::min(block_size, output.size() - begin);
        require(size <= input.size() - position, "match event block extent");
        if ((header & 0x80000000U) != 0) {
            require(size == count, "match event raw block");
            std::memcpy(output.data() + begin, input.data() + position, count);
        } else if ((header & 0x40000000U) != 0) {
            cng_huffman_kernel::decode(input.subspan(position, size), output.subspan(begin, count));
        } else {
            require(size >= 18, "match event coded header");
            std::size_t events = get32(input, position);
            std::size_t controls = get32(input, position + 4);
            require(events > 0 && events <= count / 3 + 1 && controls <= size - 13, "match event counts");
            std::span<cng_match_event::Event> records = std::span<cng_match_event::Event>(workspace.events).first(events);
            cng_match_event::decode(input.subspan(position + 8, controls), records);
            std::span<const std::uint8_t> literal_packet = input.subspan(position + 8 + controls, size - 8 - controls);
            std::size_t literal_count = get32(literal_packet, 1);
            require(literal_count <= count, "match event literal count");
            std::span<std::uint8_t> literals = std::span<std::uint8_t>(workspace.literals).first(literal_count);
            cng_huffman_kernel::decode(literal_packet, literals);
            reconstruct(records, literals, output, begin, count);
        }
        position += size;
        begin += count;
    }
    require(position == input.size(), "match event exact packet");
}
}
