#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Adaptive binary range coder of the phase codec (docs/PHASE_NATIVE_PORT.md, section 2).
// LZMA-style 32-bit range with carry propagation through a cache byte. Probabilities are
// sixteen-bit estimates of a zero bit with a count-based adaptation rate. Bit for bit, this
// mirrors research/phase_codec.py: any change here changes the stream.
namespace cng_phase {

constexpr std::uint32_t kProbabilityBits = 16;
constexpr std::int32_t kProbabilityOne = 1 << kProbabilityBits;
constexpr std::int32_t kProbabilityFloor = 64;
constexpr std::int32_t kProbabilityCeiling = kProbabilityOne - kProbabilityFloor;
constexpr std::uint32_t kAdaptationCap = 30;
constexpr std::uint32_t kRenormalizeBelow = 1u << 24;

// One adaptive probability: estimate of bit zero in Q16 and the number of observed bits.
struct BitModel {
    std::int32_t zero_probability = kProbabilityOne >> 1;
    std::uint32_t observed = 0;
};

// Reciprocal rate table: rate(n) = floor(65536 / (n + 2)), n capped at kAdaptationCap.
struct AdaptationRates {
    std::int32_t reciprocal[kAdaptationCap + 1] = {};
    constexpr AdaptationRates() {
        for (std::uint32_t count = 0; count <= kAdaptationCap; ++count) {
            reciprocal[count] = static_cast<std::int32_t>(65536u / (count + 2));
        }
    }
};
inline constexpr AdaptationRates kRates{};

inline void update_model(BitModel& model, std::uint32_t bit) {
    std::int64_t target = (bit == 0) ? static_cast<std::int64_t>(kProbabilityOne) : 0;
    std::int64_t difference = target - model.zero_probability;
    // Arithmetic shift of a signed product: floor division by 65536, as the reference does.
    std::int64_t step = (difference * kRates.reciprocal[model.observed]) >> 16;
    std::int32_t updated = model.zero_probability + static_cast<std::int32_t>(step);
    if (updated < kProbabilityFloor) { updated = kProbabilityFloor; }
    if (updated > kProbabilityCeiling) { updated = kProbabilityCeiling; }
    model.zero_probability = updated;
    if (model.observed < kAdaptationCap) { model.observed += 1; }
}

// Encoder appends to a caller-owned growable byte vector; finish() flushes five bytes.
class RangeEncoder {
public:
    explicit RangeEncoder(std::vector<std::uint8_t>& destination) : output(destination) {}
    RangeEncoder(const RangeEncoder&) = delete;
    RangeEncoder& operator=(const RangeEncoder&) = delete;

    void encode_bit(BitModel& model, std::uint32_t bit) {
        std::uint64_t bound = static_cast<std::uint64_t>(range >> kProbabilityBits)
                              * static_cast<std::uint64_t>(model.zero_probability);
        if (bit == 0) {
            range = static_cast<std::uint32_t>(bound);
        } else {
            low += bound;
            range -= static_cast<std::uint32_t>(bound);
        }
        update_model(model, bit);
        normalize();
    }
    void encode_bypass(std::uint32_t bit) {
        range >>= 1;
        if (bit != 0) { low += range; }
        normalize();
    }
    void finish() {
        for (std::size_t index = 0; index < 5; ++index) { shift_low(); }
    }

private:
    void normalize() {
        while (range < kRenormalizeBelow) {
            range <<= 8;
            shift_low();
        }
    }
    void shift_low() {
        if (low < 0xFF000000ull || low >= (1ull << 32)) {
            std::uint8_t carry = static_cast<std::uint8_t>(low >> 32);
            std::uint32_t pending = cache;
            while (true) {
                output.push_back(static_cast<std::uint8_t>((pending + carry) & 0xFFu));
                pending = 0xFFu;
                cache_size -= 1;
                if (cache_size == 0) { break; }
            }
            cache = static_cast<std::uint32_t>((low >> 24) & 0xFFu);
        }
        cache_size += 1;
        low = (low & 0x00FFFFFFull) << 8;
    }

    std::vector<std::uint8_t>& output;
    std::uint64_t low = 0;
    std::uint32_t range = 0xFFFFFFFFu;
    std::uint32_t cache = 0;
    std::uint64_t cache_size = 1;
};

// Decoder reads a borrowed byte span; bytes past the end read as zero, as the reference does.
class RangeDecoder {
public:
    explicit RangeDecoder(std::span<const std::uint8_t> source) : input(source) {
        for (std::size_t index = 0; index < 5; ++index) { code = (code << 8) | next_byte(); }
    }
    RangeDecoder(const RangeDecoder&) = delete;
    RangeDecoder& operator=(const RangeDecoder&) = delete;

    [[nodiscard]] std::uint32_t decode_bit(BitModel& model) {
        std::uint32_t bound = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(range >> kProbabilityBits)
            * static_cast<std::uint64_t>(model.zero_probability));
        std::uint32_t bit = 0;
        if (code < bound) {
            range = bound;
        } else {
            code -= bound;
            range -= bound;
            bit = 1;
        }
        update_model(model, bit);
        normalize();
        return bit;
    }
    [[nodiscard]] std::uint32_t decode_bypass() {
        range >>= 1;
        std::uint32_t bit = 0;
        if (code >= range) {
            code -= range;
            bit = 1;
        }
        normalize();
        return bit;
    }
    [[nodiscard]] std::size_t consumed() const { return position; }

private:
    std::uint32_t next_byte() {
        std::uint32_t byte = 0;
        if (position < input.size()) { byte = input[position]; }
        position += 1;
        return byte;
    }
    void normalize() {
        while (range < kRenormalizeBelow) {
            range <<= 8;
            code = (code << 8) | next_byte();
        }
    }

    std::span<const std::uint8_t> input;
    std::size_t position = 0;
    std::uint32_t range = 0xFFFFFFFFu;
    std::uint32_t code = 0;
};

}  // namespace cng_phase
