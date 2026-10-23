#include "match_events.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace cng_match_event {
namespace {
constexpr std::uint32_t scale = 1024;
constexpr std::uint32_t lower = 32768;
constexpr std::size_t alphabet = 88;
using Counts = std::array<std::uint32_t, alphabet>;
struct Model {
    Counts frequencies{};
    Counts starts{};
    std::array<std::uint64_t, alphabet> reciprocals{};
};
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
void put32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) { bytes[at + byte] = static_cast<std::uint8_t>(value >> (8 * byte)); }
}
std::uint32_t get32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t result = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { result |= static_cast<std::uint32_t>(bytes[at + byte]) << (8 * byte); }
    return result;
}
std::uint32_t get_value(const Event& event, std::size_t field) {
    switch (field) {
        case 0: return event.literals;
        case 1: return event.length;
        case 2: return event.distance;
        default: throw std::runtime_error("event field");
    }
}
void put_value(Event& event, std::size_t field, std::uint32_t value) {
    switch (field) {
        case 0: event.literals = value; break;
        case 1: event.length = value; break;
        case 2: event.distance = value; break;
        default: throw std::runtime_error("event field");
    }
}
std::uint32_t code_distance(std::uint32_t distance, std::array<std::uint32_t, 3>& recent) {
    std::size_t found = 0;
    while (found < recent.size() && recent[found] != distance) { ++found; }
    std::uint32_t code = found < recent.size() ? static_cast<std::uint32_t>(found + 1) : distance + 3;
    std::size_t move = found < recent.size() ? found : recent.size() - 1;
    while (move != 0) { recent[move] = recent[move - 1]; --move; }
    recent[0] = distance;
    return code;
}
std::uint32_t restore_distance(std::uint32_t code, std::array<std::uint32_t, 3>& recent) {
    std::uint32_t distance = code <= 3 ? recent[code - 1] : code - 3;
    require(distance != 0, "event recent distance initialized");
    std::size_t move = code <= 3 ? code - 1 : recent.size() - 1;
    while (move != 0) { recent[move] = recent[move - 1]; --move; }
    recent[0] = distance;
    return distance;
}
struct Bucket {
    std::uint8_t symbol = 0;
    unsigned bits = 0;
    std::uint32_t base = 0;
};
Bucket bucket(std::uint32_t value) {
    if (value < 16) { return Bucket{static_cast<std::uint8_t>(value), 0, value}; }
    unsigned exponent = std::bit_width(value) - 1;
    unsigned bits = exponent - 2;
    std::uint32_t leading = value >> bits;
    std::uint8_t symbol = static_cast<std::uint8_t>(16 + 4 * (exponent - 4) + leading - 4);
    return Bucket{symbol, bits, leading << bits};
}
Bucket expand(std::uint8_t symbol) {
    if (symbol < 16) { return Bucket{symbol, 0, symbol}; }
    unsigned bits = 2 + (symbol - 16) / 4;
    std::uint32_t leading = 4 + (symbol - 16) % 4;
    return Bucket{symbol, bits, leading << bits};
}
struct BitWriter {
    std::span<std::uint8_t> bytes;
    std::size_t position = 0;
    std::uint64_t pending = 0;
    unsigned live = 0;
    void append(std::uint32_t value, unsigned bits) {
        pending |= static_cast<std::uint64_t>(value) << live;
        live += bits;
        while (live >= 8) {
            require(position < bytes.size(), "event suffix capacity");
            bytes[position] = static_cast<std::uint8_t>(pending);
            ++position;
            pending >>= 8;
            live -= 8;
        }
    }
    void finish() {
        if (live != 0) {
            require(position < bytes.size(), "event suffix tail capacity");
            bytes[position] = static_cast<std::uint8_t>(pending);
            ++position;
        }
    }
};
struct BitReader {
    std::span<const std::uint8_t> bytes;
    std::size_t position = 0;
    std::uint64_t pending = 0;
    unsigned live = 0;
    std::uint32_t take(unsigned bits) {
        while (live < bits) {
            require(position < bytes.size(), "event suffix extent");
            pending |= static_cast<std::uint64_t>(bytes[position]) << live;
            ++position;
            live += 8;
        }
        std::uint32_t mask = (1U << bits) - 1;
        std::uint32_t result = static_cast<std::uint32_t>(pending) & mask;
        pending >>= bits;
        live -= bits;
        return result;
    }
};
}
std::size_t encode(std::span<const Event> input, std::span<std::uint8_t> output,
                   std::span<std::uint8_t> scratch) {
    require(input.size() <= 87382, "event shape");
    require(output.size() >= 24 * input.size() + 2048 && scratch.size() >= 24 * input.size() + 64, "event storage");
    std::size_t count = input.size();
    output[0] = 30;
    put32(output, 1, static_cast<std::uint32_t>(input.size()));
    if (count == 0) { return 5; }
    std::array<Counts, 3> populations{};
    std::span<std::uint8_t> symbols = scratch.first(3 * count);
    BitWriter suffix{scratch.subspan(3 * count)};
    std::array<std::uint32_t, 3> recent{};
    for (std::size_t field = 0; field < 3; ++field) {
        for (std::size_t index = 0; index < count; ++index) {
            std::uint32_t value = get_value(input[index], field);
            require(value <= 2097160, "event value bound");
            if (field == 1 && value != 0) {
                require(value >= 3, "event positive match length");
                value -= 2;
            }
            if (field == 2 && value != 0) {
                value = code_distance(value, recent);
            }
            Bucket code = bucket(value);
            require(code.symbol < alphabet, "event alphabet");
            symbols[field * count + index] = code.symbol;
            ++populations[field][code.symbol];
            suffix.append(value - code.base, code.bits);
        }
    }
    suffix.finish();
    std::array<Model, 3> models{};
    BitWriter model_bits{output.subspan(5)};
    for (std::size_t field = 0; field < 3; ++field) {
        std::uint32_t active = 0;
        for (std::uint32_t population : populations[field]) { active += population != 0; }
        model_bits.append(active, 7);
        std::uint32_t emitted = 0;
        Model& model = models[field];
        std::uint64_t accumulated = 0;
        std::uint32_t previous = 0;
        std::uint32_t cumulative = 0;
        for (std::size_t symbol = 0; symbol < alphabet; ++symbol) {
            if (populations[field][symbol] == 0) { continue; }
            accumulated += static_cast<std::uint64_t>(populations[field][symbol]) * (scale - active);
            std::uint32_t apportioned = static_cast<std::uint32_t>(accumulated / count);
            std::uint32_t frequency = 1 + apportioned - previous;
            previous = apportioned;
            model.frequencies[symbol] = frequency;
            model.starts[symbol] = cumulative;
            model.reciprocals[symbol] = ((1ULL << 32) + frequency - 1) / frequency;
            cumulative += frequency;
            model_bits.append(static_cast<std::uint32_t>(symbol), 7);
            ++emitted;
            if (emitted < active) { model_bits.append(frequency, 10); }
        }
        require(cumulative == scale, "event model total");
    }
    model_bits.finish();
    std::size_t header = 5 + model_bits.position;
    put32(output, header, static_cast<std::uint32_t>(suffix.position));
    header += 4;
    std::size_t state_position = header;
    header += 16;
    std::memcpy(output.data() + header, suffix.bytes.data(), suffix.position);
    header += suffix.position;
    std::array<std::uint32_t, 4> states{lower, lower, lower, lower};
    std::size_t tail = output.size();
    for (std::size_t remaining_fields = 3; remaining_fields != 0; --remaining_fields) {
        std::size_t field = remaining_fields - 1;
        const Model& model = models[field];
        for (std::size_t remaining = count; remaining != 0; --remaining) {
            std::size_t index = field * count + remaining - 1;
            std::uint8_t symbol = symbols[index];
            std::uint32_t frequency = model.frequencies[symbol];
            std::uint32_t state = states[index & 3U];
            if (state >= ((lower >> 10) << 16) * frequency) {
                require(tail >= header + 2, "event state capacity");
                tail -= 2;
                output[tail] = static_cast<std::uint8_t>(state);
                output[tail + 1] = static_cast<std::uint8_t>(state >> 8);
                state >>= 16;
            }
            std::uint64_t product = static_cast<std::uint64_t>(state) * model.reciprocals[symbol];
            std::uint32_t quotient = static_cast<std::uint32_t>(product >> 32);
            if (quotient * frequency > state) { --quotient; }
            states[index & 3U] = (quotient << 10) + state - quotient * frequency + model.starts[symbol];
        }
    }
    for (std::size_t lane = 0; lane < 4; ++lane) { put32(output, state_position + 4 * lane, states[lane]); }
    std::size_t payload = output.size() - tail;
    std::memmove(output.data() + header, output.data() + tail, payload);
    std::size_t size = header + payload;
    return size;
}
void decode(std::span<const std::uint8_t> input, std::span<Event> output) {
    require(input.size() >= 5 && output.size() <= 87382, "event decode shape");
    require(input[0] == 30 && get32(input, 1) == output.size(), "event decode header");
    std::size_t count = output.size();
    if (count == 0) { require(input.size() == 5, "event empty extent"); return; }
    std::array<std::array<std::uint32_t, scale>, 3> tables{};
    BitReader model_bits{input.subspan(5)};
    for (std::size_t field = 0; field < 3; ++field) {
        std::size_t active = model_bits.take(7);
        require(active > 0 && active <= alphabet, "event model extent");
        std::uint32_t cumulative = 0;
        int previous = -1;
        for (std::size_t index = 0; index < active; ++index) {
            std::uint8_t symbol = static_cast<std::uint8_t>(model_bits.take(7));
            std::uint32_t frequency = index + 1 == active ? scale - cumulative : model_bits.take(10);
            require(symbol > previous && symbol < alphabet && frequency > 0 && frequency <= scale - cumulative, "event model population");
            previous = symbol;
            for (std::uint32_t slot = cumulative; slot < cumulative + frequency; ++slot) {
                tables[field][slot] = ((frequency - 1) << 22) | ((slot - cumulative) << 12) | symbol;
            }
            cumulative += frequency;
        }
        require(cumulative == scale, "event model sum");
    }
    require(model_bits.pending == 0, "event model padding");
    std::size_t position = 5 + model_bits.position;
    require(input.size() - position >= 20, "event states extent");
    std::size_t suffix_size = get32(input, position);
    position += 4;
    std::array<std::uint32_t, 4> states{};
    for (std::size_t lane = 0; lane < 4; ++lane) {
        states[lane] = get32(input, position);
        require(states[lane] >= lower && states[lane] < (lower << 16), "event state bound");
        position += 4;
    }
    require(suffix_size <= input.size() - position, "event suffix bound");
    BitReader suffix{input.subspan(position, suffix_size)};
    position += suffix_size;
    std::array<std::uint32_t, 3> recent{};
    for (std::size_t field = 0; field < 3; ++field) {
        const std::array<std::uint32_t, scale>& table = tables[field];
        for (std::size_t index = 0; index < count; ++index) {
            std::size_t lane = (field * count + index) & 3U;
            std::uint32_t state = states[lane];
            std::uint32_t entry = table[state & (scale - 1)];
            std::uint32_t frequency = (entry >> 22) + 1;
            state = frequency * (state >> 10) + ((entry >> 12) & (scale - 1));
            if (state < lower) {
                require(input.size() - position >= 2, "event word extent");
                std::uint32_t word = input[position] | (static_cast<std::uint32_t>(input[position + 1]) << 8);
                state = (state << 16) | word;
                position += 2;
            }
            states[lane] = state;
            Bucket code = expand(static_cast<std::uint8_t>(entry));
            std::uint32_t value = code.base + suffix.take(code.bits);
            if (field == 1 && value != 0) { value += 2; }
            if (field == 2 && value != 0) {
                value = restore_distance(value, recent);
            }
            require(value <= 2097160, "event decoded bound");
            put_value(output[index], field, value);
        }
    }
    require(position == input.size() && suffix.position == suffix.bytes.size() && suffix.pending == 0, "event exact consumption");
    for (std::uint32_t state : states) { require(state == lower, "event terminal state"); }
}
}
