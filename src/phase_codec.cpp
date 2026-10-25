#include "phase_codec.hpp"
#include "phase_tables.hpp"
#include "range_coder.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

// Native reference of research/phase_codec_v7.py (CNGPHI07): the CNGPHI05 construction inside independently decodable
// 128x128 frames (models, coder and repetition table restart per frame; frame edges take the image-edge rules), with a
// frame directory of lengths and CRC32 checksums so that frames decode in any order, in parallel, and damage is confined.
//
// Per-frame engine (unchanged from CNGPHI05): Every quantity is integer; the pixel routine is shared
// by encoder and decoder through a compile-time direction, so the decoder recomputes exactly what the encoder saw.
//
// The codec streams: all per-plane state lives in row rings of at most K+2 rows (the causal window reaches K rows
// up, and the column sums drop row y-1-K when they advance), the encoder reads source rows from the caller's buffer
// and the decoder writes finished samples straight into the caller's destination. The only state proportional to
// the image is the bounded repetition table, whose capacity is a power of two at most 2^22 slots.
//
// Work per row: first the row passes (column sums and their prefix sums for every cleaned plane, the row-above
// part of the activity sum, the row-above part of the repetition key), which are plain loops over x with no
// dependence on the current row; then the serial pixel loop, which gathers the causal quantities at (y, x) either
// through the interior kernel (no edge checks, prefix-sum moments) or the border kernel (direct sums with the
// boundary rules), and codes the pixel. The interior is y >= K and K <= x <= W-1-K.
namespace cng_phase {
namespace {

constexpr std::int32_t kPhaseBins = 8;        // PB
constexpr std::int32_t kGate = 5;             // GATE
constexpr std::int32_t kCleanThreshold = 2;   // CLEAN_T
constexpr std::uint32_t kMaxConfidence = 3;   // MAXCONF
constexpr std::uint32_t kActivityBins = 8;
constexpr std::array<std::int32_t, 8> kErrorBinEdges = {0, 1, 2, 3, 5, 9, 17, 33};
constexpr std::array<std::int32_t, 7> kMagnitudeBinEdges = {0, 1, 2, 3, 5, 9, 17};
constexpr std::uint32_t kPhaseStates = 34;    // -16..16 plus "no taps"
constexpr std::uint32_t kPhaseNone = 33;
constexpr std::uint32_t kOffsetStates = 8;    // -3..3 plus "no taps"
constexpr std::uint32_t kOffsetNone = 7;
constexpr std::uint32_t kMagnitudeBins = 7;
constexpr std::uint32_t kPrefixStates = 16;
constexpr std::uint32_t kRadius = static_cast<std::uint32_t>(kWindowRadius);   // K
constexpr std::uint32_t kWindowRows = kRadius + 2;                               // rows y-1-K .. y kept per cleaned plane
constexpr std::uint32_t kRepeatMinLog = 6;    // REPEAT_MIN_LOG
constexpr std::uint32_t kRepeatMaxLog = 22;   // REPEAT_MAX_LOG
constexpr std::array<std::uint8_t, 8> kSignature = {0x43, 0x4E, 0x47, 0x50, 0x48, 0x49, 0x30, 0x37};  // "CNGPHI07"
constexpr std::uint32_t kFrame = 128;                                                              // default frame side in pixels
constexpr std::uint32_t kMinFrame = 16;
constexpr std::size_t kFieldBytes = 10;                 // width u32, height u32, channels u8, frame side u8
constexpr std::size_t kTailBytes = kFieldBytes + 4;      // trailing copy of the header fields and their CRC32

// Causal offsets as (rows above, columns left). The activity sum uses (0,1) (1,0) (1,1) (1,-1) (0,2) (2,0): the
// row pass above_energy_row sums the four offsets in the rows above, the pixel loop adds the two on the current row.
constexpr std::array<std::array<std::int32_t, 2>, 4> kRepeatOffsets = {{{0, 1}, {1, 0}, {1, 1}, {1, -1}}};
constexpr std::uint64_t kOutsideComponent = 0x1FF;  // encodes -1 (outside the image) in a 9-bit key component

[[nodiscard]] constexpr std::int32_t wrap8(std::int32_t value) { return ((value + 128) & 255) - 128; }
[[nodiscard]] constexpr std::uint32_t zigzag(std::int32_t value) {
    return value >= 0 ? static_cast<std::uint32_t>(2 * value) : static_cast<std::uint32_t>(-2 * value - 1);
}
[[nodiscard]] constexpr std::int32_t unzigzag(std::uint32_t symbol) {
    return (symbol & 1u) == 0 ? static_cast<std::int32_t>(symbol >> 1) : -static_cast<std::int32_t>((symbol + 1) >> 1);
}
[[nodiscard]] constexpr std::int64_t floor_divide(std::int64_t numerator, std::int64_t denominator) {
    std::int64_t quotient = numerator / denominator;
    std::int64_t remainder = numerator % denominator;
    if (remainder != 0 && ((remainder < 0) != (denominator < 0))) { quotient -= 1; }
    return quotient;
}
[[nodiscard]] constexpr std::int32_t median_edge(std::int32_t left, std::int32_t up, std::int32_t up_left) {
    std::int32_t low = std::min(left, up);
    std::int32_t high = std::max(left, up);
    if (up_left >= high) { return low; }
    if (up_left <= low) { return high; }
    return left + up - up_left;
}
template <std::size_t kEdges>
[[nodiscard]] constexpr std::uint32_t bin_of(std::int32_t value, const std::array<std::int32_t, kEdges>& edges) {
    std::uint32_t bin = 0;
    while (bin + 1 < edges.size() && value >= edges[bin + 1]) { bin += 1; }
    return bin;
}
// Modular mean/opponent lift of the published CNG format: three wrapped 8-bit planes, bijective on RGB8.
struct Opponent { std::int32_t y = 0; std::int32_t a = 0; std::int32_t bc = 0; };
[[nodiscard]] constexpr Opponent lift(std::int32_t red, std::int32_t green, std::int32_t blue) {
    std::int32_t a = wrap8(red - green);
    std::int32_t t = wrap8(green + (a >> 1));
    std::int32_t bc = wrap8(blue - t);
    std::int32_t y = wrap8(t + static_cast<std::int32_t>(floor_divide(bc + (a & 1) + 1, 3)));
    return Opponent{y, a, bc};
}
struct Rgb { std::int32_t red = 0; std::int32_t green = 0; std::int32_t blue = 0; };
[[nodiscard]] constexpr Rgb unlift(Opponent o) {
    std::int32_t t = wrap8(o.y - static_cast<std::int32_t>(floor_divide(o.bc + (o.a & 1) + 1, 3)));
    std::int32_t green = wrap8(t - (o.a >> 1));
    std::int32_t red = wrap8(green + o.a);
    std::int32_t blue = wrap8(o.bc + t);
    return Rgb{red & 255, green & 255, blue & 255};
}

// ---------------------------------------------------------------- context enumeration
// Zero-flag families, their extents, and the coarse (magnitude) and sign families. Indices are mixed radix in
// the order of the reference's tuple keys; families never overlap.
struct ZeroFamilies {
    static constexpr std::uint32_t alpha_repeat = 0;                                     // conf3 x act8
    static constexpr std::uint32_t alpha = alpha_repeat + 3 * 8;                         // act8 x phase34
    static constexpr std::uint32_t colour_repeat = alpha + 8 * kPhaseStates;             // conf3 x act8 x actx3
    static constexpr std::uint32_t green = colour_repeat + 3 * 8 * 3;                    // act8 x phase34 x actx3
    static constexpr std::uint32_t red_blue = green + 8 * kPhaseStates * 3;              // act8 x phase34 x actx3 x gq3
    static constexpr std::uint32_t luma = red_blue + 8 * kPhaseStates * 3 * 3;           // act8 x off8 x actx3
    static constexpr std::uint32_t chroma_a = luma + 8 * kOffsetStates * 3;              // act8 x ry7 x off8 x actx3
    static constexpr std::uint32_t chroma_b = chroma_a + 8 * 7 * kOffsetStates * 3;      // act8 x ry7 x rco7 x off8 x actx3
    static constexpr std::uint32_t total = chroma_b + 8 * 7 * 7 * kOffsetStates * 3;
};
struct CoarseFamilies {
    static constexpr std::uint32_t alpha_repeat = 0;               // conf3 x act8
    static constexpr std::uint32_t alpha = 24;                     // act8
    static constexpr std::uint32_t colour_repeat = 32;             // conf3 x act8
    static constexpr std::uint32_t green = 56;                     // act8
    static constexpr std::uint32_t red_blue = 64;                  // act8
    static constexpr std::uint32_t luma = 72;                      // act8
    static constexpr std::uint32_t chroma_a = 80;                  // act8 x ry7
    static constexpr std::uint32_t chroma_b = 136;                 // act8 x rco7
    static constexpr std::uint32_t total = 192;
};
struct SignFamilies {
    static constexpr std::uint32_t alpha_repeat = 0;               // conf3 x act8
    static constexpr std::uint32_t alpha = 24;                     // act8 x sg3
    static constexpr std::uint32_t colour_repeat = 48;             // conf3 x act8
    static constexpr std::uint32_t green = 72;                     // act8 x sg3
    static constexpr std::uint32_t red_blue = 96;                  // act8 x sg3 x gq3
    static constexpr std::uint32_t luma = 168;                     // act8 x off8
    static constexpr std::uint32_t chroma_a = 232;                 // act8 x ry7 x off8
    static constexpr std::uint32_t chroma_b = 680;                 // act8 x rco7 x off8
    static constexpr std::uint32_t total = 1128;
};
struct Models {
    std::vector<BitModel> zero;       // ZeroFamilies::total
    std::vector<BitModel> sign;       // SignFamilies::total
    std::vector<BitModel> prefix;     // CoarseFamilies::total x kPrefixStates
    std::vector<BitModel> suffix;     // CoarseFamilies::total x kPrefixStates
    Models() : zero(ZeroFamilies::total), sign(SignFamilies::total),
               prefix(CoarseFamilies::total * kPrefixStates), suffix(CoarseFamilies::total * kPrefixStates) {}
};
struct SymbolContexts {
    std::uint32_t zero = 0;
    std::uint32_t coarse = 0;
    std::uint32_t sign = 0;
};
[[nodiscard]] constexpr std::uint32_t phase_state(bool has_taps, std::int32_t quantized) {
    return has_taps ? static_cast<std::uint32_t>(quantized + 16) : kPhaseNone;
}
[[nodiscard]] constexpr std::uint32_t offset_state(bool has_taps, std::int32_t quantized) {
    return has_taps ? static_cast<std::uint32_t>(quantized + 3) : kOffsetNone;
}
[[nodiscard]] constexpr std::uint32_t sign_state(bool has_taps, std::int32_t quantized) {
    if (!has_taps || quantized == 0) { return 1; }
    return quantized > 0 ? 2u : 0u;
}

// ---------------------------------------------------------------- symbol coding
struct Direction {
    RangeEncoder* encoder = nullptr;
    RangeDecoder* decoder = nullptr;
    std::vector<SymbolTrace>* trace = nullptr;   // encoder instrumentation only; null in ordinary use
    std::uint32_t pixel = 0;                     // pixel index of the symbols being coded (for the trace)
};
template <bool kDecoding>
void code_bit(Direction& direction, BitModel& model, std::uint32_t& bit) {
    if constexpr (kDecoding) { bit = (*direction.decoder).decode_bit(model); }
    else { (*direction.encoder).encode_bit(model, bit); }
}
template <bool kDecoding>
void code_bypass(Direction& direction, std::uint32_t& bit) {
    if constexpr (kDecoding) { bit = (*direction.decoder).decode_bypass(); }
    else { (*direction.encoder).encode_bypass(bit); }
}
// Zero flag, sign, exp-Golomb magnitude with adaptive prefix bins and adaptive leading suffix bit.
template <bool kDecoding>
void code_symbol(Direction& direction, Models& models, const SymbolContexts& contexts, std::uint32_t& symbol) {
    if constexpr (!kDecoding) {
        if (direction.trace != nullptr) {
            SymbolTrace record;
            record.pixel = direction.pixel; record.zero_context = contexts.zero; record.coarse_context = contexts.coarse;
            record.sign_context = contexts.sign; record.symbol = symbol;
            (*direction.trace).push_back(record);
        }
    }
    std::uint32_t is_zero = kDecoding ? 0u : (symbol == 0 ? 1u : 0u);
    code_bit<kDecoding>(direction, models.zero[contexts.zero], is_zero);
    if (is_zero != 0) { symbol = 0; return; }
    std::uint32_t sign = kDecoding ? 0u : (symbol & 1u);
    code_bit<kDecoding>(direction, models.sign[contexts.sign], sign);
    std::uint32_t prefix_base = contexts.coarse * kPrefixStates;
    std::uint32_t prefix_length = 0;
    std::uint32_t value = 1;  // magnitude + 1, with its leading one bit
    if constexpr (kDecoding) {
        std::uint32_t more = 1;
        while (prefix_length < 24) {
            code_bit<true>(direction, models.prefix[prefix_base + std::min(prefix_length, kPrefixStates - 1)], more);
            if (more == 0) { break; }
            prefix_length += 1;
        }
    } else {
        std::uint32_t magnitude = (symbol + 1) / 2 - 1;
        value = magnitude + 1;
        while ((value >> (prefix_length + 1)) != 0) { prefix_length += 1; }
        for (std::uint32_t index = 0; index < prefix_length; ++index) {
            std::uint32_t one = 1;
            code_bit<false>(direction, models.prefix[prefix_base + std::min(index, kPrefixStates - 1)], one);
        }
        std::uint32_t zero = 0;
        code_bit<false>(direction, models.prefix[prefix_base + std::min(prefix_length, kPrefixStates - 1)], zero);
    }
    std::uint32_t suffix_model = prefix_base + std::min(prefix_length, kPrefixStates - 1);
    if constexpr (kDecoding) {
        for (std::uint32_t index = 0; index < prefix_length; ++index) {
            std::uint32_t bit = 0;
            if (index == 0) { code_bit<true>(direction, models.suffix[suffix_model], bit); }
            else { code_bypass<true>(direction, bit); }
            value = (value << 1) | bit;
        }
        std::uint32_t magnitude = value - 1;
        symbol = 2 * magnitude + 2 - sign;
    } else {
        for (std::uint32_t index = prefix_length; index > 0; --index) {
            std::uint32_t bit = (value >> (index - 1)) & 1u;
            if (index == prefix_length) { code_bit<false>(direction, models.suffix[suffix_model], bit); }
            else { code_bypass<false>(direction, bit); }
        }
    }
}

// ---------------------------------------------------------------- bounded repetition table
// Direct-mapped map from the causal neighbourhood (L, U, UL, UR of all planes; -1 outside the image) to the last
// colour that followed it and a confidence 0..3. Capacity is the smallest power of two with capacity*2 >= pixels
// (REPEAT_RATIO = 1/2), clamped to [2^6, 2^22]. A slot keeps a 29-bit fingerprint of the key's hash; a fingerprint
// collision yields a wrong prediction that the residual corrects, identically in encoder and decoder. Eight bytes
// per slot: tag = ((fingerprint + 1) << 2) | confidence (0 means empty) and the packed colour.
struct RepeatKey {
    std::array<std::uint64_t, 3> words = {0, 0, 0};  // sixteen 9-bit components packed little-end first
};
[[nodiscard]] std::uint64_t hash_key(const RepeatKey& key) {
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    for (std::size_t index = 0; index < 3; ++index) {
        h ^= key.words[index];
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 29;
    }
    return h;
}
struct RepeatLookup {
    std::size_t slot = 0;
    std::uint32_t fingerprint = 0;      // 29 bits
    bool hit = false;
    std::uint32_t confidence = 0;
    std::array<std::uint8_t, 4> colour = {0, 0, 0, 0};
};
struct RepeatTable {
    std::vector<std::uint32_t> tags;
    std::vector<std::uint32_t> colours;
    std::uint64_t mask = 0;
    void reset(std::uint64_t pixel_count) {
        std::uint64_t capacity = 1ull << kRepeatMinLog;
        while (capacity < (1ull << kRepeatMaxLog) && capacity * 2 < pixel_count) { capacity *= 2; }
        tags.assign(static_cast<std::size_t>(capacity), 0);
        colours.assign(static_cast<std::size_t>(capacity), 0);
        mask = capacity - 1;
    }
    [[nodiscard]] RepeatLookup lookup(const RepeatKey& key) const {
        std::uint64_t h = hash_key(key);
        RepeatLookup found;
        found.slot = static_cast<std::size_t>(h & mask);
        found.fingerprint = static_cast<std::uint32_t>(h >> 35);
        std::uint32_t tag = tags[found.slot];
        found.hit = (tag >> 2) == found.fingerprint + 1;
        if (found.hit) {
            found.confidence = tag & 3u;
            std::uint32_t packed = colours[found.slot];
            found.colour = {static_cast<std::uint8_t>(packed), static_cast<std::uint8_t>(packed >> 8),
                            static_cast<std::uint8_t>(packed >> 16), static_cast<std::uint8_t>(packed >> 24)};
        }
        return found;
    }
    void store(const RepeatLookup& at, const std::array<std::uint8_t, 4>& colour, std::uint32_t confidence) {
        tags[at.slot] = ((at.fingerprint + 1) << 2) | confidence;
        colours[at.slot] = static_cast<std::uint32_t>(colour[0]) | (static_cast<std::uint32_t>(colour[1]) << 8)
                           | (static_cast<std::uint32_t>(colour[2]) << 16) | (static_cast<std::uint32_t>(colour[3]) << 24);
    }
};

// ---------------------------------------------------------------- row rings
// A ring of `rows` image rows; row y lives at slot y mod rows. A ring of R rows serves reads of rows y-R+1 .. y
// while row y is being written, which is all the causal access the codec makes.
struct RowRing {
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    std::vector<std::int32_t> storage;
    void prepare(std::uint32_t w, std::uint32_t r) { width = w; rows = r; storage.assign(static_cast<std::size_t>(w) * r, 0); }
    [[nodiscard]] std::int32_t* row(std::uint32_t y) { return storage.data() + static_cast<std::size_t>(y % rows) * width; }
    [[nodiscard]] const std::int32_t* row(std::uint32_t y) const { return storage.data() + static_cast<std::size_t>(y % rows) * width; }
};
struct ByteRing {
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    std::vector<std::uint8_t> storage;
    void prepare(std::uint32_t w, std::uint32_t r) { width = w; rows = r; storage.assign(static_cast<std::size_t>(w) * r, 0); }
    [[nodiscard]] std::uint8_t* row(std::uint32_t y) { return storage.data() + static_cast<std::size_t>(y % rows) * width; }
    [[nodiscard]] const std::uint8_t* row(std::uint32_t y) const { return storage.data() + static_cast<std::size_t>(y % rows) * width; }
};

// ---------------------------------------------------------------- window moments
// m0 = sum S, m1 = sum dx*S, m2 = sum (-dy)*S over the causal window available at the boundary configuration
// (t, l, r). Interior pixels use per-row column sums over the K rows above and their prefix sums along x, so the
// above-row part of each moment is a difference of two prefixes and the current-row part is a K-tap sum of the
// samples just decoded. Border configurations are summed directly. Both give identical integers.
struct Moments { std::int64_t m0 = 0; std::int64_t m1 = 0; std::int64_t m2 = 0; };
struct PlaneRows {
    std::uint32_t width = 0;
    std::vector<std::int32_t> column_sum;      // C0[x] = sum_{dy=1..K} S[y-dy][x] for the current row y
    std::vector<std::int32_t> column_depth;    // C2[x] = sum_{dy=1..K} dy * S[y-dy][x]
    std::vector<std::int64_t> prefix0;         // P0[x] = sum_{x'<x} C0[x'],      size width+1
    std::vector<std::int64_t> prefix1;         // P1[x] = sum_{x'<x} x' * C0[x']
    std::vector<std::int64_t> prefix2;         // P2[x] = sum_{x'<x} C2[x']
    void prepare(std::uint32_t w) {
        width = w;
        column_sum.assign(w, 0); column_depth.assign(w, 0);
        prefix0.assign(static_cast<std::size_t>(w) + 1, 0); prefix1.assign(static_cast<std::size_t>(w) + 1, 0); prefix2.assign(static_cast<std::size_t>(w) + 1, 0);
    }
    // Before coding row y >= 1: advance the column sums from row y-1 to row y (newest = row y-1 of the cleaned
    // plane, oldest = row y-1-K or null while y <= K), then rebuild the prefixes.
    // new C2 = (old C2 - K*dropped) + new C0, with new C0 = old C0 - dropped + newest.
    void begin_row(const std::int32_t* newest, const std::int32_t* oldest) {
        std::int32_t* sum = column_sum.data();
        std::int32_t* depth = column_depth.data();
        if (oldest != nullptr) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::int32_t dropped = oldest[x];
                std::int32_t next_sum = sum[x] - dropped + newest[x];
                depth[x] = depth[x] - static_cast<std::int32_t>(kRadius) * dropped + next_sum;
                sum[x] = next_sum;
            }
        } else {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::int32_t next_sum = sum[x] + newest[x];
                depth[x] = depth[x] + next_sum;
                sum[x] = next_sum;
            }
        }
        std::int64_t running0 = 0; std::int64_t running1 = 0; std::int64_t running2 = 0;
        std::int64_t* p0 = prefix0.data(); std::int64_t* p1 = prefix1.data(); std::int64_t* p2 = prefix2.data();
        for (std::uint32_t x = 0; x < width; ++x) {
            p0[x] = running0; p1[x] = running1; p2[x] = running2;
            running0 += sum[x];
            running1 += static_cast<std::int64_t>(x) * sum[x];
            running2 += depth[x];
        }
        p0[width] = running0; p1[width] = running1; p2[width] = running2;
    }
};
// Window moments at column x from the per-row prefix sums: columns x-l..x+r of the rows above (the column sums hold
// exactly the rows available above y, so this is valid at every y) plus the l samples to the left on the current row.
// Identical integers to the direct sums at every boundary configuration.
[[nodiscard]] Moments window_moments(const PlaneRows& rows, const std::int32_t* current_row, std::uint32_t x, std::uint32_t l, std::uint32_t r) {
    std::size_t low = x - l;
    std::size_t high = static_cast<std::size_t>(x) + r + 1;
    std::int64_t window0 = rows.prefix0[high] - rows.prefix0[low];
    std::int64_t window1 = rows.prefix1[high] - rows.prefix1[low];
    std::int64_t window2 = rows.prefix2[high] - rows.prefix2[low];
    std::int64_t row0 = 0; std::int64_t row1 = 0;
    for (std::uint32_t d = 1; d <= l; ++d) {
        std::int64_t sample = current_row[x - d];
        row0 += sample;
        row1 -= static_cast<std::int64_t>(d) * sample;
    }
    Moments m;
    m.m0 = window0 + row0;
    m.m1 = (window1 - static_cast<std::int64_t>(x) * window0) + row1;
    m.m2 = -window2;
    return m;
}
struct Phase { bool has_taps = false; std::int32_t quantized = 0; };
// Q24 plane extrapolation from the window moments; offset from the integer prediction in 1/8 steps, floored.
// floor(n / (step * 2^24)) = floor(floor(n / 2^24) / step) for positive step, so the shift comes first and the
// division by step (1 in RGB mode, 3 or 4 in opponent mode) acts on a small quotient.
[[nodiscard]] Phase phase_from(const Moments& m, const std::int64_t* c, std::int32_t prediction, std::int32_t step, std::int32_t clip) {
    std::int64_t accumulator = c[0] * m.m0 + c[1] * m.m1 + c[2] * m.m2;
    std::int64_t offset = accumulator - (static_cast<std::int64_t>(prediction) << kMomentBits);
    std::int64_t numerator = offset * kPhaseBins + (static_cast<std::int64_t>(step) << (kMomentBits - 1));
    std::int64_t shifted = numerator >> kMomentBits;   // arithmetic shift: floor division by 2^24
    std::int64_t quantized = (step == 1) ? shifted : floor_divide(shifted, step);
    if (quantized > clip) { quantized = clip; }
    if (quantized < -clip) { quantized = -clip; }
    Phase phase;
    phase.has_taps = true;
    phase.quantized = static_cast<std::int32_t>(quantized);
    return phase;
}
struct Configuration { std::uint32_t t = 0; std::uint32_t l = 0; std::uint32_t r = 0; };
[[nodiscard]] Configuration configuration_at(std::uint32_t width, std::uint32_t y, std::uint32_t x) {
    Configuration c;
    c.t = std::min<std::uint32_t>(y, kRadius);
    c.l = std::min<std::uint32_t>(x, kRadius);
    c.r = std::min<std::uint32_t>(width - 1 - x, kRadius);
    return c;
}

// ---------------------------------------------------------------- workspace
constexpr std::uint32_t kPlaneSlots = 7;   // cleaned RGBA (0..3) then cleaned opponent (4..6)
struct Workspace {
    Dimensions dimensions;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    RowRing samples[4];                      // decoded samples 0..255, rows y-1..y
    RowRing cleaned[4];                      // cleaned RGB(A) planes, rows y-1-K..y
    RowRing opponent[3];                     // lifted planes, wrapped, rows y-1..y
    RowRing cleaned_opponent[3];             // cleaned lifted planes, rows y-1-K..y
    ByteRing error_energy;                   // max over colour planes of |MED residual|, rows y-2..y
    std::vector<std::int32_t> above_energy;  // row scratch: activity contribution of the rows above at each x
    std::vector<RepeatKey> above_key;        // row scratch: U, UL, UR components of the repetition key at each x
    RepeatTable repeats;
    Models models;
    PlaneRows rows[kPlaneSlots];
    std::uint32_t stride = 0;        // samples per image row between frame rows (the image width)
    std::uint32_t origin_x = 0, origin_y = 0, image_width = 0;   // frame position in the image (for the trace's pixel index)

    void place(std::uint32_t image_stride, std::uint32_t ox, std::uint32_t oy, std::uint32_t iw) { stride = image_stride; origin_x = ox; origin_y = oy; image_width = iw; }
    void prepare(Dimensions d) {
        dimensions = d; width = d.width; height = d.height; channels = d.channels;
        for (std::uint32_t c = 0; c < 4; ++c) { samples[c].prepare(c < channels ? width : 0, 2); cleaned[c].prepare(c < channels ? width : 0, kWindowRows); }
        for (std::uint32_t k = 0; k < 3; ++k) { opponent[k].prepare(width, 2); cleaned_opponent[k].prepare(width, kWindowRows); }
        error_energy.prepare(width, 3);
        above_energy.assign(width, 0);
        above_key.assign(width, RepeatKey{});
        repeats.reset(static_cast<std::uint64_t>(width) * height);
        for (std::uint32_t slot = 0; slot < kPlaneSlots; ++slot) { rows[slot].prepare(width); }
        // the adaptive models restart with every frame: the workspace is reused across frames
        models.zero.assign(ZeroFamilies::total, BitModel{});
        models.sign.assign(SignFamilies::total, BitModel{});
        models.prefix.assign(static_cast<std::size_t>(CoarseFamilies::total) * kPrefixStates, BitModel{});
        models.suffix.assign(static_cast<std::size_t>(CoarseFamilies::total) * kPrefixStates, BitModel{});
    }
};
struct Neighbours { std::int32_t left = 0; std::int32_t up = 0; std::int32_t up_left = 0; std::int32_t up_right = 0; };
// JPEG-LS boundary rules: row above the image is zero (above == nullptr); left of the image is the sample above.
[[nodiscard]] Neighbours neighbours(const std::int32_t* current, const std::int32_t* above, std::uint32_t width, std::uint32_t x) {
    Neighbours n;
    n.up = (above != nullptr) ? above[x] : 0;
    n.up_left = (above != nullptr && x > 0) ? above[x - 1] : n.up;
    n.up_right = (above != nullptr && x + 1 < width) ? above[x + 1] : n.up;
    n.left = (x > 0) ? current[x - 1] : n.up;
    return n;
}
// Interior prediction: y >= 1 and 1 <= x <= width-2, no boundary rule applies.
[[nodiscard]] std::int32_t interior_prediction(const std::int32_t* current, const std::int32_t* above, std::uint32_t x) {
    return median_edge(current[x - 1], above[x], above[x - 1]);
}

// ---------------------------------------------------------------- row passes
// Activity contribution of the rows above: E[y-1][x] + E[y-1][x-1] + E[y-1][x+1] + E[y-2][x], with the
// boundary rule that offsets outside the image contribute nothing. The current row's E[y][x-1] + E[y][x-2] is
// added per pixel.
void above_energy_row(Workspace& w, std::uint32_t y) {
    const std::uint32_t width = w.width;
    std::int32_t* out = w.above_energy.data();
    if (y == 0) { for (std::uint32_t x = 0; x < width; ++x) { out[x] = 0; } return; }
    const std::uint8_t* row1 = w.error_energy.row(y - 1);
    for (std::uint32_t x = 0; x < width; ++x) { out[x] = row1[x]; }
    for (std::uint32_t x = 1; x < width; ++x) { out[x] += row1[x - 1]; }
    for (std::uint32_t x = 0; x + 1 < width; ++x) { out[x] += row1[x + 1]; }
    if (y >= 2) {
        const std::uint8_t* row2 = w.error_energy.row(y - 2);
        for (std::uint32_t x = 0; x < width; ++x) { out[x] += row2[x]; }
    }
}
// Components of the repetition key in reference order: L (all channels), U, UL, UR; component i occupies bits
// 9*(i mod 7) of word i/7. The row pass fills U, UL and UR for every x; the pixel loop adds L.
void above_key_row(Workspace& w, std::uint32_t y) {
    const std::uint32_t width = w.width;
    const std::uint32_t channels = w.channels;
    RepeatKey* out = w.above_key.data();
    for (std::uint32_t x = 0; x < width; ++x) { out[x] = RepeatKey{}; }
    for (std::size_t offset = 1; offset < kRepeatOffsets.size(); ++offset) {
        std::int32_t dx = kRepeatOffsets[offset][1];
        for (std::uint32_t c = 0; c < channels; ++c) {
            std::uint32_t component = static_cast<std::uint32_t>(offset) * channels + c;
            std::size_t word = component / 7;
            std::uint32_t shift = 9 * (component % 7);
            if (y == 0) {
                for (std::uint32_t x = 0; x < width; ++x) { out[x].words[word] |= kOutsideComponent << shift; }
                continue;
            }
            const std::int32_t* row = w.samples[c].row(y - 1);
            if (dx == 0) {              // U: the sample directly above
                for (std::uint32_t x = 0; x < width; ++x) { out[x].words[word] |= static_cast<std::uint64_t>(row[x]) << shift; }
            } else if (dx > 0) {        // UL: above-left; outside at x = 0
                out[0].words[word] |= kOutsideComponent << shift;
                for (std::uint32_t x = 1; x < width; ++x) { out[x].words[word] |= static_cast<std::uint64_t>(row[x - 1]) << shift; }
            } else {                    // UR: above-right; outside at the last column
                for (std::uint32_t x = 0; x + 1 < width; ++x) { out[x].words[word] |= static_cast<std::uint64_t>(row[x + 1]) << shift; }
                out[width - 1].words[word] |= kOutsideComponent << shift;
            }
        }
    }
}

// ---------------------------------------------------------------- the gathered causal state of one pixel
// Everything the coding step needs that depends only on already-decoded samples. The border and interior kernels
// fill the same record; the coding step does not know which produced it.
struct Causal {
    std::uint32_t activity = 0;
    RepeatKey key;
    std::int32_t prediction[4] = {0, 0, 0, 0};          // MED of the RGBA planes
    std::int32_t lifted_prediction[3] = {0, 0, 0};      // MED of the opponent planes
    Configuration configuration;
};
// Row pointers of the current and previous rows of every plane, fixed for the duration of one row.
struct RowViews {
    std::int32_t* sample[4] = {nullptr, nullptr, nullptr, nullptr};
    const std::int32_t* sample_above[4] = {nullptr, nullptr, nullptr, nullptr};
    std::int32_t* cleaned[4] = {nullptr, nullptr, nullptr, nullptr};
    std::int32_t* lifted[3] = {nullptr, nullptr, nullptr};
    const std::int32_t* lifted_above[3] = {nullptr, nullptr, nullptr};
    std::int32_t* lifted_clean[3] = {nullptr, nullptr, nullptr};
    std::uint8_t* energy = nullptr;
};
[[nodiscard]] std::uint32_t activity_bin(std::int32_t energy) { return std::min(bin_of(energy, kErrorBinEdges), kActivityBins - 1); }
void gather_border(const Workspace& w, const RowViews& v, std::uint32_t y, std::uint32_t x, Causal& out) {
    const std::uint32_t width = w.width;
    out.configuration = configuration_at(width, y, x);
    std::int32_t energy = w.above_energy[x];
    if (x >= 1) { energy += v.energy[x - 1]; }
    if (x >= 2) { energy += v.energy[x - 2]; }
    out.activity = activity_bin(energy);
    out.key = w.above_key[x];
    for (std::uint32_t c = 0; c < w.channels; ++c) {
        std::uint64_t value = kOutsideComponent;
        if (x > 0) { value = static_cast<std::uint64_t>(v.sample[c][x - 1]); }
        out.key.words[c / 7] |= value << (9 * (c % 7));
        Neighbours n = neighbours(v.sample[c], v.sample_above[c], width, x);
        out.prediction[c] = median_edge(n.left, n.up, n.up_left);
    }
    for (std::uint32_t k = 0; k < 3; ++k) {
        Neighbours n = neighbours(v.lifted[k], v.lifted_above[k], width, x);
        out.lifted_prediction[k] = median_edge(n.left, n.up, n.up_left);
    }
}
// Interior gather: y >= 1 and x >= 2, so the row above, the two left energies and the left sample exist without checks.
void gather_interior(const Workspace& w, const RowViews& v, std::uint32_t y, std::uint32_t x, Causal& out) {
    out.configuration = configuration_at(w.width, y, x);
    std::int32_t energy = w.above_energy[x] + v.energy[x - 1] + v.energy[x - 2];
    out.activity = activity_bin(energy);
    out.key = w.above_key[x];
    for (std::uint32_t c = 0; c < w.channels; ++c) {
        out.key.words[c / 7] |= static_cast<std::uint64_t>(v.sample[c][x - 1]) << (9 * (c % 7));
        out.prediction[c] = interior_prediction(v.sample[c], v.sample_above[c], x);
    }
    for (std::uint32_t k = 0; k < 3; ++k) { out.lifted_prediction[k] = interior_prediction(v.lifted[k], v.lifted_above[k], x); }
}
// Phase of a cleaned plane (ring `plane`, prefix slot `slot`) at the gathered pixel.
[[nodiscard]] Phase phase_of(const Workspace& w, const RowRing& plane, std::uint32_t slot, std::uint32_t y, std::uint32_t x, const Causal& causal,
                             std::int32_t prediction, std::int32_t step, std::int32_t clip) {
    const Configuration& c = causal.configuration;
    if (c.t == 0 && c.l == 0) { return Phase{}; }
    Moments m = window_moments(w.rows[slot], plane.row(y), x, c.l, c.r);
    return phase_from(m, kMomentCoefficients[c.t][c.l][c.r], prediction, step, clip);
}

// ---------------------------------------------------------------- the shared pixel routine
// Encoding reads `source` (interleaved samples, caller's buffer); decoding writes `destination` the same way.
template <bool kDecoding>
Status process(Workspace& w, Direction& direction, const std::uint8_t* source, std::uint8_t* destination, std::span<const std::uint8_t> stream) {
    const std::uint32_t width = w.width;
    const std::uint32_t channels = w.channels;
    const bool has_alpha = channels == 4;
    for (std::uint32_t y = 0; y < w.height; ++y) {
        if constexpr (kDecoding) {
            if ((*direction.decoder).consumed() > stream.size() + 64) { return Status::stream_exhausted; }
        }
        // row passes
        if (y >= 1) {
            const bool drop = y > kRadius;
            for (std::uint32_t c = 0; c < channels; ++c) {
                w.rows[c].begin_row(w.cleaned[c].row(y - 1), drop ? w.cleaned[c].row(y - 1 - kRadius) : nullptr);
            }
            for (std::uint32_t k = 0; k < 3; ++k) {
                w.rows[4 + k].begin_row(w.cleaned_opponent[k].row(y - 1), drop ? w.cleaned_opponent[k].row(y - 1 - kRadius) : nullptr);
            }
        }
        above_energy_row(w, y);
        above_key_row(w, y);
        RowViews v;
        for (std::uint32_t c = 0; c < channels; ++c) {
            v.sample[c] = w.samples[c].row(y); v.sample_above[c] = (y > 0) ? w.samples[c].row(y - 1) : nullptr; v.cleaned[c] = w.cleaned[c].row(y);
        }
        for (std::uint32_t k = 0; k < 3; ++k) {
            v.lifted[k] = w.opponent[k].row(y); v.lifted_above[k] = (y > 0) ? w.opponent[k].row(y - 1) : nullptr; v.lifted_clean[k] = w.cleaned_opponent[k].row(y);
        }
        v.energy = w.error_energy.row(y);
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sample_base = (static_cast<std::size_t>(y) * w.stride + x) * channels;
            direction.pixel = static_cast<std::uint32_t>((static_cast<std::size_t>(w.origin_y) + y) * w.image_width + w.origin_x + x);
            Causal causal;
            if (y >= 1 && x >= 2) { gather_interior(w, v, y, x, causal); }
            else { gather_border(w, v, y, x, causal); }
            const std::uint32_t activity = causal.activity;
            // repetition lookup
            RepeatLookup found = w.repeats.lookup(causal.key);
            bool repeat_mode = found.hit && found.confidence >= 1;
            std::uint32_t confidence_state = repeat_mode ? found.confidence - 1 : 0;
            const std::int32_t* prediction = causal.prediction;
            std::int32_t value[4] = {0, 0, 0, 0};
            std::uint32_t alpha_context = 0;
            if (has_alpha) {
                std::uint32_t symbol = 0;
                SymbolContexts contexts;
                if (repeat_mode) {
                    contexts.zero = ZeroFamilies::alpha_repeat + confidence_state * 8 + activity;
                    contexts.coarse = CoarseFamilies::alpha_repeat + confidence_state * 8 + activity;
                    contexts.sign = SignFamilies::alpha_repeat + confidence_state * 8 + activity;
                    if constexpr (!kDecoding) { symbol = zigzag(wrap8(source[sample_base + 3] - found.colour[3])); }
                    code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                    value[3] = (found.colour[3] + unzigzag(symbol)) & 255;
                } else {
                    Phase phase = phase_of(w, w.cleaned[3], 3, y, x, causal, prediction[3], 1, 16);
                    contexts.zero = ZeroFamilies::alpha + activity * kPhaseStates + phase_state(phase.has_taps, phase.quantized);
                    contexts.coarse = CoarseFamilies::alpha + activity;
                    contexts.sign = SignFamilies::alpha + activity * 3 + sign_state(phase.has_taps, phase.quantized);
                    if constexpr (!kDecoding) { symbol = zigzag(wrap8(source[sample_base + 3] - prediction[3])); }
                    code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                    value[3] = (prediction[3] + unzigzag(symbol)) & 255;
                }
                alpha_context = static_cast<std::uint32_t>(std::min(std::abs(wrap8(value[3] - prediction[3])), 2));
            }
            if (repeat_mode) {
                const std::uint32_t order[3] = {1, 0, 2};
                for (std::uint32_t c : order) {
                    SymbolContexts contexts;
                    contexts.zero = ZeroFamilies::colour_repeat + (confidence_state * 8 + activity) * 3 + alpha_context;
                    contexts.coarse = CoarseFamilies::colour_repeat + confidence_state * 8 + activity;
                    contexts.sign = SignFamilies::colour_repeat + confidence_state * 8 + activity;
                    std::uint32_t symbol = 0;
                    if constexpr (!kDecoding) { symbol = zigzag(wrap8(source[sample_base + c] - found.colour[c])); }
                    code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                    value[c] = (found.colour[c] + unzigzag(symbol)) & 255;
                }
            } else if (activity < static_cast<std::uint32_t>(kGate)) {
                std::uint32_t green_state = 0;
                const std::uint32_t order[3] = {1, 0, 2};
                for (std::uint32_t c : order) {
                    Phase phase = phase_of(w, w.cleaned[c], c, y, x, causal, prediction[c], 1, 16);
                    std::uint32_t phase_index = phase_state(phase.has_taps, phase.quantized);
                    std::uint32_t sign_index = sign_state(phase.has_taps, phase.quantized);
                    SymbolContexts contexts;
                    if (c == 1) {
                        contexts.zero = ZeroFamilies::green + (activity * kPhaseStates + phase_index) * 3 + alpha_context;
                        contexts.coarse = CoarseFamilies::green + activity;
                        contexts.sign = SignFamilies::green + activity * 3 + sign_index;
                    } else {
                        contexts.zero = ZeroFamilies::red_blue + ((activity * kPhaseStates + phase_index) * 3 + alpha_context) * 3 + green_state;
                        contexts.coarse = CoarseFamilies::red_blue + activity;
                        contexts.sign = SignFamilies::red_blue + (activity * 3 + sign_index) * 3 + green_state;
                    }
                    std::uint32_t symbol = 0;
                    if constexpr (!kDecoding) { symbol = zigzag(wrap8(source[sample_base + c] - prediction[c])); }
                    code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                    value[c] = (prediction[c] + unzigzag(symbol)) & 255;
                    if (c == 1) { green_state = static_cast<std::uint32_t>(std::clamp(wrap8(value[1] - prediction[1]), -1, 1) + 1); }
                }
            } else {
                std::int32_t step = 1 + static_cast<std::int32_t>(activity) / 2;
                const std::int32_t* lifted_prediction = causal.lifted_prediction;
                Phase offsets[3];
                for (std::uint32_t k = 0; k < 3; ++k) {
                    offsets[k] = phase_of(w, w.cleaned_opponent[k], 4 + k, y, x, causal, lifted_prediction[k], step, 3);
                }
                Opponent target;
                if constexpr (!kDecoding) {
                    target = lift(source[sample_base], source[sample_base + 1], source[sample_base + 2]);
                }
                std::uint32_t offset_index[3] = {offset_state(offsets[0].has_taps, offsets[0].quantized),
                                                 offset_state(offsets[1].has_taps, offsets[1].quantized),
                                                 offset_state(offsets[2].has_taps, offsets[2].quantized)};
                // luma
                SymbolContexts contexts;
                contexts.zero = ZeroFamilies::luma + (activity * kOffsetStates + offset_index[0]) * 3 + alpha_context;
                contexts.coarse = CoarseFamilies::luma + activity;
                contexts.sign = SignFamilies::luma + activity * kOffsetStates + offset_index[0];
                std::uint32_t symbol = 0;
                if constexpr (!kDecoding) { symbol = zigzag(wrap8(target.y - lifted_prediction[0])); }
                code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                std::int32_t decoded_y = wrap8(lifted_prediction[0] + unzigzag(symbol));
                std::uint32_t luma_bin = bin_of(std::abs(wrap8(decoded_y - lifted_prediction[0])), kMagnitudeBinEdges);
                // first chroma
                contexts.zero = ZeroFamilies::chroma_a + ((activity * kMagnitudeBins + luma_bin) * kOffsetStates + offset_index[1]) * 3 + alpha_context;
                contexts.coarse = CoarseFamilies::chroma_a + activity * kMagnitudeBins + luma_bin;
                contexts.sign = SignFamilies::chroma_a + (activity * kMagnitudeBins + luma_bin) * kOffsetStates + offset_index[1];
                symbol = 0;
                if constexpr (!kDecoding) { symbol = zigzag(wrap8(target.a - lifted_prediction[1])); }
                code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                std::int32_t decoded_a = wrap8(lifted_prediction[1] + unzigzag(symbol));
                std::uint32_t chroma_bin = bin_of(std::abs(wrap8(decoded_a - lifted_prediction[1])), kMagnitudeBinEdges);
                // second chroma
                contexts.zero = ZeroFamilies::chroma_b + (((activity * kMagnitudeBins + luma_bin) * kMagnitudeBins + chroma_bin) * kOffsetStates + offset_index[2]) * 3 + alpha_context;
                contexts.coarse = CoarseFamilies::chroma_b + activity * kMagnitudeBins + chroma_bin;
                contexts.sign = SignFamilies::chroma_b + (activity * kMagnitudeBins + chroma_bin) * kOffsetStates + offset_index[2];
                symbol = 0;
                if constexpr (!kDecoding) { symbol = zigzag(wrap8(target.bc - lifted_prediction[2])); }
                code_symbol<kDecoding>(direction, w.models, contexts, symbol);
                std::int32_t decoded_bc = wrap8(lifted_prediction[2] + unzigzag(symbol));
                Rgb rgb = unlift(Opponent{decoded_y, decoded_a, decoded_bc});
                value[0] = rgb.red; value[1] = rgb.green; value[2] = rgb.blue;
            }
            // commit the pixel and update state
            std::int32_t worst = 0;
            for (std::uint32_t c = 0; c < channels; ++c) {
                v.sample[c][x] = value[c];
                std::int32_t residual = wrap8(value[c] - prediction[c]);
                if (c < 3 && std::abs(residual) > worst) { worst = std::abs(residual); }
                v.cleaned[c][x] = std::abs(residual) >= kCleanThreshold ? prediction[c] : value[c];
                if constexpr (kDecoding) { destination[sample_base + c] = static_cast<std::uint8_t>(value[c]); }
            }
            v.energy[x] = static_cast<std::uint8_t>(worst);
            Opponent lifted_now = lift(value[0], value[1], value[2]);
            std::int32_t lifted_values[3] = {lifted_now.y, lifted_now.a, lifted_now.bc};
            for (std::uint32_t k = 0; k < 3; ++k) {
                std::int32_t predicted = causal.lifted_prediction[k];
                v.lifted[k][x] = lifted_values[k];
                v.lifted_clean[k][x] = std::abs(wrap8(lifted_values[k] - predicted)) >= kCleanThreshold ? predicted : lifted_values[k];
            }
            std::array<std::uint8_t, 4> colour = {static_cast<std::uint8_t>(value[0]), static_cast<std::uint8_t>(value[1]),
                                                  static_cast<std::uint8_t>(value[2]), static_cast<std::uint8_t>(value[3])};
            std::uint32_t confidence = 0;
            if (found.hit && found.colour == colour) { confidence = std::min<std::uint32_t>(found.confidence + 1, kMaxConfidence); }
            w.repeats.store(found, colour, confidence);
        }
    }
    return Status::ok;
}

[[nodiscard]] bool valid_dimensions(Dimensions d) {
    if (d.width == 0 || d.height == 0) { return false; }
    if (d.channels != 3 && d.channels != 4) { return false; }
    std::uint64_t pixels = static_cast<std::uint64_t>(d.width) * d.height;
    return pixels * d.channels <= (1ull << 31);
}
void write_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24)); out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8)); out.push_back(static_cast<std::uint8_t>(value));
}
[[nodiscard]] std::uint32_t read_u32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
           | (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}
// CRC-32 (IEEE 802.3, reflected, as zlib's crc32) over a byte span.
struct CrcTable {
    std::uint32_t entry[256] = {};
    constexpr CrcTable() {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) { c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
            entry[n] = c;
        }
    }
};
inline constexpr CrcTable kCrc{};
[[nodiscard]] std::uint32_t crc32_of(std::span<const std::uint8_t> bytes) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint8_t b : bytes) { c = kCrc.entry[(c ^ b) & 0xFFu] ^ (c >> 8); }
    return c ^ 0xFFFFFFFFu;
}
// Frame grid in raster order; the last column and row of frames may be smaller than kFrame.
struct FrameRect { std::uint32_t x0 = 0, y0 = 0, width = 0, height = 0; };
[[nodiscard]] std::uint32_t frames_across(std::uint32_t extent, std::uint32_t side) { return (extent + side - 1) / side; }
[[nodiscard]] std::uint32_t frames_of(Dimensions d, std::uint32_t side) { return frames_across(d.width, side) * frames_across(d.height, side); }
[[nodiscard]] FrameRect frame_rect(Dimensions d, std::uint32_t side, std::uint32_t index) {
    std::uint32_t across = frames_across(d.width, side);
    FrameRect r;
    r.x0 = (index % across) * side; r.y0 = (index / across) * side;
    r.width = std::min(side, d.width - r.x0); r.height = std::min(side, d.height - r.y0);
    return r;
}
// The directory block: (payload length u32, payload CRC32 u32) per frame in raster order, then the CRC32 of those
// entries. It is written twice, after the header and again before the trailing header copy, so that damage to
// either copy leaves the other to parse from.
[[nodiscard]] std::size_t directory_bytes(std::uint32_t frames) { return static_cast<std::size_t>(frames) * 8 + 4; }
[[nodiscard]] bool directory_valid(const std::uint8_t* block, std::uint32_t frames) {
    return read_u32(block + static_cast<std::size_t>(frames) * 8) == crc32_of(std::span<const std::uint8_t>(block, static_cast<std::size_t>(frames) * 8));
}
// Parses the ten header fields from `fields` when the CRC32 that follows them matches.
[[nodiscard]] bool fields_valid(const std::uint8_t* fields) {
    return read_u32(fields + kFieldBytes) == crc32_of(std::span<const std::uint8_t>(fields, kFieldBytes));
}
void parse_fields(const std::uint8_t* fields, Dimensions& dimensions, std::uint32_t& side) {
    dimensions.width = read_u32(fields); dimensions.height = read_u32(fields + 4); dimensions.channels = fields[8]; side = fields[9];
}
// Header: signature, fields and their CRC32 at the front; the fields and CRC32 again at the end of the stream. The
// front copy is used when its checksum matches, otherwise the trailing copy; a stream with neither is rejected.
Status parse_header(std::span<const std::uint8_t> stream, Dimensions& dimensions, std::uint32_t& side) {
    if (stream.size() < kHeaderBytes + kTailBytes) { return Status::bad_header; }
    for (std::size_t index = 0; index < kSignature.size(); ++index) {
        if (stream[index] != kSignature[index]) { return Status::bad_header; }
    }
    const std::uint8_t* front = stream.data() + kSignature.size();
    const std::uint8_t* tail = stream.data() + stream.size() - kTailBytes;
    Dimensions parsed; std::uint32_t parsed_side = 0;
    if (fields_valid(front)) { parse_fields(front, parsed, parsed_side); }
    else if (fields_valid(tail)) { parse_fields(tail, parsed, parsed_side); }
    else { return Status::bad_header; }
    if (parsed_side < kMinFrame || !valid_dimensions(parsed)) { return Status::bad_header; }
    dimensions = parsed; side = parsed_side;
    return Status::ok;
}
// Encodes one frame of `samples` (interleaved image, row stride = image width) into `payload` (cleared first).
Status encode_frame(Workspace& workspace, const std::uint8_t* samples, Dimensions image, FrameRect r, std::vector<std::uint8_t>& payload, std::vector<SymbolTrace>* trace) {
    payload.clear();
    Dimensions frame; frame.width = r.width; frame.height = r.height; frame.channels = image.channels;
    workspace.prepare(frame);
    workspace.place(image.width, r.x0, r.y0, image.width);
    RangeEncoder encoder(payload);
    Direction direction;
    direction.encoder = &encoder;
    direction.trace = trace;
    const std::uint8_t* origin = samples + (static_cast<std::size_t>(r.y0) * image.width + r.x0) * image.channels;
    Status status = process<false>(workspace, direction, origin, nullptr, std::span<const std::uint8_t>{});
    encoder.finish();
    return status;
}
Status decode_frame_payload(Workspace& workspace, std::span<const std::uint8_t> payload, Dimensions image, FrameRect r, std::uint8_t* destination) {
    Dimensions frame; frame.width = r.width; frame.height = r.height; frame.channels = image.channels;
    workspace.prepare(frame);
    workspace.place(image.width, r.x0, r.y0, image.width);
    RangeDecoder decoder(payload);
    Direction direction;
    direction.decoder = &decoder;
    std::uint8_t* origin = destination + (static_cast<std::size_t>(r.y0) * image.width + r.x0) * image.channels;
    return process<true>(workspace, direction, nullptr, origin, payload);
}
Status encode_frames(std::span<const std::uint8_t> samples, Dimensions dimensions, std::uint32_t side, std::vector<std::uint8_t>& destination, std::vector<SymbolTrace>* trace) {
    if (side < kMinFrame || side > 255) { return Status::bad_dimensions; }
    destination.clear();
    if (trace != nullptr) { (*trace).clear(); }
    if (!valid_dimensions(dimensions)) { return Status::bad_dimensions; }
    std::size_t expected = static_cast<std::size_t>(dimensions.width) * dimensions.height * dimensions.channels;
    if (samples.size() != expected) { return Status::bad_sample_count; }
    const std::uint32_t frames = frames_of(dimensions, side);
    std::vector<std::vector<std::uint8_t>> payloads(frames);
    Workspace workspace;   // one per call; prepare() refills it for each frame
    for (std::uint32_t index = 0; index < frames; ++index) {
        Status status = encode_frame(workspace, samples.data(), dimensions, frame_rect(dimensions, side, index), payloads[index], trace);
        if (status != Status::ok) { return status; }
    }
    std::vector<std::uint8_t> fields;
    fields.reserve(kFieldBytes + 4);
    write_u32(fields, dimensions.width);
    write_u32(fields, dimensions.height);
    fields.push_back(static_cast<std::uint8_t>(dimensions.channels));
    fields.push_back(static_cast<std::uint8_t>(side));
    write_u32(fields, crc32_of(std::span<const std::uint8_t>(fields.data(), kFieldBytes)));
    std::vector<std::uint8_t> directory;
    directory.reserve(directory_bytes(frames));
    for (const std::vector<std::uint8_t>& p : payloads) {
        write_u32(directory, static_cast<std::uint32_t>(p.size()));
        write_u32(directory, crc32_of(std::span<const std::uint8_t>(p.data(), p.size())));
    }
    write_u32(directory, crc32_of(std::span<const std::uint8_t>(directory.data(), static_cast<std::size_t>(frames) * 8)));
    std::size_t total = kHeaderBytes + 2 * directory.size() + kTailBytes;
    for (const std::vector<std::uint8_t>& p : payloads) { total += p.size(); }
    destination.reserve(total);
    destination.insert(destination.end(), kSignature.begin(), kSignature.end());
    destination.insert(destination.end(), fields.begin(), fields.end());
    destination.insert(destination.end(), directory.begin(), directory.end());
    for (const std::vector<std::uint8_t>& p : payloads) { destination.insert(destination.end(), p.begin(), p.end()); }
    destination.insert(destination.end(), directory.begin(), directory.end());
    destination.insert(destination.end(), fields.begin(), fields.end());
    return Status::ok;
}
// Reads and validates the directory; fills offsets (from the start of the stream), lengths and checksums of every frame payload.
Status read_directory(std::span<const std::uint8_t> stream, Dimensions dimensions, std::uint32_t side, std::vector<std::size_t>& offsets, std::vector<std::uint32_t>& lengths, std::vector<std::uint32_t>& checksums) {
    const std::uint32_t frames = frames_of(dimensions, side);
    const std::size_t block = directory_bytes(frames);
    if (stream.size() < kHeaderBytes + 2 * block + kTailBytes) { return Status::bad_directory; }
    const std::uint8_t* front = stream.data() + kHeaderBytes;
    const std::uint8_t* mirror = stream.data() + stream.size() - kTailBytes - block;
    const std::uint8_t* directory = directory_valid(front, frames) ? front : directory_valid(mirror, frames) ? mirror : nullptr;
    if (directory == nullptr) { return Status::bad_directory; }
    offsets.assign(frames, 0); lengths.assign(frames, 0); checksums.assign(frames, 0);
    const std::size_t payload_end = stream.size() - kTailBytes - block;
    std::size_t position = kHeaderBytes + block;
    for (std::uint32_t index = 0; index < frames; ++index) {
        lengths[index] = read_u32(directory + static_cast<std::size_t>(index) * 8); checksums[index] = read_u32(directory + static_cast<std::size_t>(index) * 8 + 4);
        offsets[index] = position; position += lengths[index];
        if (position > payload_end) { return Status::bad_directory; }
    }
    if (position != payload_end) { return Status::bad_directory; }
    return Status::ok;
}
}  // namespace

Status encode(std::span<const std::uint8_t> samples, Dimensions dimensions, std::vector<std::uint8_t>& destination) {
    return encode_frames(samples, dimensions, kFrame, destination, nullptr);
}

Status encode_with_frame(std::span<const std::uint8_t> samples, Dimensions dimensions, std::uint32_t frame_side, std::vector<std::uint8_t>& destination) {
    return encode_frames(samples, dimensions, frame_side, destination, nullptr);
}

Status encode_traced(std::span<const std::uint8_t> samples, Dimensions dimensions, std::vector<std::uint8_t>& destination, std::vector<SymbolTrace>& trace) {
    return encode_frames(samples, dimensions, kFrame, destination, &trace);
}

Status read_header(std::span<const std::uint8_t> stream, Dimensions& dimensions) {
    std::uint32_t side = 0;
    return parse_header(stream, dimensions, side);
}


std::size_t frame_workspace_bytes(Dimensions dimensions, std::uint32_t frame_side) {
    // The engine's owned storage for one frame: row rings, column sums and prefixes, row scratch, repetition table
    // and adaptive models, as prepare() allocates them for the frame's width and height.
    std::size_t width = std::min<std::size_t>(frame_side, dimensions.width);
    std::size_t height = std::min<std::size_t>(frame_side, dimensions.height);
    std::size_t planes = static_cast<std::size_t>(dimensions.channels) * 2 + 6;   // samples, cleaned, opponent, cleaned opponent
    std::size_t rings = planes * kWindowRows * width * sizeof(std::int32_t) + 3 * width;
    std::size_t prefixes = kPlaneSlots * (2 * width * sizeof(std::int32_t) + 3 * (width + 1) * sizeof(std::int64_t));
    std::size_t scratch = width * (sizeof(std::int32_t) + sizeof(RepeatKey));
    std::uint64_t capacity = 1ull << kRepeatMinLog;
    while (capacity < (1ull << kRepeatMaxLog) && capacity * 2 < static_cast<std::uint64_t>(width) * height) { capacity *= 2; }
    std::size_t table = static_cast<std::size_t>(capacity) * 2 * sizeof(std::uint32_t);
    std::size_t models = (ZeroFamilies::total + SignFamilies::total + 2 * static_cast<std::size_t>(CoarseFamilies::total) * kPrefixStates) * sizeof(BitModel);
    return rings + prefixes + scratch + table + models;
}

std::uint32_t frame_count(std::span<const std::uint8_t> stream, Dimensions dimensions) {
    Dimensions parsed; std::uint32_t side = 0;
    if (parse_header(stream, parsed, side) != Status::ok) { return 0; }
    return frames_of(dimensions, side);
}

std::uint32_t frame_side(std::span<const std::uint8_t> stream) {
    Dimensions parsed; std::uint32_t side = 0;
    return parse_header(stream, parsed, side) == Status::ok ? side : 0;
}

Status decode_frame(std::span<const std::uint8_t> stream, std::uint32_t index, std::span<std::uint8_t> destination) {
    Dimensions dimensions; std::uint32_t side = 0;
    Status header = parse_header(stream, dimensions, side);
    if (header != Status::ok) { return header; }
    std::size_t expected = static_cast<std::size_t>(dimensions.width) * dimensions.height * dimensions.channels;
    if (destination.size() != expected) { return Status::bad_sample_count; }
    std::vector<std::size_t> offsets; std::vector<std::uint32_t> lengths; std::vector<std::uint32_t> checksums;
    Status directory = read_directory(stream, dimensions, side, offsets, lengths, checksums);
    if (directory != Status::ok) { return directory; }
    if (index >= offsets.size()) { return Status::bad_directory; }
    std::span<const std::uint8_t> payload = stream.subspan(offsets[index], lengths[index]);
    if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }
    Workspace workspace;
    return decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data());
}

Status decode(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination) {
    Dimensions dimensions; std::uint32_t side = 0;
    Status header = parse_header(stream, dimensions, side);
    if (header != Status::ok) { return header; }
    std::size_t expected = static_cast<std::size_t>(dimensions.width) * dimensions.height * dimensions.channels;
    if (destination.size() != expected) { return Status::bad_sample_count; }
    std::vector<std::size_t> offsets; std::vector<std::uint32_t> lengths; std::vector<std::uint32_t> checksums;
    Status directory = read_directory(stream, dimensions, side, offsets, lengths, checksums);
    if (directory != Status::ok) { return directory; }
    Workspace workspace;   // one per call; prepare() refills it for each frame
    for (std::uint32_t index = 0; index < offsets.size(); ++index) {
        std::span<const std::uint8_t> payload = stream.subspan(offsets[index], lengths[index]);
        if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }
        Status status = decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data());
        if (status != Status::ok) { return status; }
    }
    return Status::ok;
}

}  // namespace cng_phase
