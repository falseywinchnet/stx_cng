#include "direct_words.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace stx_cng_detail {
namespace {
bool overlaps_output(const void* input, std::size_t bytes,
                     std::span<std::uint8_t> output) {
    if (bytes == 0) { return false; }
    std::uintptr_t input_begin = reinterpret_cast<std::uintptr_t>(input);
    std::uintptr_t output_begin = reinterpret_cast<std::uintptr_t>(output.data());
    bool overlaps = input_begin <= output_begin
        ? output_begin - input_begin < bytes
        : input_begin - output_begin < output.size();
    return overlaps;
}

// Per-byte addition modulo 256. Low seven-bit sums cannot carry into the
// next byte. XOR restores the input high bits and discards each byte's carry.
std::uint32_t add_bytes(std::uint32_t left, std::uint32_t right) {
    constexpr std::uint32_t low = 0x7f7f7f7fU;
    constexpr std::uint32_t high = 0x80808080U;
    std::uint32_t sum = (left & low) + (right & low);
    std::uint32_t upper = (left ^ right) & high;
    std::uint32_t result = sum ^ upper;
    return result;
}
std::uint32_t subtract_bytes(std::uint32_t left, std::uint32_t right) {
    constexpr std::uint32_t low = 0x7f7f7f7fU;
    constexpr std::uint32_t high = 0x80808080U;
    std::uint32_t difference = (left | high) - (right & low);
    std::uint32_t upper = (~(left ^ right)) & high;
    std::uint32_t result = difference ^ upper;
    return result;
}
template<std::size_t channels>
std::uint32_t load_pixel(const std::uint8_t* source) {
    std::uint32_t word = 0;
    std::memcpy(&word, source, channels);
    return word;
}
template<std::size_t channels>
void store_pixel(std::uint8_t* destination, std::uint32_t word) {
    std::memcpy(destination, &word, channels);
}

template<std::size_t channels>
void restore_kernel(std::span<const Sequence> sequences,
                    std::span<const std::uint8_t> literals,
                    std::span<const std::uint8_t> previous,
                    std::span<std::uint8_t> output) {
    std::size_t row_bytes = previous.size();
    std::size_t position = 0;
    std::size_t consumed = 0;
    std::size_t column = 0;
    for (const Sequence& sequence : sequences) {
        std::size_t remaining = static_cast<std::size_t>(sequence.literals) * channels;
        while (remaining != 0) {
            std::size_t count = std::min(remaining, row_bytes - column);
            const std::uint8_t* above = position < row_bytes
                ? previous.data() : output.data() + position - column - row_bytes;
            std::uint32_t sum = 0;
            if (column != 0) {
                std::uint32_t left = load_pixel<channels>(output.data() + position - channels);
                std::uint32_t upper_left = load_pixel<channels>(above + column - channels);
                sum = subtract_bytes(left, upper_left);
            }
            for (std::size_t offset = 0; offset < count; offset += channels) {
                std::uint32_t residual = load_pixel<channels>(literals.data() + consumed + offset);
                std::uint32_t upper = load_pixel<channels>(above + column + offset);
                sum = add_bytes(sum, residual);
                std::uint32_t value = add_bytes(sum, upper);
                store_pixel<channels>(output.data() + position + offset, value);
            }
            position += count;
            column += count;
            if (column == row_bytes) { column = 0; }
            consumed += count;
            remaining -= count;
        }
        std::size_t length = static_cast<std::size_t>(sequence.length) * channels;
        std::size_t distance = static_cast<std::size_t>(sequence.distance) * channels;
        if (length != 0 && distance == 0) {
            std::size_t remaining_prediction = length;
            while (remaining_prediction != 0) {
                std::size_t count = std::min(remaining_prediction, row_bytes - column);
                const std::uint8_t* above = position < row_bytes
                    ? previous.data() : output.data() + position - column - row_bytes;
                if (column == 0) {
                    std::memcpy(output.data() + position, above, count);
                } else {
                    std::uint32_t left = load_pixel<channels>(output.data() + position - channels);
                    std::uint32_t upper_left = load_pixel<channels>(above + column - channels);
                    std::uint32_t delta = subtract_bytes(left, upper_left);
                    for (std::size_t offset = 0; offset < count; offset += channels) {
                        std::uint32_t upper = load_pixel<channels>(above + column + offset);
                        std::uint32_t value = add_bytes(upper, delta);
                        store_pixel<channels>(output.data() + position + offset, value);
                    }
                }
                position += count;
                column += count;
                if (column == row_bytes) { column = 0; }
                remaining_prediction -= count;
            }
            continue;
        }
        std::size_t copied = 0;
        // Grow from the already reconstructed prefix. Each memcpy is disjoint,
        // including distance-one repeats, while the available prefix doubles.
        while (copied < length) {
            std::size_t count = std::min(length - copied, distance + copied);
            std::memcpy(output.data() + position + copied,
                        output.data() + position - distance, count);
            copied += count;
        }
        position += length;
        column += length;
        if (column >= row_bytes) { column %= row_bytes; }
    }
}
}

void restore_prediction_direct_words(std::span<const Sequence> sequences,
             std::span<const std::uint8_t> literals,
             std::span<const std::uint8_t> previous,
             std::size_t channels,
             std::span<std::uint8_t> output) {
    if (channels != 4) {
        restore_prediction(sequences, literals, previous, channels, output);
        return;
    }
    if ((channels != 3 && channels != 4) || output.empty()
        || output.size() > band_capacity || previous.empty()
        || previous.size() > output.size() || previous.size() % channels != 0
        || output.size() % previous.size() != 0 || literals.size() % channels != 0
        || literals.size() > output.size() || sequences.empty()
        || sequences.size() > output.size() / channels / 4 + 1) {
        throw std::invalid_argument("restore buffer contract");
    }
    if (overlaps_output(sequences.data(), sequences.size_bytes(), output)
        || overlaps_output(literals.data(), literals.size(), output)
        || overlaps_output(previous.data(), previous.size(), output)) {
        throw std::invalid_argument("restore storage overlap");
    }
    std::size_t pixels = output.size() / channels;
    std::size_t literal_count = literals.size() / channels;
    std::size_t position = 0;
    std::size_t consumed = 0;
    for (std::size_t index = 0; index < sequences.size(); ++index) {
        const Sequence& sequence = sequences[index];
        if (sequence.literals > pixels - position || sequence.literals > literal_count - consumed) {
            throw std::invalid_argument("restore literal extent");
        }
        position += sequence.literals;
        consumed += sequence.literals;
        if (sequence.length == 0) {
            if (sequence.distance != 0 || index != sequences.size() - 1) {
                throw std::invalid_argument("restore terminal sequence");
            }
            continue;
        }
        if (sequence.length < 4 || sequence.length > pixels - position
            || sequence.distance > position
            || sequence.distance > 262144 || index == sequences.size() - 1) {
            throw std::invalid_argument("restore match extent");
        }
        position += sequence.length;
    }
    if (position != pixels || consumed != literal_count) {
        throw std::invalid_argument("restore exact consumption");
    }
    if (channels == 3) {
        restore_kernel<3>(sequences, literals, previous, output);
    } else {
        restore_kernel<4>(sequences, literals, previous, output);
    }
}
}
