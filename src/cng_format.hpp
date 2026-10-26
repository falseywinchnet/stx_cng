// CNG 1.0 format constants and the definitions shared by the encoder and the decoder: container layout, symbol
// alphabets and contexts, prefix codes, checksums and table normalisation. docs/FORMAT.md is the specification;
// every definition here follows it.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stx_cng::detail {

// ------------------------------------------------------------------ container
inline constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'C', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kFieldBytes = 28;   // width, height, channels, version, segment width and height, mosaic and tables lengths and CRCs
inline constexpr std::size_t kHeaderBytes = kSignature.size() + kFieldBytes + 4;
inline constexpr std::size_t kTailBytes = kFieldBytes + 4;
inline constexpr std::uint32_t kSegmentWidth = 128;
inline constexpr std::uint32_t kSegmentRows = 32;
inline constexpr std::uint32_t kCell = 16;                       // mosaic cell side
inline constexpr std::uint64_t kMaxSamples = 1ull << 31;         // width * height * channels
inline constexpr int kTablesLevel = 19;                          // zstd level of the encoder's tables frame

// ------------------------------------------------------------------ rANS
inline constexpr std::uint32_t kPrecision = 12;
inline constexpr std::uint32_t kTotal = 1u << kPrecision;
inline constexpr std::uint32_t kLower = 1u << 23;

// ------------------------------------------------------------------ contexts and alphabets
inline constexpr std::uint32_t kClasses = 5;
inline constexpr std::uint32_t kRed0 = kClasses;                    // red contexts: class x green bucket
inline constexpr std::uint32_t kBlue0 = kRed0 + 3 * kClasses;       // blue contexts: class x green bucket
inline constexpr std::uint32_t kAlpha0 = kBlue0 + 3 * kClasses;     // alpha contexts: quiet, busy
inline constexpr std::uint32_t kDistanceContext = kAlpha0 + 2;
inline constexpr std::uint32_t kContexts = kDistanceContext + 1;    // 38
inline constexpr std::int32_t kGate = 3;                             // classes at or above code red and blue minus green

inline constexpr std::uint32_t kMinimumLength = 2;
inline constexpr std::uint32_t kNewMinimumLength = 4;               // encoder: shortest match at a new distance
inline constexpr std::uint32_t kMaximumLength = 16384;              // encoder: longest match
inline constexpr std::uint32_t kLengthCodes = 30;
inline constexpr std::uint32_t kRepeats = 3;
inline constexpr std::uint32_t kSpecials = 6;
inline constexpr std::uint32_t kMaximumNewDistance = 16384;         // largest distance a distance code can express
inline constexpr std::uint32_t kWholeLength0 = 256;                 // green: whole-pixel match lengths
inline constexpr std::uint32_t kCache0 = kWholeLength0 + kLengthCodes;
inline constexpr std::uint32_t kCacheSize = 256;
inline constexpr std::uint32_t kGreenOnly0 = kCache0 + kCacheSize;   // green: green-only match lengths
inline constexpr std::uint32_t kGreenSymbols = kGreenOnly0 + kLengthCodes;   // 572
inline constexpr std::uint32_t kPlaneLength0 = 256;                 // red, blue, alpha: plane match lengths
inline constexpr std::uint32_t kEscapeMaximum = 2;                  // encoder: symbols used at most this often are escaped

// Dequantized frequency of q = i + 1 (max(1, round(2^(i/2)))).
inline constexpr std::array<std::uint16_t, 25> kLevels = {1, 1, 2, 3, 4, 6, 8, 11, 16, 23, 32, 45, 64, 91, 128, 181, 256, 362, 512,
                                                          724, 1024, 1448, 2048, 2896, 4096};

// ------------------------------------------------------------------ bytes and checksums
[[nodiscard]] inline std::uint32_t read_u32(const std::uint8_t* bytes) {
    std::uint32_t value = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
                          | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
    return value;
}

inline void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24)); out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8)); out.push_back(static_cast<std::uint8_t>(value));
}

// CRC-32 (ISO-HDLC: reflected polynomial 0xEDB88320, initial and final value 0xFFFFFFFF), the checksum zlib's crc32
// computes. One table of 256 entries, built at compile time.
[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) { c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
        table[n] = c;
    }
    return table;
}
inline constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

// The CRC-32 of the bytes that produced `crc` followed by `bytes` (crc = 0 for none), as zlib's crc32(crc, ...).
[[nodiscard]] inline std::uint32_t crc_extend(std::uint32_t crc, std::span<const std::uint8_t> bytes) {
    std::uint32_t c = ~crc;
    for (std::uint8_t b : bytes) { c = kCrcTable[(c ^ b) & 0xFFu] ^ (c >> 8); }
    return ~c;
}

[[nodiscard]] inline std::uint32_t crc_of(std::span<const std::uint8_t> bytes) { return crc_extend(0, bytes); }

// A block of `body` bytes followed by their big-endian CRC32.
[[nodiscard]] inline bool block_valid(std::span<const std::uint8_t> block, std::size_t body) {
    if (block.size() < body + 4) { return false; }
    bool valid = read_u32(block.data() + body) == crc_of(block.subspan(0, body));
    return valid;
}

// ------------------------------------------------------------------ residuals and codes
// ((value + 128) & 255) - 128: the low byte of value as a signed byte (one sign extension).
[[nodiscard]] inline std::int32_t wrap(std::int32_t value) { return static_cast<std::int8_t>(static_cast<std::uint8_t>(value)); }

// Zigzag coding of a residual: 0, -1, 1, -2, 2, ... as 0, 1, 2, 3, 4, ...
[[nodiscard]] inline std::uint32_t zig(std::int32_t value) { return value >= 0 ? static_cast<std::uint32_t>(2 * value) : static_cast<std::uint32_t>(-2 * value - 1); }

// Zigzag decoding: even symbols map to 0, 1, 2, ..., odd ones to -1, -2, ... (a shift and an exclusive or).
[[nodiscard]] inline std::int32_t unzig(std::uint32_t symbol) {
    return static_cast<std::int32_t>(symbol >> 1) ^ -static_cast<std::int32_t>(symbol & 1u);
}

// Raw bits of an escaped symbol in context `context`.
[[nodiscard]] inline std::uint32_t raw_bits(std::uint32_t context) {
    if (context < kRed0) { return 10; }
    if (context == kDistanceContext) { return 6; }
    return 9;
}

// Prefix code of a value v >= 0: codes 0..3 are the values themselves; code 2h + b (h >= 2) covers
// [(2 + b) 2^(h - 1), (3 + b) 2^(h - 1)) with h - 1 extra bits.
[[nodiscard]] inline std::uint32_t code_bits(std::uint32_t code) { return code < 4 ? 0 : (code >> 1) - 1; }

[[nodiscard]] inline std::uint32_t unprefix(std::uint32_t code, std::uint32_t extra) {
    if (code < 4) { return code; }
    std::uint32_t h = code >> 1;
    std::uint32_t value = ((2u | (code & 1u)) << (h - 1)) | extra;
    return value;
}

struct PrefixCode {
    std::uint32_t code = 0;
    std::uint32_t bits = 0;
    std::uint32_t extra = 0;
};

[[nodiscard]] inline PrefixCode prefix(std::uint32_t value) {
    PrefixCode result;
    if (value < 4) { result.code = value; return result; }
    std::uint32_t h = 31 - static_cast<std::uint32_t>(__builtin_clz(value));   // floor(log2(value))
    result.bits = h - 1;
    result.code = 2 * h + ((value >> result.bits) & 1u);
    result.extra = value & ((1u << result.bits) - 1);
    return result;
}

// ------------------------------------------------------------------ tables
// Orders symbol indices by descending scaled frequency; std::stable_sort keeps equal values in index order.
struct DescendingScaled {
    const std::vector<std::int64_t>* scaled;
    [[nodiscard]] bool operator()(std::size_t a, std::size_t b) const { return (*scaled)[a] > (*scaled)[b]; }
};

// Frequencies summing to 4096 from q bytes (empty when every q is 0); the format's normalisation.
inline void normalize(std::span<const std::uint8_t> q, std::vector<std::uint16_t>& frequency) {
    frequency.clear();
    std::vector<std::uint32_t> raw(q.size(), 0);
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < q.size(); ++i) {
        if (q[i] != 0) { raw[i] = kLevels[q[i] - 1]; }
        total += raw[i];
    }
    if (total == 0) { return; }
    std::vector<std::int64_t> scaled(q.size(), 0);
    std::int64_t sum = 0;
    for (std::size_t i = 0; i < q.size(); ++i) {
        if (raw[i] != 0) { scaled[i] = std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<std::uint64_t>(raw[i]) * kTotal / total)); }
        sum += scaled[i];
    }
    std::vector<std::size_t> order(q.size());
    for (std::size_t i = 0; i < order.size(); ++i) { order[i] = i; }
    std::stable_sort(order.begin(), order.end(), DescendingScaled{&scaled});   // by (-scaled, index)
    std::int64_t difference = static_cast<std::int64_t>(kTotal) - sum;
    std::size_t step = 0;
    while (difference != 0) {
        std::size_t k = order[step % order.size()];
        if (difference > 0) { scaled[k] += difference; difference = 0; }
        else if (scaled[k] > 1) {
            std::int64_t take = std::min<std::int64_t>(scaled[k] - 1, -difference);
            scaled[k] -= take; difference += take;
        }
        ++step;
    }
    frequency.resize(q.size());
    for (std::size_t i = 0; i < q.size(); ++i) { frequency[i] = static_cast<std::uint16_t>(scaled[i]); }
}

}  // namespace stx_cng::detail
