#include "prediction.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace stx_cng_detail {
namespace {
bool overlap(const void* first, std::size_t first_size,
             const void* second, std::size_t second_size) {
    std::uintptr_t first_begin = reinterpret_cast<std::uintptr_t>(first);
    std::uintptr_t second_begin = reinterpret_cast<std::uintptr_t>(second);
    if (first_begin <= second_begin) {
        return second_begin - first_begin < first_size;
    }
    return first_begin - second_begin < second_size;
}

template<std::size_t channels>
std::uint32_t pixel(std::span<const std::uint8_t> source, std::size_t position) {
    std::size_t offset = position * channels;
    std::uint32_t value = source[offset];
    value |= static_cast<std::uint32_t>(source[offset + 1]) << 8;
    value |= static_cast<std::uint32_t>(source[offset + 2]) << 16;
    if constexpr (channels == 4) {
        value |= static_cast<std::uint32_t>(source[offset + 3]) << 24;
    }
    return value;
}

// A zero mixed difference means the vertical difference stays constant along
// the row. Row boundaries reset that difference to zero. A rejected short run
// costs at most three repeated comparisons; accepted scans are skipped by the
// chosen prediction or longer copy, so prediction lookahead is linear overall.
template<std::size_t channels>
std::size_t prediction_length(std::span<const std::uint8_t> source,
                              std::span<const std::uint8_t> previous,
                              std::size_t position) {
    std::size_t row_bytes = previous.size();
    std::size_t begin = position * channels;
    std::size_t offset = begin;
    while (offset < source.size()) {
        std::size_t column = offset % row_bytes;
        std::size_t row_begin = offset - column;
        const std::uint8_t* above = row_begin == 0
            ? previous.data() : source.data() + row_begin - row_bytes;
        std::array<std::uint8_t, channels> difference{};
        if (column != 0) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                difference[channel] = static_cast<std::uint8_t>(source[offset - channels + channel] - above[column - channels + channel]);
            }
        }
        std::size_t end = row_begin + row_bytes;
        while (offset < end) {
            bool equal = true;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                std::uint8_t current = static_cast<std::uint8_t>(source[offset + channel] - above[column + channel]);
                equal = equal && current == difference[channel];
            }
            if (!equal) {
                std::size_t length = (offset - begin) / channels;
                return length;
            }
            offset += channels;
            column += channels;
        }
    }
    std::size_t length = (offset - begin) / channels;
    return length;
}

template<std::size_t channels>
Result parse_kernel(std::span<const std::uint8_t> source,
                    std::span<const std::uint8_t> previous_row,
                    std::span<std::int32_t> index,
                    std::span<Sequence> output) {
    std::size_t count = source.size() / channels;
    std::fill_n(index.data(), index_capacity, -1);
    Result result{};
    std::size_t position = 0;
    std::size_t literal_begin = 0;
    while (position + 3 < count) {
        std::uint32_t first = pixel<channels>(source, position);
        std::uint32_t second = pixel<channels>(source, position + 1);
        std::uint64_t mixed = static_cast<std::uint64_t>(first) * 0x9e3779b1;
        mixed ^= static_cast<std::uint64_t>(second) * 0x85ebca77;
        std::size_t slot = (mixed ^ (mixed >> 16) ^ (mixed >> 32) ^ (mixed >> 48)) & 65535;
        std::int32_t previous_value = index[slot];
        index[slot] = static_cast<std::int32_t>(position);
        std::size_t predicted = prediction_length<channels>(source, previous_row, position);
        std::size_t length = 0;
        std::size_t distance = 0;
        if (previous_value >= 0 && predicted < count - position) {
            std::size_t previous = static_cast<std::size_t>(previous_value);
            distance = position - previous;
            bool candidate = distance <= 262144
                && pixel<channels>(source, previous) == first
                && pixel<channels>(source, previous + 1) == second
                && pixel<channels>(source, previous + 2) == pixel<channels>(source, position + 2)
                && pixel<channels>(source, previous + 3) == pixel<channels>(source, position + 3);
            if (candidate) {
                length = 4;
                while (position + length < count) {
                    std::size_t batch = std::min<std::size_t>(256, count - position - length);
                    const std::uint8_t* first_bytes = source.data() + (previous + length) * channels;
                    const std::uint8_t* second_bytes = source.data() + (position + length) * channels;
                    int difference = std::memcmp(first_bytes, second_bytes, batch * channels);
                    if (difference == 0) {
                        length += batch;
                        continue;
                    }
                    while (pixel<channels>(source, previous + length) == pixel<channels>(source, position + length)) {
                        ++length;
                    }
                    break;
                }
            }
        }
        if (predicted >= 4 && predicted >= length) {
            length = predicted;
            distance = 0;
        }
        if (length < 4) {
            ++position;
            continue;
        }
        std::size_t literals = position - literal_begin;
        output[result.sequences] = Sequence{
            .literals = static_cast<std::uint32_t>(literals),
            .length = static_cast<std::uint32_t>(length),
            .distance = static_cast<std::uint32_t>(distance)};
        ++result.sequences;
        result.literals += literals;
        result.matched += length;
        position += length;
        literal_begin = position;
    }
    std::size_t tail = count - literal_begin;
    output[result.sequences] = Sequence{.literals = static_cast<std::uint32_t>(tail)};
    ++result.sequences;
    result.literals += tail;
    return result;
}
}

Result parse_prediction(std::span<const std::uint8_t> source,
                        std::span<const std::uint8_t> previous, std::size_t channels,
             std::span<std::int32_t> index, std::span<Sequence> output) {
    if ((channels != 3 && channels != 4) || source.empty()
        || source.size() > band_capacity || previous.empty() || previous.size() > source.size()
        || previous.size() % channels != 0 || source.size() % previous.size() != 0
        || index.size() < index_capacity || output.size() < source.size() / channels / 4 + 1) {
        throw std::invalid_argument("source match buffer contract");
    }
    std::size_t index_bytes = index_capacity * sizeof(std::int32_t);
    std::size_t output_bytes = (source.size() / channels / 4 + 1) * sizeof(Sequence);
    if (overlap(source.data(), source.size(), index.data(), index_bytes)
        || overlap(source.data(), source.size(), output.data(), output_bytes)
        || overlap(index.data(), index_bytes, output.data(), output_bytes)
        || overlap(previous.data(), previous.size(), index.data(), index_bytes)
        || overlap(previous.data(), previous.size(), output.data(), output_bytes)) {
        throw std::invalid_argument("source match storage overlap");
    }
    Result result{};
    if (source.size() / channels < 4) {
        result.sequences = 1;
        result.literals = source.size() / channels;
        output[0] = Sequence{.literals = static_cast<std::uint32_t>(result.literals)};
        return result;
    }
    if (channels == 3) {
        result = parse_kernel<3>(source, previous, index, output);
    } else {
        result = parse_kernel<4>(source, previous, index, output);
    }
    return result;
}
}
