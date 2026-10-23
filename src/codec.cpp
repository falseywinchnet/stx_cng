#include "stx_cng/codec.hpp"
#include "context.hpp"
#include "direct_words.hpp"
#include "raw.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zlib.h>

namespace stx_cng {
namespace {
using Clock = std::chrono::steady_clock;
using Sequence = stx_cng_detail::Sequence;
constexpr std::size_t budget = 1048576;
constexpr std::array<std::uint8_t, 8> signature{137, 67, 83, 80, 13, 10, 26, 10};
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::size_t validate(Dimensions dimensions) {
    require(dimensions.height > 0 && dimensions.width > 0 && dimensions.height <= 65536 && dimensions.width <= 65536, "image dimensions");
    require(dimensions.channels == 3 || dimensions.channels == 4, "image channels");
    std::size_t ph = ((dimensions.height + 127) / 128) * 128;
    std::size_t pw = ((dimensions.width + 127) / 128) * 128;
    require(ph <= 4194304 / pw && ph + pw <= 65536, "canvas bound");
    std::size_t rows = (budget - dimensions.channels) / (dimensions.width * dimensions.channels);
    require(rows > 0, "band row bound");
    return rows;
}
// Track every Zstandard allocation, including allocation headers. Context is
// destroyed before its accounting record. No shared or process-global counters.
struct AllocationCount {
    std::size_t live = 0;
    std::size_t peak = 0;
};
struct alignas(std::max_align_t) AllocationHeader {
    std::size_t bytes;
};
void* allocate_context(void* opaque, std::size_t size) {
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader)) { return nullptr; }
    std::size_t total = size + sizeof(AllocationHeader);
    void* memory = std::malloc(total);
    if (memory == nullptr) { return nullptr; }
    AllocationHeader* header = static_cast<AllocationHeader*>(memory);
    (*header).bytes = total;
    AllocationCount& count = *static_cast<AllocationCount*>(opaque);
    count.live += total;
    count.peak = std::max(count.peak, count.live);
    void* result = header + 1;
    return result;
}
void release_context(void* opaque, void* memory) {
    if (memory == nullptr) { return; }
    AllocationHeader* header = static_cast<AllocationHeader*>(memory) - 1;
    AllocationCount& count = *static_cast<AllocationCount*>(opaque);
    count.live -= (*header).bytes;
    std::free(header);
}
using CompressionContext = stx_cng_detail::CompressionContext;
struct DecompressionContext {
    AllocationCount count;
    ZSTD_DCtx* context;
    DecompressionContext() : count{}, context(nullptr) {
        ZSTD_customMem memory{allocate_context, release_context, &count};
        context = ZSTD_createDCtx_advanced(memory);
        require(context != nullptr, "decompression context");
    }
    ~DecompressionContext() { ZSTD_freeDCtx(context); }
    DecompressionContext(const DecompressionContext&) = delete;
    DecompressionContext& operator=(const DecompressionContext&) = delete;
};

int wrap(int value) {
    unsigned reduced = static_cast<unsigned>(value + 128) & 255U;
    int result = static_cast<int>(reduced) - 128;
    return result;
}
// C++20 signed right shift is floor division, matching the Python oracle.
void forward_colour(std::array<int, 4>& values) {
    int difference = wrap(values[0] - values[1]);
    int second = wrap(values[2] - values[1] - (difference >> 1));
    int centre = wrap(values[1] + (difference >> 1));
    // Add a multiple of three so C++ truncation implements floor division
    // for the bounded signed numerator second + parity + 1 in [-127,129].
    int third = (second + (difference & 1) + 130) / 3 - 43;
    int brightness = wrap(centre + third);
    values[0] = brightness;
    values[1] = difference;
    values[2] = second;
}
void inverse_colour(std::array<int, 4>& values) {
    int third = (values[2] + (values[1] & 1) + 130) / 3 - 43;
    int centre = wrap(values[0] - third);
    int green = wrap(centre - (values[1] >> 1));
    int red = wrap(green + values[1]);
    int blue = wrap(green + values[2] + (values[1] >> 1));
    values[0] = red;
    values[1] = green;
    values[2] = blue;
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
    require(packed.size() <= budget && separated.size() >= 2 * packed.size() + 4, "run separation capacity");
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

void write_u32(std::span<std::uint8_t> bytes, std::size_t offset, std::size_t value) {
    for (unsigned index = 0; index < 4; ++index) { bytes[offset + index] = static_cast<std::uint8_t>(value >> (24 - 8 * index)); }
}
std::size_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::size_t result = 0;
    for (unsigned index = 0; index < 4; ++index) { result = (result << 8) | bytes[offset + index]; }
    return result;
}
void read_exact(std::istream& source, std::span<std::uint8_t> bytes) {
    if (bytes.empty()) { return; }
    source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(source), "stream read");
}
void write_exact(std::ostream& destination, std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) { return; }
    destination.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(destination), "stream write");
}
void write_chunk(std::ostream& destination, const std::array<std::uint8_t, 4>& kind,
                 std::span<const std::uint8_t> payload, int coding = -1) {
    std::array<std::uint8_t, 8> header{};
    std::size_t size = payload.size();
    if (coding >= 0) { ++size; }
    write_u32(header, 0, size);
    std::copy(kind.begin(), kind.end(), header.begin() + 4);
    write_exact(destination, header);
    uLong checksum = crc32(0, kind.data(), 4);
    if (coding >= 0) {
        std::array<std::uint8_t, 1> tag{static_cast<std::uint8_t>(coding)};
        checksum = crc32(checksum, tag.data(), 1);
        write_exact(destination, tag);
    }
    if (!payload.empty()) { checksum = crc32(checksum, payload.data(), static_cast<uInt>(payload.size())); }
    write_exact(destination, payload);
    std::array<std::uint8_t, 4> footer{};
    write_u32(footer, 0, checksum);
    write_exact(destination, footer);
}
// Returns a length into caller-owned storage, not a retained borrow.
std::size_t read_chunk(std::istream& source, const std::array<std::uint8_t, 4>& kind, std::span<std::uint8_t> payload) {
    std::array<std::uint8_t, 8> header{};
    read_exact(source, header);
    require(std::equal(kind.begin(), kind.end(), header.begin() + 4), "chunk kind");
    std::size_t size = read_u32(header, 0);
    require(size <= payload.size(), "chunk bound");
    read_exact(source, payload.first(size));
    uLong checksum = crc32(0, kind.data(), 4);
    if (size != 0) { checksum = crc32(checksum, payload.data(), static_cast<uInt>(size)); }
    std::array<std::uint8_t, 4> footer{};
    read_exact(source, footer);
    require(checksum == read_u32(footer, 0), "chunk checksum");
    return size;
}
void finish_input(std::istream& source) {
    require(source.peek() == std::char_traits<char>::eof() && !source.bad(), "trailing stream data");
}

void pack_sequences(std::span<const Sequence> sequences, std::span<std::uint8_t> raw) {
    std::size_t count = sequences.size();
    for (std::size_t index = 0; index < count; ++index) {
        std::array<std::uint32_t, 3> words{sequences[index].literals, sequences[index].length, sequences[index].distance};
        for (std::size_t field = 0; field < 3; ++field) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                raw[(field * 4 + byte) * count + index] = static_cast<std::uint8_t>(words[field] >> (8 * byte));
            }
        }
    }
}

void unpack_sequences(std::span<const std::uint8_t> raw, std::span<Sequence> sequences) {
    std::size_t count = sequences.size();
    for (std::size_t index = 0; index < count; ++index) {
        std::array<std::uint32_t, 3> words{};
        for (std::size_t field = 0; field < 3; ++field) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                words[field] |= static_cast<std::uint32_t>(raw[(field * 4 + byte) * count + index]) << (8 * byte);
            }
        }
        sequences[index] = Sequence{words[0], words[1], words[2]};
    }
}

template<std::size_t channels>
void collect_literals(std::span<const std::uint8_t> source,
                      std::span<const std::uint8_t> previous,
                      std::span<const Sequence> sequences,
                      std::span<std::uint8_t> literals,
                      std::span<std::uint8_t> packed) {
    std::fill(packed.begin(), packed.end(), 0);
    std::size_t groups = (literals.size() / channels + 1) / 2;
    std::size_t position = 0;
    std::size_t written = 0;
    std::size_t stride = previous.size();
    for (const Sequence& sequence : sequences) {
        std::size_t remaining = static_cast<std::size_t>(sequence.literals) * channels;
        while (remaining != 0) {
            std::size_t column = position % stride;
            std::size_t count = std::min(remaining, stride - column);
            const std::uint8_t* above = position < stride
                ? previous.data() : source.data() + position - column - stride;
            std::array<int, channels> left_difference{};
            if (column != 0) {
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    left_difference[channel] = source[position - channels + channel] - above[column - channels + channel];
                }
            }
            for (std::size_t offset = 0; offset < count; offset += channels) {
                std::array<int, 4> values{};
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    int difference = source[position + offset + channel] - above[column + offset + channel];
                    int error = difference - left_difference[channel];
                    left_difference[channel] = difference;
                    literals[written * channels + channel] = source[position + offset + channel];
                    values[channel] = wrap(error);
                }
                forward_colour(values);
                unsigned shift = static_cast<unsigned>((written & 1U) * 4);
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    int value = values[channel];
                    unsigned mapped = static_cast<unsigned>(value < 0 ? -2 * value - 1 : 2 * value);
                    packed[(channel * 2) * groups + written / 2] |= static_cast<std::uint8_t>((mapped & 15U) << shift);
                    packed[(channel * 2 + 1) * groups + written / 2] |= static_cast<std::uint8_t>((mapped >> 4) << shift);
                }
                ++written;
            }
            position += count;
            remaining -= count;
        }
        position += static_cast<std::size_t>(sequence.length) * channels;
    }
    require(written * channels == literals.size() && position == source.size(), "literal collection extent");
}

template<std::size_t channels>
void unpack_literals(std::span<const std::uint8_t> packed, std::span<std::uint8_t> literals) {
    std::size_t count = literals.size() / channels;
    std::size_t groups = (count + 1) / 2;
    for (std::size_t position = 0; position < count; ++position) {
        unsigned shift = static_cast<unsigned>((position & 1U) * 4);
        std::array<int, 4> values{};
        for (std::size_t channel = 0; channel < channels; ++channel) {
            unsigned low = (packed[(channel * 2) * groups + position / 2] >> shift) & 15U;
            unsigned high = (packed[(channel * 2 + 1) * groups + position / 2] >> shift) & 15U;
            unsigned mapped = low | (high << 4);
            int magnitude = static_cast<int>(mapped / 2);
            values[channel] = (mapped & 1U) != 0 ? -magnitude - 1 : magnitude;
        }
        inverse_colour(values);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            literals[position * channels + channel] = static_cast<std::uint8_t>(values[channel]);
        }
    }
}
}

void encode(std::istream& source, std::ostream& destination, Dimensions dimensions, Measurements& measurements) {
    measurements = {};
    std::size_t rows = validate(dimensions);
    std::size_t stride = dimensions.width * dimensions.channels;
    std::size_t sample_capacity = std::min(rows, dimensions.height) * stride;
    std::size_t sequence_capacity = sample_capacity / dimensions.channels / 4 + 1;
    std::size_t raw_sequence_capacity = sequence_capacity * 12;
    std::size_t packed_capacity = ((sample_capacity / dimensions.channels + 1) / 2) * 2 * dimensions.channels;
    std::size_t separated_capacity = 2 * packed_capacity + 4;
    std::size_t work_capacity = std::max(raw_sequence_capacity, ZSTD_compressBound(separated_capacity));
    std::size_t payload_capacity = 13 + ZSTD_compressBound(raw_sequence_capacity) + sample_capacity;
    std::unique_ptr<std::uint8_t[]> sample_owner = std::make_unique_for_overwrite<std::uint8_t[]>(sample_capacity);
    std::unique_ptr<std::uint8_t[]> previous_owner = std::make_unique_for_overwrite<std::uint8_t[]>(stride);
    std::unique_ptr<std::uint8_t[]> literal_owner = std::make_unique_for_overwrite<std::uint8_t[]>(sample_capacity);
    std::unique_ptr<std::uint8_t[]> work_owner = std::make_unique_for_overwrite<std::uint8_t[]>(work_capacity);
    std::unique_ptr<std::uint8_t[]> separated_owner = std::make_unique_for_overwrite<std::uint8_t[]>(separated_capacity);
    std::unique_ptr<std::uint8_t[]> payload_owner = std::make_unique_for_overwrite<std::uint8_t[]>(payload_capacity);
    std::unique_ptr<Sequence[]> sequence_owner = std::make_unique_for_overwrite<Sequence[]>(sequence_capacity);
    std::unique_ptr<std::int32_t[]> index_owner = std::make_unique_for_overwrite<std::int32_t[]>(stx_cng_detail::index_capacity);
    std::span<std::uint8_t> samples(sample_owner.get(), sample_capacity);
    std::span<std::uint8_t> previous(previous_owner.get(), stride);
    std::span<std::uint8_t> literals(literal_owner.get(), sample_capacity);
    std::span<std::uint8_t> work(work_owner.get(), work_capacity);
    std::span<std::uint8_t> separated(separated_owner.get(), separated_capacity);
    std::span<std::uint8_t> payload(payload_owner.get(), payload_capacity);
    std::span<Sequence> sequences(sequence_owner.get(), sequence_capacity);
    std::span<std::int32_t> index(index_owner.get(), stx_cng_detail::index_capacity);
    std::fill(previous.begin(), previous.end(), 0);
    CompressionContext context;
    write_exact(destination, signature);
    std::array<std::uint8_t, 16> header{};
    header[0] = 2;
    header[1] = static_cast<std::uint8_t>(dimensions.channels);
    write_u32(header, 4, dimensions.width);
    write_u32(header, 8, dimensions.height);
    write_u32(header, 12, budget);
    write_chunk(destination, {67, 72, 68, 82}, header);
    for (std::size_t y = 0; y < dimensions.height; y += rows) {
        std::size_t height = std::min(rows, dimensions.height - y);
        std::span<std::uint8_t> active = samples.first(height * stride);
        read_exact(source, active);
        Clock::time_point start = Clock::now();
        stx_cng_detail::Result parsed = stx_cng_detail::parse_prediction(active, previous, dimensions.channels, index, sequences);
        std::span<const Sequence> live = sequences.first(parsed.sequences);
        std::size_t raw_size = parsed.sequences * 12;
        pack_sequences(live, work.first(raw_size));
        Clock::time_point transformed = Clock::now();
        std::size_t sequence_size = ZSTD_compressCCtx(context.context, payload.data() + 12, payload.size() - 12, work.data(), raw_size, 3);
        require(ZSTD_isError(sequence_size) == 0, "sequence compression");
        Clock::time_point sequence_end = Clock::now();
        std::span<std::uint8_t> selected = literals.first(parsed.literals * dimensions.channels);
        std::size_t packed_size = ((parsed.literals + 1) / 2) * 2 * dimensions.channels;
        std::span<std::uint8_t> packed = work.first(packed_size);
        if (dimensions.channels == 3) { collect_literals<3>(active, previous, live, selected, packed); }
        else { collect_literals<4>(active, previous, live, selected, packed); }
        std::size_t separated_size = separate_runs(packed, separated.first(2 * packed_size + 4));
        Clock::time_point packed_at = Clock::now();
        std::size_t literal_size = 0;
        if (!selected.empty()) {
            literal_size = ZSTD_compressCCtx(context.context, work.data(), work.size(), separated.data(), separated_size, 3);
            require(ZSTD_isError(literal_size) == 0, "literal compression");
        }
        Clock::time_point compressed_at = Clock::now();
        std::size_t literal_offset = 12 + sequence_size;
        if (selected.empty() || literal_size >= selected.size()) {
            payload[literal_offset] = 0;
            std::copy(selected.begin(), selected.end(), payload.begin() + literal_offset + 1);
            literal_size = selected.size();
            ++measurements.stored_bands;
        } else {
            payload[literal_offset] = 1;
            std::copy_n(work.data(), literal_size, payload.data() + literal_offset + 1);
            ++measurements.compressed_bands;
        }
        write_u32(payload, 0, parsed.sequences);
        write_u32(payload, 4, parsed.literals);
        write_u32(payload, 8, sequence_size);
        write_chunk(destination, {66, 65, 78, 68}, payload.first(literal_offset + 1 + literal_size));
        std::copy_n(active.data() + active.size() - stride, stride, previous.data());
        measurements.transform_seconds += std::chrono::duration<double>(transformed - start).count();
        measurements.transform_seconds += std::chrono::duration<double>(packed_at - sequence_end).count();
        measurements.entropy_seconds += std::chrono::duration<double>(sequence_end - transformed).count();
        measurements.entropy_seconds += std::chrono::duration<double>(compressed_at - packed_at).count();
    }
    finish_input(source);
    write_chunk(destination, {67, 69, 78, 68}, {});
    measurements.workspace_bytes = 2 * sample_capacity + stride + work_capacity + separated_capacity + payload_capacity;
    measurements.workspace_bytes += sequence_capacity * sizeof(Sequence) + index.size_bytes() + context.workspace_bytes;
}

namespace {
template<bool supplied_output>
Dimensions decode_body(std::istream& source, std::ostream* destination,
                       std::span<std::uint8_t> output, Measurements& measurements) {
    measurements = {};
    std::array<std::uint8_t, 8> identifier{};
    read_exact(source, identifier);
    require(identifier == signature, "CSP signature");
    std::array<std::uint8_t, 16> header{};
    require(read_chunk(source, {67, 72, 68, 82}, header) == header.size(), "CSP header extent");
    require(header[0] == 2 && header[2] == 0 && header[3] == 0 && read_u32(header, 12) == budget, "CSP header fields");
    Dimensions dimensions{read_u32(header, 8), read_u32(header, 4), header[1]};
    std::size_t rows = validate(dimensions);
    std::size_t stride = dimensions.width * dimensions.channels;
    if constexpr (supplied_output) {
        require(output.size() == dimensions.height * stride, "destination extent");
    }
    std::size_t sample_capacity = std::min(rows, dimensions.height) * stride;
    std::size_t sequence_capacity = sample_capacity / dimensions.channels / 4 + 1;
    std::size_t raw_sequence_capacity = sequence_capacity * 12;
    std::size_t packed_capacity = ((sample_capacity / dimensions.channels + 1) / 2) * 2 * dimensions.channels;
    std::size_t work_capacity = std::max(raw_sequence_capacity, packed_capacity);
    std::size_t separated_capacity = 2 * packed_capacity + 4;
    std::size_t payload_capacity = 13 + ZSTD_compressBound(raw_sequence_capacity) + sample_capacity;
    std::unique_ptr<std::uint8_t[]> sample_owner;
    if constexpr (!supplied_output) {
        sample_owner = std::make_unique_for_overwrite<std::uint8_t[]>(sample_capacity);
    }
    std::unique_ptr<std::uint8_t[]> previous_owner = std::make_unique_for_overwrite<std::uint8_t[]>(stride);
    std::unique_ptr<std::uint8_t[]> literal_owner = std::make_unique_for_overwrite<std::uint8_t[]>(sample_capacity);
    std::unique_ptr<std::uint8_t[]> work_owner = std::make_unique_for_overwrite<std::uint8_t[]>(work_capacity);
    std::unique_ptr<std::uint8_t[]> separated_owner = std::make_unique_for_overwrite<std::uint8_t[]>(separated_capacity);
    std::unique_ptr<std::uint8_t[]> payload_owner = std::make_unique_for_overwrite<std::uint8_t[]>(payload_capacity);
    std::unique_ptr<Sequence[]> sequence_owner = std::make_unique_for_overwrite<Sequence[]>(sequence_capacity);
    std::span<std::uint8_t> previous(previous_owner.get(), stride);
    std::span<std::uint8_t> literals(literal_owner.get(), sample_capacity);
    std::span<std::uint8_t> work(work_owner.get(), work_capacity);
    std::span<std::uint8_t> separated(separated_owner.get(), separated_capacity);
    std::span<std::uint8_t> payload(payload_owner.get(), payload_capacity);
    std::span<Sequence> sequences(sequence_owner.get(), sequence_capacity);
    std::fill(previous.begin(), previous.end(), 0);
    DecompressionContext context;
    for (std::size_t y = 0; y < dimensions.height; y += rows) {
        std::size_t height = std::min(rows, dimensions.height - y);
        std::size_t pixels = height * dimensions.width;
        std::span<std::uint8_t> active;
        if constexpr (supplied_output) { active = output.subspan(y * stride, height * stride); }
        else { active = std::span<std::uint8_t>(sample_owner.get(), height * stride); }
        std::size_t payload_size = read_chunk(source, {66, 65, 78, 68}, payload);
        require(payload_size >= 13, "CSP band header");
        std::size_t count = read_u32(payload, 0);
        std::size_t literal_count = read_u32(payload, 4);
        std::size_t sequence_size = read_u32(payload, 8);
        require(count >= 1 && count <= pixels / 4 + 1 && literal_count <= pixels
            && sequence_size <= payload_size - 13, "CSP band counts");
        Clock::time_point start = Clock::now();
        std::size_t size = ZSTD_decompressDCtx(context.context, work.data(), count * 12, payload.data() + 12, sequence_size);
        require(ZSTD_isError(size) == 0 && size == count * 12, "CSP sequence frame");
        Clock::time_point sequence_at = Clock::now();
        unpack_sequences(work.first(size), sequences.first(count));
        std::size_t literal_offset = 12 + sequence_size;
        std::size_t literal_size = payload_size - literal_offset - 1;
        std::span<std::uint8_t> selected = literals.first(literal_count * dimensions.channels);
        Clock::time_point reordered = Clock::now();
        Clock::time_point entropy_at = reordered;
        if (payload[literal_offset] == 0) {
            require(literal_size == selected.size(), "CSP stored literals");
            ++measurements.stored_bands;
        } else {
            require(payload[literal_offset] == 1, "CSP literal coding");
            std::size_t packed_size = ((literal_count + 1) / 2) * 2 * dimensions.channels;
            std::size_t separated_size = ZSTD_decompressDCtx(context.context, separated.data(), 2 * packed_size + 4,
                payload.data() + literal_offset + 1, literal_size);
            require(ZSTD_isError(separated_size) == 0, "CSP literal frame");
            entropy_at = Clock::now();
            restore_runs(separated.first(separated_size), work.first(packed_size));
            if (dimensions.channels == 3) { unpack_literals<3>(work.first(packed_size), selected); }
            else { unpack_literals<4>(work.first(packed_size), selected); }
            ++measurements.compressed_bands;
        }
        Clock::time_point literals_at = Clock::now();
        if (payload[literal_offset] == 0) {
            stx_cng_detail::restore_raw(sequences.first(count), payload.subspan(literal_offset + 1, literal_size), previous, dimensions.channels, active);
        } else {
            stx_cng_detail::restore_prediction_direct_words(sequences.first(count), selected, previous, dimensions.channels, active);
        }
        Clock::time_point reconstructed = Clock::now();
        measurements.sequence_seconds += std::chrono::duration<double>(reordered - sequence_at).count();
        measurements.literal_seconds += std::chrono::duration<double>(literals_at - entropy_at).count();
        measurements.reconstruction_seconds += std::chrono::duration<double>(reconstructed - literals_at).count();
        std::copy_n(active.data() + active.size() - stride, stride, previous.data());
        if constexpr (!supplied_output) { write_exact(*destination, active); }
        measurements.entropy_seconds += std::chrono::duration<double>(sequence_at - start).count();
        measurements.entropy_seconds += std::chrono::duration<double>(entropy_at - reordered).count();
        measurements.transform_seconds += std::chrono::duration<double>(reordered - sequence_at).count();
        measurements.transform_seconds += std::chrono::duration<double>(reconstructed - entropy_at).count();
    }
    require(read_chunk(source, {67, 69, 78, 68}, payload) == 0, "CSP end chunk");
    finish_input(source);
    measurements.workspace_bytes = sample_capacity + stride + work_capacity + separated_capacity + payload_capacity;
    measurements.workspace_bytes += sequence_capacity * sizeof(Sequence) + context.count.peak;
    if constexpr (!supplied_output) { measurements.workspace_bytes += sample_capacity; }
    return dimensions;
}
}

Dimensions decode(std::istream& source, std::ostream& destination, Measurements& measurements) {
    Dimensions result = decode_body<false>(source, &destination, {}, measurements);
    return result;
}

Dimensions decode_into(std::istream& source, std::span<std::uint8_t> destination, Measurements& measurements) {
    Dimensions result = decode_body<true>(source, nullptr, destination, measurements);
    return result;
}
}
