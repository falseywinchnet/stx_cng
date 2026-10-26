// The CNG 1.0 encoder. It makes the decisions of the format's Python reference operation for operation, and writes
// byte-identical files where both use the same libzstd:
//   1. every tile (the 128 x 32 segments, then the mosaic of 16 x 16-cell means) is parsed greedily under the older
//      shared-class contexts, and the symbol counts give the first code lengths;
//   2. twice: every tile is planned under the current code lengths (each plane parsed on its own, whole-pixel matches
//      and cache hits where they beat that route, then per-plane matches) and its plane streams emitted; the counts
//      of the emitted symbols give the next code lengths;
//   3. the counts of the last emission give the escaped per-image tables, and every plane stream is rANS coded.
// Code lengths are -log2(f / 4096) from a generated table (code_lengths.hpp), so the parse makes the same decisions on
// every platform. The parse prices in doubles, in the reference's order of operations.
#include "cng_codec.hpp"
#include "cng_format.hpp"
#include "code_lengths.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <thread>
#include <zstd.h>

namespace stx_cng::detail {
namespace {

constexpr double kUnseen = 14.0;                                   // code length of a symbol a table does not list
constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr std::uint32_t kChain = 16;                               // match candidates per 4-gram chain
constexpr std::uint32_t kSufficient = 64;                          // a match this long ends the search at a position
constexpr std::array<std::uint32_t, 9> kTrialLengths = {2, 4, 6, 8, 12, 16, 24, 32, 48};
constexpr std::array<std::uint32_t, 4> kPlaneOrder = {1, 0, 2, 3};   // stream order in a payload: green, blue, red, alpha
constexpr std::uint32_t kMaxChannels = 4;

// ------------------------------------------------------------------ tiles
// One tile's samples, interleaved, rows without padding.
struct Tile {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;
    std::vector<std::uint8_t> samples;

    [[nodiscard]] std::uint32_t pixels() const { return width * height; }
};

// The pixels of the segment at (x0, y0) of the image.
void cut_segment(std::span<const std::uint8_t> image, Dimensions d, std::uint32_t x0, std::uint32_t y0, Tile& tile) {
    tile.width = std::min(kSegmentWidth, d.width - x0);
    tile.height = std::min(kSegmentRows, d.height - y0);
    tile.channels = d.channels;
    const std::size_t row = static_cast<std::size_t>(tile.width) * d.channels;
    tile.samples.resize(row * tile.height);
    for (std::uint32_t y = 0; y < tile.height; ++y) {
        const std::uint8_t* from = image.data() + (static_cast<std::size_t>(y0 + y) * d.width + x0) * d.channels;
        std::copy(from, from + row, tile.samples.begin() + static_cast<std::ptrdiff_t>(y * row));
    }
}

// The mosaic: the rounded mean colour of every 16 x 16 cell over the pixels inside the image.
void block_means(std::span<const std::uint8_t> image, Dimensions d, Tile& tile) {
    const Dimensions m = mosaic_dimensions(d);
    tile.width = m.width; tile.height = m.height; tile.channels = d.channels;
    tile.samples.assign(static_cast<std::size_t>(m.width) * m.height * d.channels, 0);
    std::vector<std::uint64_t> sums(static_cast<std::size_t>(m.width) * d.channels, 0);
    std::vector<std::uint64_t> counts(m.width, 0);
    for (std::uint32_t cy = 0; cy < m.height; ++cy) {
        std::fill(sums.begin(), sums.end(), 0); std::fill(counts.begin(), counts.end(), 0);
        const std::uint32_t y1 = std::min(d.height, (cy + 1) * kCell);
        for (std::uint32_t y = cy * kCell; y < y1; ++y) {
            const std::uint8_t* row = image.data() + static_cast<std::size_t>(y) * d.width * d.channels;
            for (std::uint32_t x = 0; x < d.width; ++x) {
                const std::uint32_t cx = x / kCell;
                for (std::uint32_t c = 0; c < d.channels; ++c) { sums[static_cast<std::size_t>(cx) * d.channels + c] += row[static_cast<std::size_t>(x) * d.channels + c]; }
                ++counts[cx];
            }
        }
        for (std::uint32_t cx = 0; cx < m.width; ++cx) {
            for (std::uint32_t c = 0; c < d.channels; ++c) {
                const std::uint64_t mean = (sums[static_cast<std::size_t>(cx) * d.channels + c] + counts[cx] / 2) / counts[cx];
                tile.samples[(static_cast<std::size_t>(cy) * m.width + cx) * d.channels + c] = static_cast<std::uint8_t>(mean);
            }
        }
    }
}

// ------------------------------------------------------------------ residuals and activity
// MED residuals of every plane under the boundary rules (top row: U = UL = 0, L from the row; left column: L = UL = U),
// and the alpha context (L, U and UL alpha samples equal).
struct Residuals {
    std::array<std::vector<std::int32_t>, kMaxChannels> plane;   // per plane, pixels in raster order
    std::vector<std::uint8_t> quiet;                            // alpha context: 1 when L == U == UL in alpha
};

void residuals_of(const Tile& tile, Residuals& out) {
    const std::uint32_t w = tile.width, h = tile.height, C = tile.channels, n = tile.pixels();
    for (std::uint32_t c = 0; c < C; ++c) { out.plane[c].resize(n); }
    out.quiet.assign(n, 1);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            for (std::uint32_t c = 0; c < C; ++c) {
                const std::int32_t s = tile.samples[i * C + c];
                const std::int32_t up = y >= 1 ? tile.samples[(i - w) * C + c] : 0;
                const std::int32_t lf = x >= 1 ? tile.samples[(i - 1) * C + c] : up;
                const std::int32_t ul = (y >= 1 && x >= 1) ? tile.samples[(i - w - 1) * C + c] : up;
                const std::int32_t hi = std::max(lf, up);
                const std::int32_t lo = std::min(lf, up);
                const std::int32_t med = ul >= hi ? lo : (ul <= lo ? hi : lf + up - ul);
                out.plane[c][i] = wrap(s - med);
                if (c == 3) { out.quiet[i] = (lf == up && up == ul) ? 1 : 0; }
            }
        }
    }
}

// The activity class of every pixel from an energy plane: how many of 0, 3, 12, 48 the sum of the energies of L, LL,
// U, UU, UL and UR exceeds (zero outside the tile).
void classes_of(const std::vector<std::int32_t>& energy, std::uint32_t w, std::uint32_t h, std::vector<std::uint8_t>& out) {
    out.resize(static_cast<std::size_t>(w) * h);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            std::int32_t a = 0;
            if (x >= 1) { a += energy[i - 1]; }
            if (x >= 2) { a += energy[i - 2]; }
            if (y >= 1) {
                a += energy[i - w];
                if (x >= 1) { a += energy[i - w - 1]; }
                if (x + 1 < w) { a += energy[i - w + 1]; }
            }
            if (y >= 2) { a += energy[i - 2 * static_cast<std::size_t>(w)]; }
            out[i] = static_cast<std::uint8_t>((a > 0) + (a > 3) + (a > 12) + (a > 48));
        }
    }
}

[[nodiscard]] std::uint32_t green_bucket(std::int32_t g) { return g == 0 ? 0 : ((g >= -2 && g <= 2) ? 1 : 2); }

// The packed residual key of pixel i: plane c's residual + 128 in bits 8c..8c+7.
[[nodiscard]] std::uint32_t packed_key(const Residuals& r, std::uint32_t C, std::size_t i) {
    std::uint32_t key = 0;
    for (std::uint32_t c = 0; c < C; ++c) { key |= static_cast<std::uint32_t>(r.plane[c][i] + 128) << (8 * c); }
    return key;
}

// The colour of pixel i: sample c in bits 8c..8c+7.
[[nodiscard]] std::uint32_t colour_key(const Tile& tile, std::size_t i) {
    std::uint32_t key = 0;
    for (std::uint32_t c = 0; c < tile.channels; ++c) { key |= static_cast<std::uint32_t>(tile.samples[i * tile.channels + c]) << (8 * c); }
    return key;
}

// ------------------------------------------------------------------ symbol counts and code lengths
struct Counts {
    std::vector<std::uint64_t> value = std::vector<std::uint64_t>(static_cast<std::size_t>(kContexts) * kGreenSymbols, 0);

    void add(std::uint32_t context, std::uint32_t symbol) { ++value[static_cast<std::size_t>(context) * kGreenSymbols + symbol]; }
    [[nodiscard]] std::uint64_t at(std::uint32_t context, std::uint32_t symbol) const { return value[static_cast<std::size_t>(context) * kGreenSymbols + symbol]; }
};

// q bytes of every entry of `counts` (0 for an unused entry): 1 + round(2 log2 f), f = count * 4096 / total, clamped to
// [0, 24], and 1 for f below 1. Rounding is to nearest, ties to even.
void quantize(std::span<const std::uint64_t> counts, std::vector<std::uint8_t>& q) {
    std::uint64_t total = 0;
    for (std::uint64_t c : counts) { total += c; }
    q.assign(counts.size(), 0);
    if (total == 0) { return; }
    for (std::size_t k = 0; k < counts.size(); ++k) {
        if (counts[k] == 0) { continue; }
        const double f = static_cast<double>(counts[k] * kTotal) / static_cast<double>(total);
        if (f < 1.0) { q[k] = 1; continue; }
        const double level = std::nearbyint(2.0 * std::log2(f));
        q[k] = static_cast<std::uint8_t>(1 + static_cast<std::int32_t>(std::min(24.0, std::max(0.0, level))));
    }
}

// Code length of every (context, symbol): -log2(f / 4096) under the quantized table of the counts, kUnseen elsewhere.
struct CodeLengths {
    std::vector<double> value = std::vector<double>(static_cast<std::size_t>(kContexts) * kGreenSymbols, kUnseen);

    [[nodiscard]] const double* row(std::uint32_t context) const { return value.data() + static_cast<std::size_t>(context) * kGreenSymbols; }
};

void code_lengths_of(const Counts& counts, CodeLengths& out) {
    std::fill(out.value.begin(), out.value.end(), kUnseen);
    std::vector<std::uint8_t> q;
    std::vector<std::uint16_t> frequency;
    for (std::uint32_t k = 0; k < kContexts; ++k) {
        std::span<const std::uint64_t> row(counts.value.data() + static_cast<std::size_t>(k) * kGreenSymbols, kGreenSymbols);
        quantize(row, q);
        normalize(q, frequency);
        for (std::size_t s = 0; s < frequency.size(); ++s) {
            if (frequency[s] != 0) { out.value[static_cast<std::size_t>(k) * kGreenSymbols + s] = kCodeLength[frequency[s]]; }
        }
    }
}

// ------------------------------------------------------------------ distances
// The last three distances used, most recent first (a repeat moves to the front, a new distance pushes the last out).
struct Repeats {
    std::array<std::uint32_t, kRepeats> distance = {1, 1, 2};

    void reset(std::uint32_t width) { distance = {1, width, 2}; }

    [[nodiscard]] std::int32_t index_of(std::uint32_t d) const {
        for (std::uint32_t k = 0; k < kRepeats; ++k) { if (distance[k] == d) { return static_cast<std::int32_t>(k); } }
        return -1;
    }

    void update(std::uint32_t d) {
        std::int32_t found = index_of(d);
        std::uint32_t from = found >= 0 ? static_cast<std::uint32_t>(found) : kRepeats - 1;
        for (std::uint32_t k = from; k > 0; --k) { distance[k] = distance[k - 1]; }
        distance[0] = d;
    }
};

// The six special distances of a tile of width w: 1, w, w - 1, w + 1, 2, 2w.
[[nodiscard]] std::array<std::uint32_t, kSpecials> specials_of(std::uint32_t w) { return {1, w, w - 1, w + 1, 2, 2 * w}; }

[[nodiscard]] std::int32_t special_index(const std::array<std::uint32_t, kSpecials>& special, std::uint32_t d) {
    for (std::uint32_t k = 0; k < kSpecials; ++k) { if (special[k] == d) { return static_cast<std::int32_t>(k); } }
    return -1;
}

// ------------------------------------------------------------------ 4-gram chains
struct Gram {
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    [[nodiscard]] bool operator==(const Gram& other) const { return low == other.low && high == other.high; }
};

struct GramHash {
    [[nodiscard]] std::size_t operator()(const Gram& g) const {
        std::uint64_t h = g.low * 0x9E3779B97F4A7C15ull ^ (g.high + 0x632BE59BD9B4E019ull + (g.low << 6) + (g.low >> 2));
        return static_cast<std::size_t>(h ^ (h >> 31));
    }
};

// Positions of every 4-gram of a sequence seen so far. The parse reads only the newest kChain positions of a 4-gram,
// newest first (the reference keeps a list that drops its oldest 16 when it exceeds 32, which never touches those),
// so each 4-gram keeps a ring of its last kChain positions. Open addressing; entries of earlier parses are retired by
// a generation number instead of being cleared.
struct ChainEntry {
    Gram gram;
    std::uint32_t generation = 0;
    std::uint32_t count = 0;                       // positions inserted for this 4-gram in this parse
    std::array<std::uint32_t, kChain> ring{};      // position k is at ring[k % kChain]
};

struct Chains {
    std::vector<ChainEntry> entries;
    std::uint64_t mask = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] static Gram gram_at(const std::vector<std::uint32_t>& S, std::uint32_t j) {
        Gram g;
        g.low = static_cast<std::uint64_t>(S[j]) | (static_cast<std::uint64_t>(S[j + 1]) << 32);
        g.high = static_cast<std::uint64_t>(S[j + 2]) | (static_cast<std::uint64_t>(S[j + 3]) << 32);
        return g;
    }

    // Starts a parse of a sequence of n positions: at most n distinct 4-grams, so at most half the table is used.
    void reset(std::uint32_t n) {
        std::uint64_t capacity = 64;
        while (capacity < 2 * static_cast<std::uint64_t>(n)) { capacity *= 2; }
        if (entries.size() < capacity || generation == 0xFFFFFFFFu) {
            entries.assign(static_cast<std::size_t>(std::max<std::uint64_t>(capacity, entries.size())), ChainEntry{});
            generation = 0;
        }
        mask = entries.size() - 1;
        ++generation;
    }

    [[nodiscard]] std::uint64_t slot_of(const Gram& g) const { return static_cast<std::uint64_t>(GramHash{}(g)) & mask; }

    void insert(const std::vector<std::uint32_t>& S, std::uint32_t n, std::uint32_t j) {
        if (j + 4 > n) { return; }
        const Gram g = gram_at(S, j);
        std::uint64_t slot = slot_of(g);
        while (entries[slot].generation == generation && !(entries[slot].gram == g)) { slot = (slot + 1) & mask; }
        ChainEntry& e = entries[slot];
        if (e.generation != generation) { e.gram = g; e.generation = generation; e.count = 0; }
        e.ring[e.count % kChain] = j;
        ++e.count;
    }

    // The entry of the 4-gram at position i (nullptr when none).
    [[nodiscard]] const ChainEntry* at(const std::vector<std::uint32_t>& S, std::uint32_t n, std::uint32_t i) const {
        if (i + 4 > n) { return nullptr; }
        const Gram g = gram_at(S, i);
        std::uint64_t slot = slot_of(g);
        while (entries[slot].generation == generation) {
            if (entries[slot].gram == g) { return &entries[slot]; }
            slot = (slot + 1) & mask;
        }
        return nullptr;
    }
};

void append_unique(std::vector<std::uint32_t>& list, std::uint32_t d) {
    for (std::uint32_t v : list) { if (v == d) { return; } }
    list.push_back(d);
}

// ------------------------------------------------------------------ cost-aware parse
struct Match {
    std::uint32_t position = 0;
    std::uint32_t distance = 0;
    std::uint32_t length = 0;
};

struct Step {
    std::uint32_t from = 0;
    std::uint32_t distance = 0;   // 0 for a literal step
};

// Code lengths of the 30 length codes at each position: row(context[i]) + base.
struct LengthCosts {
    const CodeLengths* lengths = nullptr;
    const std::vector<std::uint8_t>* context = nullptr;
    std::uint32_t base = 0;

    [[nodiscard]] const double* at(std::uint32_t i) const { return lengths->row((*context)[i]) + base; }
};

struct ParseWorkspace {
    std::vector<std::uint32_t> limit_at;
    std::vector<double> price;
    std::vector<Step> back;
    std::vector<Repeats> state;
    std::vector<std::uint32_t> candidates;
    Chains chains;
};

// The cheapest parse of S: lit[i] the cost of position i without a match; a stop position takes no match and no match
// crosses it. Returns the matches and, per position, its share of the cost of the step that covers it.
void costed_parse(const std::vector<std::uint32_t>& S, std::uint32_t n, std::uint32_t w, const std::vector<double>& lit, const LengthCosts& lengths,
                  const double* distance, const std::vector<std::uint8_t>& stop, ParseWorkspace& ws, std::vector<Match>& matches,
                  std::vector<double>* amortized) {
    const std::array<std::uint32_t, kSpecials> special = specials_of(w);
    ws.limit_at.assign(static_cast<std::size_t>(n) + 1, n);
    std::uint32_t next_stop = n;
    for (std::uint32_t k = n; k > 0; --k) {
        const std::uint32_t i = k - 1;
        if (stop[i] != 0) { next_stop = i; }
        ws.limit_at[i] = next_stop;
    }
    ws.price.assign(static_cast<std::size_t>(n) + 1, kInfinity);
    ws.price[0] = 0.0;
    ws.back.assign(static_cast<std::size_t>(n) + 1, Step{});
    ws.state.resize(static_cast<std::size_t>(n) + 1);
    ws.state[0].reset(w);
    ws.chains.reset(n);
    std::uint32_t i = 0;
    while (i < n) {
        const double base = ws.price[i];
        const Repeats reps = ws.state[i];
        if (base + lit[i] < ws.price[i + 1]) {
            ws.price[i + 1] = base + lit[i]; ws.back[i + 1] = Step{i, 0}; ws.state[i + 1] = reps;
        }
        if (stop[i] != 0) { ws.chains.insert(S, n, i); ++i; continue; }
        std::uint32_t longest = 0;
        const double* lcost = lengths.at(i);
        const std::uint32_t cap = std::min(ws.limit_at[i] - i, kMaximumLength);
        ws.candidates.clear();
        for (std::uint32_t d : reps.distance) { if (d >= 1 && d <= i) { append_unique(ws.candidates, d); } }
        for (std::uint32_t d : special) { if (d >= 1 && d <= i) { append_unique(ws.candidates, d); } }
        const ChainEntry* chain = ws.chains.at(S, n, i);
        if (chain != nullptr) {
            const std::uint32_t newest = std::min(chain->count, kChain);
            for (std::uint32_t t = 0; t < newest; ++t) { append_unique(ws.candidates, i - chain->ring[(chain->count - 1 - t) % kChain]); }
        }
        for (std::uint32_t d : ws.candidates) {
            if (S[i] != S[i - d]) { continue; }
            std::uint32_t L = 1;
            const std::uint32_t lim = std::min(cap, kSufficient);
            while (L < lim && S[i + L] == S[i + L - d]) { ++L; }
            if (L == kSufficient) { while (L < cap && S[i + L] == S[i + L - d]) { ++L; } }
            const std::int32_t repeat = reps.index_of(d);
            const std::uint32_t shortest = repeat >= 0 ? kMinimumLength : kNewMinimumLength;
            if (L < shortest) { continue; }
            const std::int32_t sp = special_index(special, d);
            if (repeat < 0 && sp < 0 && d > kMaximumNewDistance) { continue; }   // not expressible by a distance code
            double dcost = 0.0;
            if (repeat >= 0) { dcost = distance[repeat]; }
            else if (sp >= 0) { dcost = distance[kRepeats + static_cast<std::uint32_t>(sp)]; }
            else {
                const PrefixCode p = prefix(d - 1);
                dcost = distance[kRepeats + kSpecials + p.code] + static_cast<double>(p.bits);
            }
            Repeats moved = reps;
            moved.update(d);
            for (std::uint32_t t = 0; t <= kTrialLengths.size(); ++t) {
                const std::uint32_t trial = t < kTrialLengths.size() ? kTrialLengths[t] : L;
                if (t < kTrialLengths.size() && !(shortest <= trial && trial < L)) { continue; }
                const PrefixCode p = prefix(trial - kMinimumLength);
                const double v = base + lcost[p.code] + static_cast<double>(p.bits) + dcost;
                if (v < ws.price[i + trial]) { ws.price[i + trial] = v; ws.back[i + trial] = Step{i, d}; ws.state[i + trial] = moved; }
            }
            if (L > longest) { longest = L; }
        }
        if (longest >= kSufficient) {
            for (std::uint32_t j = i; j < i + longest; ++j) { ws.chains.insert(S, n, j); }
            i += longest;
            continue;
        }
        ws.chains.insert(S, n, i);
        ++i;
    }
    matches.clear();
    if (amortized != nullptr) { amortized->assign(n, 0.0); }
    std::uint32_t j = n;
    while (j > 0) {
        const Step step = ws.back[j];
        if (step.distance != 0) { matches.push_back(Match{step.from, step.distance, j - step.from}); }
        if (amortized != nullptr) {
            const double share = (ws.price[j] - ws.price[step.from]) / static_cast<double>(j - step.from);
            for (std::uint32_t q = step.from; q < j; ++q) { (*amortized)[q] = share; }
        }
        j = step.from;
    }
    std::reverse(matches.begin(), matches.end());
}

// ------------------------------------------------------------------ operations
// A coding operation: a table symbol (context, symbol) or extra bits (count, value).
struct Op {
    std::uint16_t first = 0;    // context, or bit count
    std::uint16_t second = 0;   // symbol, or value
    bool bits = false;
};

void distance_ops(std::vector<Op>& ops, std::uint32_t d, Repeats& reps, const std::array<std::uint32_t, kSpecials>& special) {
    const std::int32_t repeat = reps.index_of(d);
    const std::int32_t sp = special_index(special, d);
    if (repeat >= 0) { ops.push_back(Op{static_cast<std::uint16_t>(kDistanceContext), static_cast<std::uint16_t>(repeat), false}); }
    else if (sp >= 0) { ops.push_back(Op{static_cast<std::uint16_t>(kDistanceContext), static_cast<std::uint16_t>(kRepeats + static_cast<std::uint32_t>(sp)), false}); }
    else {
        const PrefixCode p = prefix(d - 1);
        ops.push_back(Op{static_cast<std::uint16_t>(kDistanceContext), static_cast<std::uint16_t>(kRepeats + kSpecials + p.code), false});
        if (p.bits != 0) { ops.push_back(Op{static_cast<std::uint16_t>(p.bits), static_cast<std::uint16_t>(p.extra), true}); }
    }
    reps.update(d);
}

void length_ops(std::vector<Op>& ops, std::uint32_t context, std::uint32_t base, std::uint32_t length) {
    const PrefixCode p = prefix(length - kMinimumLength);
    ops.push_back(Op{static_cast<std::uint16_t>(context), static_cast<std::uint16_t>(base + p.code), false});
    if (p.bits != 0) { ops.push_back(Op{static_cast<std::uint16_t>(p.bits), static_cast<std::uint16_t>(p.extra), true}); }
}

// ------------------------------------------------------------------ the greedy first pass (initial code lengths)
// The shared-class view of the first pass: one class per pixel from max(|b|, |g|, |r|), contexts and literals as the
// plane contexts with that class, a 256-slot colour cache hashed by colour.
void greedy_counts(const Tile& tile, Counts& counts, Residuals& r, Chains& chains) {
    const std::uint32_t w = tile.width, h = tile.height, C = tile.channels, n = tile.pixels();
    residuals_of(tile, r);
    std::vector<std::int32_t> energy(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::int32_t e = 0;
        for (std::uint32_t c = 0; c < 3; ++c) { e = std::max(e, std::abs(r.plane[c][i])); }
        energy[i] = e;
    }
    std::vector<std::uint8_t> K;
    classes_of(energy, w, h, K);
    std::vector<std::uint32_t> P(n);
    for (std::uint32_t i = 0; i < n; ++i) { P[i] = packed_key(r, C, i); }
    std::vector<std::int32_t> hits(n, -1);
    std::array<std::int64_t, kCacheSize> cache;
    cache.fill(-1);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t key = colour_key(tile, i);
        const std::uint32_t slot = (key * 0x1E35A7BDu) >> 24;
        if (cache[slot] == static_cast<std::int64_t>(key)) { hits[i] = static_cast<std::int32_t>(slot); }
        cache[slot] = key;
    }
    const std::array<std::uint32_t, kSpecials> special = specials_of(w);
    Repeats repeats;
    repeats.reset(w);
    chains.reset(n);
    std::vector<std::uint32_t> candidates;
    std::uint32_t i = 0;
    while (i < n) {
        std::uint32_t best_length = 0, best_d = 0;
        const std::uint32_t limit = std::min(kMaximumLength, n - i);
        candidates.clear();
        for (std::uint32_t d : repeats.distance) { if (d >= 1 && d <= i) { candidates.push_back(d); } }
        for (std::uint32_t d : special) { if (d >= 1 && d <= i) { candidates.push_back(d); } }
        const ChainEntry* chain = chains.at(P, n, i);
        if (chain != nullptr) {
            const std::uint32_t newest = std::min(chain->count, kChain);
            for (std::uint32_t t = 0; t < newest; ++t) { candidates.push_back(i - chain->ring[(chain->count - 1 - t) % kChain]); }
        }
        for (std::uint32_t d : candidates) {
            if (P[i] != P[i - d]) { continue; }
            std::uint32_t length = 1;
            while (length < limit && P[i + length] == P[i + length - d]) { ++length; }
            if (length < (repeats.index_of(d) >= 0 ? kMinimumLength : kNewMinimumLength)) { continue; }
            if (length > best_length) { best_length = length; best_d = d; }
            if (length == limit) { break; }
        }
        if (best_length != 0) {
            const PrefixCode p = prefix(best_length - kMinimumLength);
            counts.add(K[i], kWholeLength0 + p.code);
            const std::int32_t repeat = repeats.index_of(best_d);
            const std::int32_t sp = special_index(special, best_d);
            if (repeat >= 0) { counts.add(kDistanceContext, static_cast<std::uint32_t>(repeat)); }
            else if (sp >= 0) { counts.add(kDistanceContext, kRepeats + static_cast<std::uint32_t>(sp)); }
            else { counts.add(kDistanceContext, kRepeats + kSpecials + prefix(best_d - 1).code); }
            repeats.update(best_d);
            for (std::uint32_t j = i; j < i + best_length; ++j) { chains.insert(P, n, j); }
            i += best_length;
            continue;
        }
        if (hits[i] >= 0) { counts.add(K[i], kCache0 + static_cast<std::uint32_t>(hits[i])); }
        else {
            const std::uint32_t k = K[i];
            const std::int32_t g = r.plane[1][i];
            std::int32_t blue = r.plane[0][i], red = r.plane[2][i];
            if (static_cast<std::int32_t>(k) >= kGate) { blue = wrap(blue - g); red = wrap(red - g); }
            const std::uint32_t gb = green_bucket(g);
            counts.add(k, zig(g));
            counts.add(kBlue0 + 3 * k + gb, zig(blue));
            counts.add(kRed0 + 3 * k + gb, zig(red));
            if (C == 4) { counts.add(kAlpha0 + (r.quiet[i] != 0 ? 0 : 1), zig(r.plane[3][i])); }
        }
        chains.insert(P, n, i);
        ++i;
    }
}

// ------------------------------------------------------------------ the plane view, plan and emission
// Per-plane classes (green from |g|, blue from max(|g|, |b|), red from max(|g|, |r|)), contexts, literals, sequences
// for matching, and the green-keyed cache: per pixel, zigzag(green residual) when the last earlier pixel with its green
// sample has its whole colour, else -1.
struct View {
    std::uint32_t width = 0, height = 0, channels = 0, pixels = 0;
    std::array<std::vector<std::uint8_t>, kMaxChannels> context;
    std::array<std::vector<std::uint16_t>, kMaxChannels> literal;
    std::array<std::vector<std::uint32_t>, kMaxChannels> plane;   // residual + 128
    std::vector<std::uint32_t> packed;
    std::vector<std::int32_t> hits;
};

void view_of(const Tile& tile, Residuals& r, View& v) {
    const std::uint32_t w = tile.width, h = tile.height, C = tile.channels, n = tile.pixels();
    v.width = w; v.height = h; v.channels = C; v.pixels = n;
    residuals_of(tile, r);
    std::vector<std::int32_t> eg(n), eb(n), er(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t ag = std::abs(r.plane[1][i]);
        eg[i] = ag;
        eb[i] = std::max(ag, std::abs(r.plane[0][i]));
        er[i] = std::max(ag, std::abs(r.plane[2][i]));
    }
    std::vector<std::uint8_t> kg, kb, kr;
    classes_of(eg, w, h, kg); classes_of(eb, w, h, kb); classes_of(er, w, h, kr);
    for (std::uint32_t c = 0; c < C; ++c) {
        v.context[c].resize(n); v.literal[c].resize(n); v.plane[c].resize(n);
        for (std::uint32_t i = 0; i < n; ++i) { v.plane[c][i] = static_cast<std::uint32_t>(r.plane[c][i] + 128); }
    }
    v.packed.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int32_t g = r.plane[1][i];
        std::int32_t blue = r.plane[0][i], red = r.plane[2][i];
        if (kb[i] >= kGate) { blue = wrap(blue - g); }
        if (kr[i] >= kGate) { red = wrap(red - g); }
        const std::uint32_t gb = green_bucket(g);
        v.context[1][i] = kg[i]; v.literal[1][i] = static_cast<std::uint16_t>(zig(g));
        v.context[0][i] = static_cast<std::uint8_t>(kBlue0 + 3 * kb[i] + gb); v.literal[0][i] = static_cast<std::uint16_t>(zig(blue));
        v.context[2][i] = static_cast<std::uint8_t>(kRed0 + 3 * kr[i] + gb); v.literal[2][i] = static_cast<std::uint16_t>(zig(red));
        if (C == 4) { v.context[3][i] = static_cast<std::uint8_t>(kAlpha0 + (r.quiet[i] != 0 ? 0 : 1)); v.literal[3][i] = static_cast<std::uint16_t>(zig(r.plane[3][i])); }
        v.packed[i] = packed_key(r, C, i);
    }
    v.hits.assign(n, -1);
    std::array<std::int32_t, 256> last;
    last.fill(-1);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t green = tile.samples[static_cast<std::size_t>(i) * C + 1];
        const std::int32_t j = last[green];
        if (j >= 0 && colour_key(tile, static_cast<std::size_t>(j)) == colour_key(tile, i)) { v.hits[i] = static_cast<std::int32_t>(zig(r.plane[1][i])); }
        last[green] = static_cast<std::int32_t>(i);
    }
}

[[nodiscard]] std::uint32_t plane_length0(std::uint32_t c) { return c == 1 ? kGreenOnly0 : kPlaneLength0; }

struct Plan {
    std::vector<Match> whole;
    std::vector<std::uint8_t> cached;
    std::array<std::vector<Match>, kMaxChannels> per_plane;
};

struct PlanWorkspace {
    ParseWorkspace parse;
    std::array<std::vector<double>, kMaxChannels> plane_lit;
    std::vector<double> cache_cost, plane_route, amortized, lit;
    std::vector<std::uint8_t> open, stop, covered;
    std::vector<Match> discard;
};

void plan_of(const View& v, const CodeLengths& costs, PlanWorkspace& ws, Plan& plan) {
    const std::uint32_t n = v.pixels, w = v.width, C = v.channels;
    const double* distance = costs.row(kDistanceContext);
    for (std::uint32_t c = 0; c < C; ++c) {
        ws.plane_lit[c].resize(n);
        for (std::uint32_t i = 0; i < n; ++i) { ws.plane_lit[c][i] = costs.row(v.context[c][i])[v.literal[c][i]]; }
    }
    ws.cache_cost.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        ws.cache_cost[i] = v.hits[i] >= 0 ? costs.row(v.context[1][i])[kCache0 + static_cast<std::uint32_t>(v.hits[i])] : kInfinity;
    }
    ws.open.assign(n, 0);
    ws.plane_route.assign(n, 0.0);
    for (std::uint32_t c = 0; c < C; ++c) {
        LengthCosts lengths{&costs, &v.context[c], plane_length0(c)};
        costed_parse(v.plane[c], n, w, ws.plane_lit[c], lengths, distance, ws.open, ws.parse, ws.discard, &ws.amortized);
        for (std::uint32_t i = 0; i < n; ++i) { ws.plane_route[i] += ws.amortized[i]; }
    }
    ws.lit.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) { ws.lit[i] = std::min(ws.plane_route[i], ws.cache_cost[i]); }
    LengthCosts green_lengths{&costs, &v.context[1], kWholeLength0};
    costed_parse(v.packed, n, w, ws.lit, green_lengths, distance, ws.open, ws.parse, plan.whole, nullptr);
    ws.covered.assign(n, 0);
    for (const Match& m : plan.whole) { for (std::uint32_t j = m.position; j < m.position + m.length; ++j) { ws.covered[j] = 1; } }
    plan.cached.assign(n, 0);
    ws.stop.assign(n, 0);
    for (std::uint32_t i = 0; i < n; ++i) {
        plan.cached[i] = (ws.covered[i] == 0 && ws.cache_cost[i] < ws.plane_route[i]) ? 1 : 0;
        ws.stop[i] = (ws.covered[i] != 0 || plan.cached[i] != 0) ? 1 : 0;
    }
    for (std::uint32_t c = 0; c < C; ++c) {
        for (std::uint32_t i = 0; i < n; ++i) { ws.lit[i] = ws.stop[i] != 0 ? 0.0 : ws.plane_lit[c][i]; }
        LengthCosts lengths{&costs, &v.context[c], plane_length0(c)};
        costed_parse(v.plane[c], n, w, ws.lit, lengths, distance, ws.stop, ws.parse, plan.per_plane[c], nullptr);
    }
}

// The operations of every plane stream of a tile, in plane index order (blue, green, red, alpha).
void emit_streams(const View& v, const Plan& plan, std::array<std::vector<Op>, kMaxChannels>& streams, std::vector<std::uint8_t>& covered) {
    const std::uint32_t n = v.pixels, w = v.width, C = v.channels;
    const std::array<std::uint32_t, kSpecials> special = specials_of(w);
    std::vector<std::uint32_t> whole_length(n, 0), whole_distance(n, 0);
    covered.assign(n, 0);
    for (const Match& m : plan.whole) {
        whole_length[m.position] = m.length; whole_distance[m.position] = m.distance;
        for (std::uint32_t j = m.position; j < m.position + m.length; ++j) { covered[j] = 1; }
    }
    std::vector<std::uint32_t> plane_length(n), plane_distance(n);
    for (std::uint32_t c = 0; c < C; ++c) {
        std::vector<Op>& ops = streams[c];
        ops.clear();
        std::fill(plane_length.begin(), plane_length.end(), 0);
        for (const Match& m : plan.per_plane[c]) { plane_length[m.position] = m.length; plane_distance[m.position] = m.distance; }
        Repeats reps, whole_reps;
        reps.reset(w); whole_reps.reset(w);
        std::uint32_t until = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            if (c == 1 && whole_length[i] != 0) {
                length_ops(ops, v.context[1][i], kWholeLength0, whole_length[i]);
                distance_ops(ops, whole_distance[i], whole_reps, special);
                continue;
            }
            if (covered[i] != 0) { continue; }
            if (plan.cached[i] != 0) {
                if (c == 1) { ops.push_back(Op{v.context[1][i], static_cast<std::uint16_t>(kCache0 + static_cast<std::uint32_t>(v.hits[i])), false}); }
                continue;
            }
            if (until > i) { continue; }
            if (plane_length[i] != 0) {
                length_ops(ops, v.context[c][i], plane_length0(c), plane_length[i]);
                distance_ops(ops, plane_distance[i], reps, special);
                until = i + plane_length[i];
            } else {
                ops.push_back(Op{v.context[c][i], v.literal[c][i], false});
            }
        }
    }
}

void count_streams(const std::array<std::vector<Op>, kMaxChannels>& streams, std::uint32_t C, Counts& counts) {
    for (std::uint32_t c = 0; c < C; ++c) {
        for (const Op& op : streams[c]) { if (!op.bits) { counts.add(op.first, op.second); } }
    }
}

// ------------------------------------------------------------------ tables and rANS
struct EncodeTable {
    std::vector<std::uint16_t> frequency;
    std::vector<std::uint32_t> start;
    std::uint32_t listed = 0;   // symbols below this with a count above kEscapeMaximum are coded directly; the escape is entry `listed`
};

// Per context: the q bytes of the listed symbols followed by the escape entry, and the table they normalise to.
void escape_tables(const Counts& counts, std::vector<std::vector<std::uint8_t>>& qs, std::vector<EncodeTable>& tables) {
    qs.assign(kContexts, {}); tables.assign(kContexts, EncodeTable{});
    std::vector<std::uint64_t> vector;
    for (std::uint32_t k = 0; k < kContexts; ++k) {
        std::uint32_t listed = 0;
        std::uint64_t total = 0;
        for (std::uint32_t s = 0; s < kGreenSymbols; ++s) {
            const std::uint64_t c = counts.at(k, s);
            total += c;
            if (c > kEscapeMaximum) { listed = s + 1; }
        }
        vector.assign(static_cast<std::size_t>(listed) + 1, 0);
        std::uint64_t kept = 0;
        for (std::uint32_t s = 0; s < listed; ++s) {
            const std::uint64_t c = counts.at(k, s);
            if (c > kEscapeMaximum) { vector[s] = c; kept += c; }
        }
        vector[listed] = total - kept;
        quantize(vector, qs[k]);
        EncodeTable& t = tables[k];
        t.listed = listed;
        normalize(qs[k], t.frequency);
        t.start.assign(t.frequency.size(), 0);
        std::uint32_t accumulated = 0;
        for (std::size_t s = 0; s < t.frequency.size(); ++s) { t.start[s] = accumulated; accumulated += t.frequency[s]; }
    }
}

// Replaces every symbol a table does not list (beyond its listed range, or used at most kEscapeMaximum times) by the
// escape symbol followed by the symbol in raw bits.
void escape_ops(const std::vector<Op>& ops, const Counts& counts, const std::vector<EncodeTable>& tables, std::vector<Op>& out) {
    out.clear();
    for (const Op& op : ops) {
        if (!op.bits && (op.second >= tables[op.first].listed || counts.at(op.first, op.second) <= kEscapeMaximum)) {
            out.push_back(Op{op.first, static_cast<std::uint16_t>(tables[op.first].listed), false});
            out.push_back(Op{static_cast<std::uint16_t>(raw_bits(op.first)), op.second, true});
        } else {
            out.push_back(op);
        }
    }
}

// rANS (32-bit state, 12-bit frequencies, byte renormalisation) over the operations in reverse; the stream is the
// final state, big-endian, then the renormalisation bytes in decoding order.
void rans_encode(const std::vector<Op>& ops, const std::vector<EncodeTable>& tables, std::vector<std::uint8_t>& out, std::vector<std::uint8_t>& scratch) {
    std::uint32_t x = kLower;
    scratch.clear();
    for (std::size_t k = ops.size(); k > 0; --k) {
        const Op& op = ops[k - 1];
        std::uint32_t f = 0, start = 0;
        if (!op.bits) { f = tables[op.first].frequency[op.second]; start = tables[op.first].start[op.second]; }
        else { f = kTotal >> op.first; start = static_cast<std::uint32_t>(op.second) * f; }
        const std::uint64_t limit = static_cast<std::uint64_t>((kLower >> kPrecision) << 8) * f;
        while (x >= limit) { scratch.push_back(static_cast<std::uint8_t>(x & 255u)); x >>= 8; }
        x = ((x / f) << kPrecision) + (x % f) + start;
    }
    out.clear();
    append_u32(out, x);
    for (std::size_t k = scratch.size(); k > 0; --k) { out.push_back(scratch[k - 1]); }
}

void append_leb128(std::vector<std::uint8_t>& out, std::uint64_t value) {
    while (true) {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        out.push_back(static_cast<std::uint8_t>(byte | (value != 0 ? 0x80 : 0)));
        if (value == 0) { return; }
    }
}

// A tile payload: the lengths of the first C - 1 plane streams as LEB128, then green, blue, red, alpha.
struct PackWorkspace {
    std::vector<Op> escaped;
    std::array<std::vector<std::uint8_t>, kMaxChannels> coded;
    std::vector<std::uint8_t> scratch;
};

void pack_streams(const std::array<std::vector<Op>, kMaxChannels>& streams, std::uint32_t C, const Counts& counts, const std::vector<EncodeTable>& tables,
                  PackWorkspace& ws, std::vector<std::uint8_t>& payload) {
    for (std::uint32_t k = 0; k < C; ++k) {
        escape_ops(streams[kPlaneOrder[k]], counts, tables, ws.escaped);
        rans_encode(ws.escaped, tables, ws.coded[k], ws.scratch);
    }
    payload.clear();
    for (std::uint32_t k = 0; k + 1 < C; ++k) { append_leb128(payload, ws.coded[k].size()); }
    for (std::uint32_t k = 0; k < C; ++k) { payload.insert(payload.end(), ws.coded[k].begin(), ws.coded[k].end()); }
}

// The tables body (per context: the q count minus one as u16, then the q bytes) in one standard zstd frame at level
// 19 without content size, checksum or dictionary id.
[[nodiscard]] bool serialize_tables(const std::vector<std::vector<std::uint8_t>>& qs, std::vector<std::uint8_t>& blob) {
    std::vector<std::uint8_t> body;
    for (const std::vector<std::uint8_t>& q : qs) {
        const std::uint32_t count = static_cast<std::uint32_t>(q.size()) - 1;
        body.push_back(static_cast<std::uint8_t>(count >> 8)); body.push_back(static_cast<std::uint8_t>(count));
        body.insert(body.end(), q.begin(), q.end());
    }
    ZSTD_CCtx* context = ZSTD_createCCtx();
    if (context == nullptr) { return false; }
    bool ok = !ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, kTablesLevel))
              && !ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, 0))
              && !ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 0))
              && !ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_dictIDFlag, 0));
    if (ok) {
        blob.resize(ZSTD_compressBound(body.size()));
        const std::size_t written = ZSTD_compress2(context, blob.data(), blob.size(), body.data(), body.size());
        ok = !ZSTD_isError(written);
        if (ok) { blob.resize(written); }
    }
    ZSTD_freeCCtx(context);
    return ok;
}

// ------------------------------------------------------------------ the encoder
struct TileStreams {
    std::uint32_t channels = 0;
    std::array<std::vector<Op>, kMaxChannels> stream;
};

// Tile `index` of the image: segments in raster order, then the mosaic.
void tile_at(std::span<const std::uint8_t> samples, Dimensions d, std::uint64_t index, std::uint64_t segments, Tile& tile) {
    if (index == segments) { block_means(samples, d, tile); return; }
    const std::uint64_t across = (static_cast<std::uint64_t>(d.width) + kSegmentWidth - 1) / kSegmentWidth;
    const std::uint32_t x0 = static_cast<std::uint32_t>(index % across) * kSegmentWidth;
    const std::uint32_t y0 = static_cast<std::uint32_t>(index / across) * kSegmentRows;
    cut_segment(samples, d, x0, y0, tile);
}

// The work of one encoder pass over the tiles assigned to one worker (tiles first, first + stride, ...).
enum class Pass : std::uint8_t { greedy, plan, pack };

struct EncodeJob {
    std::span<const std::uint8_t> samples;
    Dimensions dimensions;
    std::uint64_t segments = 0;
    std::uint64_t first = 0;
    std::uint64_t stride = 1;
    Pass pass = Pass::greedy;
    const CodeLengths* costs = nullptr;                 // plan
    const Counts* final_counts = nullptr;               // pack
    const std::vector<EncodeTable>* tables = nullptr;   // pack
    std::vector<TileStreams>* streams = nullptr;        // plan: written; pack: read and released
    std::vector<std::vector<std::uint8_t>>* payloads = nullptr;   // pack: one per tile, the mosaic last
    Counts counts;                                      // greedy and plan: this worker's symbol counts
};

void run_encode_job(EncodeJob& job) {
    Tile tile;
    Residuals residuals;
    View view;
    Plan plan;
    PlanWorkspace plan_ws;
    PackWorkspace pack;
    std::vector<std::uint8_t> covered;
    const std::uint64_t tiles = job.segments + 1;
    for (std::uint64_t t = job.first; t < tiles; t += job.stride) {
        const std::size_t index = static_cast<std::size_t>(t);
        if (job.pass == Pass::pack) {
            pack_streams((*job.streams)[index].stream, job.dimensions.channels, *job.final_counts, *job.tables, pack, (*job.payloads)[index]);
            std::array<std::vector<Op>, kMaxChannels>().swap((*job.streams)[index].stream);   // release
            continue;
        }
        tile_at(job.samples, job.dimensions, t, job.segments, tile);
        if (job.pass == Pass::greedy) { greedy_counts(tile, job.counts, residuals, plan_ws.parse.chains); continue; }
        view_of(tile, residuals, view);
        plan_of(view, *job.costs, plan_ws, plan);
        TileStreams& out = (*job.streams)[index];
        out.channels = tile.channels;
        emit_streams(view, plan, out.stream, covered);
        count_streams(out.stream, tile.channels, job.counts);
    }
}

// Runs one pass over every tile with `workers` threads and sums their counts into `counts` (when counting).
void run_pass(std::vector<EncodeJob>& jobs, Counts* counts) {
    if (jobs.size() == 1) { run_encode_job(jobs[0]); }
    else {
        std::vector<std::thread> pool;
        for (EncodeJob& job : jobs) { pool.emplace_back(run_encode_job, std::ref(job)); }
        for (std::thread& worker : pool) { worker.join(); }
    }
    if (counts == nullptr) { return; }
    *counts = Counts{};
    for (const EncodeJob& job : jobs) {
        for (std::size_t k = 0; k < counts->value.size(); ++k) { counts->value[k] += job.counts.value[k]; }
    }
}

}  // namespace

Status encode(std::span<const std::uint8_t> samples, Dimensions d, std::vector<std::uint8_t>& file, std::uint32_t threads) {
    if (d.width == 0 || d.height == 0 || (d.channels != 3 && d.channels != 4)) { return Status::bad_dimensions; }
    if (static_cast<std::uint64_t>(d.width) * d.height * d.channels > kMaxSamples) { return Status::bad_dimensions; }
    if (samples.size() != static_cast<std::size_t>(d.width) * d.height * d.channels) { return Status::bad_sample_count; }
    const std::uint64_t segments = segment_count(d);
    const std::uint64_t tiles = segments + 1;   // the segments, then the mosaic
    const std::uint64_t workers = std::max<std::uint64_t>(1, std::min<std::uint64_t>(threads, tiles));
    std::vector<TileStreams> streams(static_cast<std::size_t>(tiles));
    std::vector<std::vector<std::uint8_t>> payloads(static_cast<std::size_t>(tiles));
    std::vector<EncodeJob> jobs(static_cast<std::size_t>(workers));
    for (std::uint64_t k = 0; k < workers; ++k) {
        EncodeJob& job = jobs[static_cast<std::size_t>(k)];
        job.samples = samples; job.dimensions = d; job.segments = segments; job.first = k; job.stride = workers;
        job.streams = &streams; job.payloads = &payloads;
    }
    // 1. the greedy first pass
    Counts counts;
    for (EncodeJob& job : jobs) { job.pass = Pass::greedy; }
    run_pass(jobs, &counts);
    CodeLengths costs;
    code_lengths_of(counts, costs);
    // 2. two planned emissions; the second one's streams are coded
    for (std::uint32_t pass = 0; pass < 2; ++pass) {
        for (EncodeJob& job : jobs) { job.pass = Pass::plan; job.costs = &costs; job.counts = Counts{}; }
        run_pass(jobs, &counts);
        if (pass == 0) { code_lengths_of(counts, costs); }
    }
    // 3. tables and payloads
    std::vector<std::vector<std::uint8_t>> qs;
    std::vector<EncodeTable> tables;
    escape_tables(counts, qs, tables);
    std::vector<std::uint8_t> blob;
    if (!serialize_tables(qs, blob)) { return Status::bad_tables; }
    for (EncodeJob& job : jobs) { job.pass = Pass::pack; job.final_counts = &counts; job.tables = &tables; }
    run_pass(jobs, nullptr);
    std::vector<std::uint8_t> mosaic = std::move(payloads.back());
    payloads.pop_back();
    // 4. the container
    std::vector<std::uint8_t> fields;
    append_u32(fields, d.width); append_u32(fields, d.height);
    fields.push_back(static_cast<std::uint8_t>(d.channels)); fields.push_back(kVersion);
    fields.push_back(static_cast<std::uint8_t>(kSegmentWidth)); fields.push_back(static_cast<std::uint8_t>(kSegmentRows));
    append_u32(fields, static_cast<std::uint32_t>(mosaic.size())); append_u32(fields, crc_of(mosaic));
    append_u32(fields, static_cast<std::uint32_t>(blob.size())); append_u32(fields, crc_of(blob));
    append_u32(fields, crc_of(fields));
    std::vector<std::uint8_t> directory;
    for (const std::vector<std::uint8_t>& p : payloads) { append_u32(directory, static_cast<std::uint32_t>(p.size())); append_u32(directory, crc_of(p)); }
    append_u32(directory, crc_of(directory));
    file.clear();
    file.insert(file.end(), kSignature.begin(), kSignature.end());
    file.insert(file.end(), fields.begin(), fields.end());
    file.insert(file.end(), directory.begin(), directory.end());
    file.insert(file.end(), blob.begin(), blob.end());
    file.insert(file.end(), mosaic.begin(), mosaic.end());
    for (const std::vector<std::uint8_t>& p : payloads) { file.insert(file.end(), p.begin(), p.end()); }
    file.insert(file.end(), mosaic.begin(), mosaic.end());
    if (segments > 1) { file.insert(file.end(), blob.begin(), blob.end()); }
    file.insert(file.end(), directory.begin(), directory.end());
    file.insert(file.end(), fields.begin(), fields.end());
    return Status::ok;
}

}  // namespace stx_cng::detail
