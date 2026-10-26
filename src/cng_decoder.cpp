// The CNG 1.0 decoder; see cng_codec.hpp and docs/FORMAT.md.
#include "cng_internal.hpp"

#include <algorithm>
#include <array>
#include <thread>
#include <zstd.h>

namespace stx_cng::detail {
namespace {

void build_table(std::span<const std::uint8_t> q, Table& table) {
    normalize(q, table.frequency);
    table.escape = static_cast<std::uint32_t>(q.size()) - 1;
    table.used = !table.frequency.empty();
    if (!table.used) { return; }   // never decoded: symbol() fails on an unused table before reading it
    table.start.assign(table.frequency.size(), 0);
    table.slot.resize(kTotal);
    std::uint32_t accumulated = 0;
    for (std::size_t s = 0; s < table.frequency.size(); ++s) {
        table.start[s] = static_cast<std::uint16_t>(accumulated);
        std::fill_n(table.slot.begin() + accumulated, table.frequency[s], static_cast<std::uint16_t>(s));
        accumulated += table.frequency[s];
    }
}

// ------------------------------------------------------------------ rANS
struct RangeDecoder {
    std::span<const std::uint8_t> data;
    std::size_t position = 4;
    std::uint32_t state = 0;
    bool failed = false;

    explicit RangeDecoder(std::span<const std::uint8_t> source) : data(source) {
        if (source.size() < 4) { failed = true; return; }
        state = read_u32(source.data());
    }

    void renormalize(std::uint32_t next) {
        while (next < kLower) {
            if (position >= data.size()) { failed = true; next = kLower; break; }
            next = (next << 8) | data[position];
            ++position;
        }
        state = next;
    }

    [[nodiscard]] std::uint32_t symbol(const Table& table) {
        if (!table.used) { failed = true; return 0; }
        std::uint32_t slot = state & (kTotal - 1);
        std::uint32_t s = table.slot[slot];
        std::uint32_t next = table.frequency[s] * (state >> kPrecision) + slot - table.start[s];
        renormalize(next);
        return s;
    }

    [[nodiscard]] std::uint32_t bits(std::uint32_t count) {
        if (count == 0) { return 0; }
        if (count > kPrecision) { failed = true; return 0; }
        std::uint32_t slot = state & (kTotal - 1);
        std::uint32_t frequency = kTotal >> count;
        std::uint32_t value = slot / frequency;
        std::uint32_t next = frequency * (state >> kPrecision) + slot - value * frequency;
        renormalize(next);
        return value;
    }
};

// ------------------------------------------------------------------ tile decoding
struct Repeats {
    std::array<std::uint32_t, kRepeats> distance;

    void reset(std::uint32_t width) { distance = {1, width, 2}; }

    // Move-to-front of the distance just used; a new distance pushes the last one out.
    void update(std::uint32_t d) {
        std::uint32_t found = kRepeats;
        for (std::uint32_t k = 0; k < kRepeats; ++k) {
            if (distance[k] == d) { found = k; break; }
        }
        std::uint32_t last = found == kRepeats ? kRepeats - 1 : found;
        for (std::uint32_t k = last; k > 0; --k) { distance[k] = distance[k - 1]; }
        distance[0] = d;
    }
};

struct TileDecoder {
    const Table* tables;   // the image's tables, indexed by context
    RangeDecoder range;
    std::uint32_t width;
    std::uint32_t pixels;
    std::array<std::uint32_t, kSpecials> special;
    bool failed = false;

    TileDecoder(const std::vector<Table>& t, std::span<const std::uint8_t> payload, std::uint32_t w, std::uint32_t n)
        : tables(t.data()), range(payload), width(w), pixels(n), special{1, w, w - 1, w + 1, 2, 2 * w} {}

    [[nodiscard]] std::uint32_t symbol(std::uint32_t context) {
        const Table& table = tables[context];
        std::uint32_t value = range.symbol(table);
        if (value == table.escape) { value = range.bits(raw_bits(context)); }
        return value;
    }

    // Reads a match's length extra bits and distance; returns the distance (0 on failure) and sets `length`.
    [[nodiscard]] std::uint32_t match(std::uint32_t code, Repeats& repeats, std::uint32_t i, std::uint32_t& length) {
        if (code >= kLengthCodes) { failed = true; return 0; }
        length = kMinimumLength + unprefix(code, range.bits(code_bits(code)));
        std::uint32_t ds = symbol(kDistanceContext);
        std::uint32_t d = 0;
        if (ds < kRepeats) { d = repeats.distance[ds]; }
        else if (ds < kRepeats + kSpecials) { d = special[ds - kRepeats]; }
        else {
            std::uint32_t distance_code = ds - kRepeats - kSpecials;
            if (distance_code >= kLengthCodes) { failed = true; return 0; }
            d = 1 + unprefix(distance_code, range.bits(code_bits(distance_code)));
        }
        if (d < 1 || d > i || length > pixels - i) { failed = true; return 0; }
        repeats.update(d);
        return d;
    }
};

}  // namespace

namespace {

// `signature_required` is false for salvage, where a damaged signature is tolerated: the checksummed header fields
// still decide whether the stream can be read.
[[nodiscard]] Status read_container_checked(std::span<const std::uint8_t> stream, Container& container, bool signature_required) {
    if (stream.size() < kHeaderBytes + kTailBytes) { return Status::bad_header; }
    for (std::size_t k = 0; signature_required && k < kSignature.size(); ++k) {
        if (stream[k] != kSignature[k]) { return Status::bad_header; }
    }
    std::span<const std::uint8_t> front = stream.subspan(kSignature.size(), kFieldBytes + 4);
    std::span<const std::uint8_t> tail = stream.subspan(stream.size() - kTailBytes, kTailBytes);
    const std::uint8_t* fields = nullptr;
    if (block_valid(front, kFieldBytes)) { fields = front.data(); }
    else if (block_valid(tail, kFieldBytes)) { fields = tail.data(); }
    else { return Status::bad_header; }
    Dimensions d;
    d.width = read_u32(fields); d.height = read_u32(fields + 4); d.channels = fields[8];
    const std::uint32_t version = fields[9];
    const std::uint32_t segment_width = fields[10];
    const std::uint32_t segment_rows = fields[11];
    std::uint64_t mosaic_length = read_u32(fields + 12); std::uint32_t mosaic_crc = read_u32(fields + 16);
    std::uint64_t tables_length = read_u32(fields + 20); std::uint32_t tables_crc = read_u32(fields + 24);
    if (version != kVersion || segment_width != kSegmentWidth || segment_rows != kSegmentRows) { return Status::unsupported_version; }
    if (d.width == 0 || d.height == 0 || (d.channels != 3 && d.channels != 4)) { return Status::bad_header; }
    if (static_cast<std::uint64_t>(d.width) * d.height * d.channels > kMaxSamples) { return Status::bad_header; }
    const std::uint32_t side = kSegmentWidth;
    std::uint64_t across = (static_cast<std::uint64_t>(d.width) + side - 1) / side;
    std::uint64_t down = (static_cast<std::uint64_t>(d.height) + kSegmentRows - 1) / kSegmentRows;
    std::uint64_t tiles = across * down;
    std::uint64_t block = 8 * tiles + 4;
    std::uint64_t copies = tiles == 1 ? 1 : 2;
    std::uint64_t size = stream.size();
    if (size < kHeaderBytes + 2 * block + 2 * mosaic_length + copies * tables_length + kTailBytes) { return Status::bad_directory; }
    std::span<const std::uint8_t> first = stream.subspan(kHeaderBytes, block);
    std::span<const std::uint8_t> mirror = stream.subspan(size - kTailBytes - block, block);
    const std::uint8_t* directory = nullptr;
    if (block_valid(first, 8 * tiles)) { directory = first.data(); }
    else if (block_valid(mirror, 8 * tiles)) { directory = mirror.data(); }
    else { return Status::bad_directory; }
    std::uint64_t second = copies == 2 ? tables_length : 0;
    std::uint64_t end = size - kTailBytes - block - second - mosaic_length;
    std::uint64_t start = kHeaderBytes + block + tables_length + mosaic_length;
    if (start > end) { return Status::bad_directory; }
    std::span<const std::uint8_t> tables_front = stream.subspan(kHeaderBytes + block, tables_length);
    if (crc_of(tables_front) == tables_crc) { container.tables = tables_front; }
    else if (copies == 2 && crc_of(stream.subspan(end + mosaic_length, tables_length)) == tables_crc) {
        container.tables = stream.subspan(end + mosaic_length, tables_length);
    } else { return Status::bad_tables; }
    std::span<const std::uint8_t> mosaic_front = stream.subspan(kHeaderBytes + block + tables_length, mosaic_length);
    std::span<const std::uint8_t> mosaic_back = stream.subspan(end, mosaic_length);
    if (crc_of(mosaic_front) == mosaic_crc) { container.mosaic = mosaic_front; }
    else if (crc_of(mosaic_back) == mosaic_crc) { container.mosaic = mosaic_back; }
    else { container.mosaic = {}; }
    container.payloads.assign(tiles, {});
    container.payload_intact.assign(tiles, 0);
    std::uint64_t position = start;
    for (std::uint64_t index = 0; index < tiles; ++index) {
        std::uint64_t length = read_u32(directory + 8 * index);
        std::uint32_t checksum = read_u32(directory + 8 * index + 4);
        std::uint64_t available = position < end ? std::min<std::uint64_t>(length, end - position) : 0;
        container.payloads[index] = stream.subspan(std::min<std::uint64_t>(position, end), available);
        container.payload_intact[index] = available == length && crc_of(container.payloads[index]) == checksum ? 1 : 0;
        position += length;
    }
    if (position != end) { return Status::bad_directory; }
    container.dimensions = d;
    container.tile_side = side;
    container.segment_rows = kSegmentRows;
    container.tiles_across = static_cast<std::uint32_t>(across);
    return Status::ok;
}

}  // namespace

Status read_container(std::span<const std::uint8_t> stream, Container& container) {
    return read_container_checked(stream, container, true);
}

Status read_dimensions(std::span<const std::uint8_t> stream, Dimensions& dimensions) {
    Container container;
    Status status = read_container(stream, container);
    if (status == Status::ok) { dimensions = container.dimensions; }
    return status;
}

Status read_tables(std::span<const std::uint8_t> frame, std::vector<Table>& tables) {
    std::size_t capacity = kContexts * (3 + kGreenSymbols);
    std::vector<std::uint8_t> body(capacity);
    ZSTD_DCtx* context = ZSTD_createDCtx();
    if (context == nullptr) { return Status::bad_tables; }
    std::size_t count = ZSTD_decompressDCtx(context, body.data(), capacity, frame.data(), frame.size());   // one standard zstd frame
    ZSTD_freeDCtx(context);
    if (ZSTD_isError(count)) { return Status::bad_tables; }
    tables.assign(kContexts, Table{});
    std::size_t position = 0;
    for (std::uint32_t k = 0; k < kContexts; ++k) {
        if (position + 2 > count) { return Status::bad_tables; }
        std::size_t listed = (static_cast<std::size_t>(body[position]) << 8) | body[position + 1];
        position += 2;
        if (listed > kGreenSymbols || position + listed + 1 > count) { return Status::bad_tables; }
        for (std::size_t e = 0; e <= listed; ++e) {
            if (body[position + e] > kLevels.size()) { return Status::bad_tables; }   // quantized frequencies are 0 to 25
        }
        build_table(std::span<const std::uint8_t>(body.data() + position, listed + 1), tables[k]);
        position += listed + 1;
    }
    if (position != count) { return Status::bad_tables; }
    return Status::ok;
}

namespace {

constexpr std::uint32_t kMaxChannels = 4;

[[nodiscard]] bool read_leb128(std::span<const std::uint8_t> data, std::size_t& position, std::uint64_t& value) {
    value = 0;
    std::uint32_t shift = 0;
    while (true) {
        if (position >= data.size() || shift > 28) { return false; }
        std::uint8_t byte = data[position];
        ++position;
        value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) { return true; }
    }
}

// The streams of a tile payload: the lengths of the first C - 1 streams as LEB128, then green, blue, red, alpha.
[[nodiscard]] bool split_streams(std::span<const std::uint8_t> payload, std::uint32_t C, std::array<std::span<const std::uint8_t>, kMaxChannels>& streams) {
    const std::array<std::uint32_t, kMaxChannels> order = {1, 0, 2, 3};
    std::array<std::uint64_t, kMaxChannels> lengths = {0, 0, 0, 0};
    std::size_t position = 0;
    std::uint64_t listed = 0;
    for (std::uint32_t k = 0; k + 1 < C; ++k) {
        if (!read_leb128(payload, position, lengths[k])) { return false; }
        listed += lengths[k];
    }
    if (position + listed > payload.size()) { return false; }
    lengths[C - 1] = payload.size() - position - listed;
    for (std::uint32_t k = 0; k < C; ++k) {
        streams[order[k]] = payload.subspan(position, lengths[k]);
        position += lengths[k];
    }
    return true;
}

// Activity classes. A pixel's three energies (green |g|, blue max(|g|, |b|), red max(|g|, |r|)), each at most 255, are
// packed into one 64-bit word, sixteen bits each. Six of them sum to at most 1530, so the fields add without carries
// and one word addition sums all three planes; a 2048-entry table maps a field's sum to its class.
constexpr std::uint32_t kField = 16;
constexpr std::uint64_t kFieldMask = (1u << kField) - 1;
constexpr std::uint32_t kClassTable = 2048;   // larger than the largest six-term sum, 6 * 255

constexpr std::array<std::uint8_t, kClassTable> make_classes() {
    std::array<std::uint8_t, kClassTable> table{};
    for (std::uint32_t a = 0; a < table.size(); ++a) { table[a] = static_cast<std::uint8_t>((a > 0) + (a > 3) + (a > 12) + (a > 48)); }
    return table;
}
constexpr std::array<std::uint8_t, kClassTable> kClassOf = make_classes();


// The green bucket of the blue and red contexts, indexed by green residual + 128: 0 for zero, 1 for |g| <= 2, else 2.
constexpr std::array<std::uint8_t, 256> make_green_buckets() {
    std::array<std::uint8_t, 256> table{};
    for (std::int32_t g = -128; g < 128; ++g) { table[static_cast<std::size_t>(g + 128)] = static_cast<std::uint8_t>(g == 0 ? 0 : ((g >= -2 && g <= 2) ? 1 : 2)); }
    return table;
}
constexpr std::array<std::uint8_t, 256> kGreenBucket = make_green_buckets();

// Plane c's residual from a pixel's packed residual word.
[[nodiscard]] std::int32_t lane(std::uint32_t packed, std::uint32_t c) {
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(packed >> (8 * c)));
}

// The packed energies of a pixel from its green, blue (plane 0) and red (plane 2) residuals.
[[nodiscard]] std::uint64_t energy_word(std::int32_t g, std::int32_t b, std::int32_t r) {
    const std::uint64_t gm = static_cast<std::uint64_t>(g >= 0 ? g : -g);
    const std::uint64_t bm = static_cast<std::uint64_t>(b >= 0 ? b : -b);
    const std::uint64_t rm = static_cast<std::uint64_t>(r >= 0 ? r : -r);
    const std::uint64_t eb = bm > gm ? bm : gm;
    const std::uint64_t er = rm > gm ? rm : gm;
    return gm | (eb << kField) | (er << (2 * kField));
}

// The decoders of a tile's plane streams, one per plane; a missing alpha plane gets a decoder over an empty stream that
// is never used.
[[nodiscard]] std::array<TileDecoder, kMaxChannels> plane_decoders(const std::vector<Table>& tables, const std::array<std::span<const std::uint8_t>, kMaxChannels>& streams,
                                                                  std::uint32_t w, std::uint32_t n) {
    return {TileDecoder(tables, streams[0], w, n), TileDecoder(tables, streams[1], w, n), TileDecoder(tables, streams[2], w, n), TileDecoder(tables, streams[3], w, n)};
}

// Per tile: one rANS stream and decoder per plane, residual planes, a ring of three padded energy rows (two zero words
// before each row, one after, so neighbours outside the tile read zero without tests), the row-above activity sums,
// the last pixel of every green value, and the match state.
//
// The state owns no allocation (the storage is the caller's Workspace), so its address never escapes the inlined
// pixel loop. The sample and residual stores are byte stores, which may alias any memory whose address has escaped;
// state that cannot be aliased stays in registers across them instead of being reloaded after every store.
struct TileState {
    std::array<TileDecoder, kMaxChannels> decoders;   // indexed by plane
    const std::uint32_t w, h, C, n;
    std::uint32_t* residual;             // per pixel: the four plane residuals as bytes, plane c in bits 8c..8c+7
    std::uint64_t* ring;                 // three rows of w + 3 words
    std::uint64_t* energy_row;           // current row's x = 0 word
    std::uint64_t* above;                // per x: U + UL + UR of row y - 1 and UU of row y - 2, all three planes
    Repeats whole_repeats;
    std::array<Repeats, 4> plane_repeats;
    std::uint32_t whole_left = 0, whole_distance = 0;
    std::array<std::uint32_t, 4> left = {0, 0, 0, 0};
    std::array<std::uint32_t, 4> distance = {0, 0, 0, 0};
    std::array<std::int32_t, 256> last;
    std::uint8_t* destination;
    std::size_t row_bytes;

    TileState(const std::vector<Table>& tables, const std::array<std::span<const std::uint8_t>, kMaxChannels>& streams, Dimensions tile,
              Workspace& workspace, std::uint8_t* out, std::size_t stride)
        : decoders(plane_decoders(tables, streams, tile.width, tile.width * tile.height)), w(tile.width), h(tile.height), C(tile.channels),
          n(tile.width * tile.height), residual(nullptr), ring(nullptr), energy_row(nullptr), above(nullptr), destination(out), row_bytes(stride) {
        workspace.residuals.resize(n);
        workspace.energy.assign(3 * (static_cast<std::size_t>(w) + 3), 0u);
        residual = workspace.residuals.data();
        ring = workspace.energy.data();
        workspace.above.resize(w);
        above = workspace.above.data();
        whole_repeats.reset(w);
        for (Repeats& r : plane_repeats) { r.reset(w); }
        last.fill(-1);
    }

    [[nodiscard]] std::uint64_t* ring_row(std::uint32_t slot) const { return ring + static_cast<std::size_t>(slot) * (w + 3) + 2; }

    // Rows y - 1 and y - 2 occupy the other two ring slots; before row 2 those slots still hold zeros.
    void prepare_row(std::uint32_t y) {
        energy_row = ring_row(y % 3);
        const std::uint64_t* e1 = ring_row((y + 2) % 3);
        const std::uint64_t* e2 = ring_row((y + 1) % 3);
        for (std::uint32_t x = 0; x < w; ++x) { above[x] = e1[static_cast<std::ptrdiff_t>(x) - 1] + e1[x] + e1[x + 1] + e2[x]; }
    }

    [[nodiscard]] bool failed() const {
        for (std::uint32_t c = 0; c < C; ++c) { if (decoders[c].failed || decoders[c].range.failed) { return true; } }
        return false;
    }

    // A blue, red or alpha symbol from that plane's stream: a literal or the start of a match of that plane alone.
    [[nodiscard]] bool plane_symbol(std::uint32_t c, std::uint32_t context, std::uint32_t i, std::int32_t k, std::int32_t g, std::int32_t& out) {
        TileDecoder& decoder = decoders[c];
        std::uint32_t v = decoder.symbol(context);
        if (v >= kPlaneLength0) {
            std::uint32_t length = 0;
            distance[c] = decoder.match(v - kPlaneLength0, plane_repeats[c], i, length);
            if (decoder.failed) { return false; }
            left[c] = length - 1;
            out = lane(residual[i - distance[c]], c);
            return true;
        }
        std::int32_t value = unzig(v);
        out = (c != 3 && k >= kGate) ? wrap(value + g) : value;
        return true;
    }

    template <bool Edge, std::uint32_t Channels>
    [[nodiscard]] bool pixel(std::uint32_t x, std::uint32_t y, std::uint8_t* row, const std::uint8_t* above_samples) {
        constexpr std::uint32_t Count = Channels;
        const std::uint32_t i = y * w + x;
        std::array<std::int32_t, 4> med = {0, 0, 0, 0};
        bool alpha_quiet = true;
        for (std::uint32_t c = 0; c < Count; ++c) {
            std::int32_t up = 0, lf = 0, ul = 0;
            if constexpr (Edge) {
                up = above_samples != nullptr ? above_samples[static_cast<std::size_t>(x) * Count + c] : 0;
                lf = x >= 1 ? row[static_cast<std::size_t>(x - 1) * Count + c] : up;
                ul = (above_samples != nullptr && x >= 1) ? above_samples[static_cast<std::size_t>(x - 1) * Count + c] : up;
            } else {
                up = above_samples[static_cast<std::size_t>(x) * Count + c];
                lf = row[static_cast<std::size_t>(x - 1) * Count + c];
                ul = above_samples[static_cast<std::size_t>(x - 1) * Count + c];
            }
            std::int32_t hi = lf > up ? lf : up;
            std::int32_t lo = lf > up ? up : lf;
            med[c] = ul >= hi ? lo : (ul <= lo ? hi : lf + up - ul);
            if (c == 3) { alpha_quiet = lf == up && up == ul; }
        }
        std::array<std::int32_t, 4> res = {0, 0, 0, 0};
        if (whole_left != 0) {
            const std::uint32_t copied = residual[i - whole_distance];
            for (std::uint32_t c = 0; c < Count; ++c) { res[c] = lane(copied, c); }
            --whole_left;
            finish<Count>(x, i, row, med, res);
            return true;
        }
        const std::uint64_t activity = above[x] + energy_row[static_cast<std::ptrdiff_t>(x) - 1] + energy_row[static_cast<std::ptrdiff_t>(x) - 2];
        std::int32_t g = 0;
        if (left[1] != 0) { g = lane(residual[i - distance[1]], 1); --left[1]; }
        else {
            TileDecoder& green = decoders[1];
            const std::uint32_t gs = green.symbol(kClassOf[activity & kFieldMask]);
            if (gs < kWholeLength0) { g = unzig(gs); }
            else if (gs < kCache0) {
                std::uint32_t length = 0;
                whole_distance = green.match(gs - kWholeLength0, whole_repeats, i, length);
                if (green.failed) { return false; }
                whole_left = length - 1;
                const std::uint32_t copied = residual[i - whole_distance];
            for (std::uint32_t c = 0; c < Count; ++c) { res[c] = lane(copied, c); }
                finish<Count>(x, i, row, med, res);
                return true;
            } else if (gs < kGreenOnly0) {
                g = unzig(gs - kCache0);
                const std::int32_t source = last[static_cast<std::uint32_t>(med[1] + g) & 255u];
                if (source < 0) { return false; }
                finish_cached<Count>(x, i, row, med, g, static_cast<std::uint32_t>(source));
                return true;
            } else {
                std::uint32_t length = 0;
                distance[1] = green.match(gs - kGreenOnly0, plane_repeats[1], i, length);
                if (green.failed) { return false; }
                left[1] = length - 1;
                g = lane(residual[i - distance[1]], 1);
            }
        }
        res[1] = g;
        const std::uint32_t bucket = kGreenBucket[static_cast<std::uint32_t>(g + 128)];   // g lies in [-128, 127]
        if (left[0] != 0) { res[0] = lane(residual[i - distance[0]], 0); --left[0]; }
        else {
            const std::int32_t k = kClassOf[(activity >> kField) & kFieldMask];
            if (!plane_symbol(0, kBlue0 + 3 * static_cast<std::uint32_t>(k) + bucket, i, k, g, res[0])) { return false; }
        }
        if (left[2] != 0) { res[2] = lane(residual[i - distance[2]], 2); --left[2]; }
        else {
            const std::int32_t k = kClassOf[(activity >> (2 * kField)) & kFieldMask];
            if (!plane_symbol(2, kRed0 + 3 * static_cast<std::uint32_t>(k) + bucket, i, k, g, res[2])) { return false; }
        }
        if constexpr (Count == 4) {
            if (left[3] != 0) { res[3] = lane(residual[i - distance[3]], 3); --left[3]; }
            else if (!plane_symbol(3, kAlpha0 + (alpha_quiet ? 0 : 1), i, 0, g, res[3])) { return false; }
        }
        finish<Count>(x, i, row, med, res);
        return true;
    }

    // Reconstructs pixel i from its predictions and residuals; stores samples, residuals and energies, and records the
    // pixel's green value. Stream failures are checked once per row: every decoded symbol is bounded by its table, and
    // match extents are checked where they are read.
    template <std::uint32_t Count>
    void finish(std::uint32_t x, std::uint32_t i, std::uint8_t* row, const std::array<std::int32_t, 4>& med, const std::array<std::int32_t, 4>& res) {
        std::uint32_t packed = 0;
        for (std::uint32_t c = 0; c < Count; ++c) {
            row[static_cast<std::size_t>(x) * Count + c] = static_cast<std::uint8_t>(med[c] + res[c]);
            packed |= (static_cast<std::uint32_t>(res[c]) & 255u) << (8 * c);
        }
        residual[i] = packed;
        energy_row[x] = energy_word(res[1], res[0], res[2]);
        last[static_cast<std::uint32_t>(med[1] + res[1]) & 255u] = static_cast<std::int32_t>(i);
    }

    // A cache hit: green from its residual, the other samples copied from pixel `source` (the last earlier pixel with
    // this green value) and their residuals derived from them.
    template <std::uint32_t Count>
    void finish_cached(std::uint32_t x, std::uint32_t i, std::uint8_t* row, const std::array<std::int32_t, 4>& med, std::int32_t g, std::uint32_t source) {
        const std::uint8_t* from = destination + static_cast<std::size_t>(source / w) * row_bytes + static_cast<std::size_t>(source % w) * Count;
        std::array<std::int32_t, 4> res = {0, g, 0, 0};
        std::uint32_t packed = 0;
        for (std::uint32_t c = 0; c < Count; ++c) {
            const std::int32_t sample = c == 1 ? ((med[1] + g) & 255) : from[c];
            if (c != 1) { res[c] = wrap(sample - med[c]); }
            row[static_cast<std::size_t>(x) * Count + c] = static_cast<std::uint8_t>(sample);
            packed |= (static_cast<std::uint32_t>(res[c]) & 255u) << (8 * c);
        }
        residual[i] = packed;
        energy_row[x] = energy_word(res[1], res[0], res[2]);
        last[static_cast<std::uint32_t>(med[1] + g) & 255u] = static_cast<std::int32_t>(i);
    }

    [[nodiscard]] bool complete() const {
        if (whole_left != 0 || left[0] != 0 || left[1] != 0 || left[2] != 0 || left[3] != 0) { return false; }
        for (std::uint32_t c = 0; c < C; ++c) {
            const TileDecoder& d = decoders[c];
            if (d.range.position != d.range.data.size() || d.range.state != kLower || d.range.failed || d.failed) { return false; }
        }
        return true;
    }
};

template <std::uint32_t C>
[[nodiscard]] bool decode_row(TileState& state, std::uint32_t y, std::uint8_t* row, const std::uint8_t* above) {
    state.prepare_row(y);
    if (y == 0) {
        for (std::uint32_t x = 0; x < state.w; ++x) { if (!state.pixel<true, C>(x, y, row, above)) { return false; } }
        return !state.failed();
    }
    if (!state.pixel<true, C>(0, y, row, above)) { return false; }
    for (std::uint32_t x = 1; x < state.w; ++x) { if (!state.pixel<false, C>(x, y, row, above)) { return false; } }
    return !state.failed();
}

}  // namespace

Status decode_tile(const std::vector<Table>& tables, std::span<const std::uint8_t> payload, Dimensions tile,
                   Workspace& workspace, std::uint8_t* destination, std::size_t row_bytes) {
    std::array<std::span<const std::uint8_t>, kMaxChannels> streams;
    if (!split_streams(payload, tile.channels, streams)) { return Status::bad_payload; }
    TileState state(tables, streams, tile, workspace, destination, row_bytes);
    for (std::uint32_t y = 0; y < tile.height; ++y) {
        std::uint8_t* row = destination + static_cast<std::size_t>(y) * row_bytes;
        const std::uint8_t* above = y >= 1 ? row - row_bytes : nullptr;
        bool ok = tile.channels == 4 ? decode_row<4>(state, y, row, above) : decode_row<3>(state, y, row, above);
        if (!ok) { return Status::bad_payload; }
    }
    return state.complete() ? Status::ok : Status::bad_payload;
}

namespace {

struct TileJob {
    const Container* container = nullptr;
    const std::vector<Table>* tables = nullptr;
    std::uint8_t* destination = nullptr;
    std::uint32_t first = 0;
    std::uint32_t stride = 1;
    Status status = Status::ok;
};

void run_tiles(TileJob& job) {
    const Container& container = *job.container;
    const Dimensions d = container.dimensions;
    const std::size_t row_bytes = static_cast<std::size_t>(d.width) * d.channels;
    Workspace workspace;
    for (std::uint32_t index = job.first; index < container.payloads.size(); index += job.stride) {
        if (container.payload_intact[index] == 0) { job.status = Status::bad_payload; return; }
        std::uint32_t x0 = (index % container.tiles_across) * container.tile_side;
        std::uint32_t y0 = (index / container.tiles_across) * container.segment_rows;
        Dimensions tile;
        tile.width = std::min(container.tile_side, d.width - x0);
        tile.height = std::min(container.segment_rows, d.height - y0);
        tile.channels = d.channels;
        std::uint8_t* origin = job.destination + static_cast<std::size_t>(y0) * row_bytes + static_cast<std::size_t>(x0) * d.channels;
        Status status = decode_tile(*job.tables, container.payloads[index], tile, workspace, origin, row_bytes);
        if (status != Status::ok) { job.status = status; return; }
    }
}

}  // namespace

Status decode(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination, std::uint32_t threads) {
    Container container;
    Status status = read_container(stream, container);
    if (status != Status::ok) { return status; }
    const Dimensions d = container.dimensions;
    if (destination.size() != static_cast<std::size_t>(d.width) * d.height * d.channels) { return Status::bad_sample_count; }
    std::vector<Table> tables;
    status = read_tables(container.tables, tables);
    if (status != Status::ok) { return status; }
    std::uint32_t workers = std::max<std::uint32_t>(1, std::min<std::uint32_t>(threads, static_cast<std::uint32_t>(container.payloads.size())));
    std::vector<TileJob> jobs(workers);
    for (std::uint32_t t = 0; t < workers; ++t) {
        jobs[t].container = &container; jobs[t].tables = &tables; jobs[t].destination = destination.data();
        jobs[t].first = t; jobs[t].stride = workers;
    }
    if (workers == 1) { run_tiles(jobs[0]); }
    else {
        std::vector<std::thread> pool;
        for (std::uint32_t t = 0; t < workers; ++t) { pool.emplace_back(run_tiles, std::ref(jobs[t])); }
        for (std::thread& worker : pool) { worker.join(); }
    }
    for (const TileJob& job : jobs) {
        if (job.status != Status::ok) { return job.status; }
    }
    return Status::ok;
}

Status decode_mosaic(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination) {
    Container container;
    Status status = read_container(stream, container);
    if (status != Status::ok) { return status; }
    if (container.mosaic.empty()) { return Status::bad_payload; }
    Dimensions m;
    m.width = (container.dimensions.width + kCell - 1) / kCell;
    m.height = (container.dimensions.height + kCell - 1) / kCell;
    m.channels = container.dimensions.channels;
    if (destination.size() != static_cast<std::size_t>(m.width) * m.height * m.channels) { return Status::bad_sample_count; }
    std::vector<Table> tables;
    status = read_tables(container.tables, tables);
    if (status != Status::ok) { return status; }
    Workspace workspace;
    return decode_tile(tables, container.mosaic, m, workspace, destination.data(), static_cast<std::size_t>(m.width) * m.channels);
}

Status decode_salvage(std::span<const std::uint8_t> stream, std::vector<std::uint8_t>& destination, Dimensions& dimensions,
                     std::vector<std::uint8_t>& segments_ok, bool& mosaic_ok) {
    segments_ok.clear();
    mosaic_ok = false;
    Container container;
    Status status = read_container_checked(stream, container, false);
    if (status != Status::ok) { return status; }
    const Dimensions d = container.dimensions;
    dimensions = d;
    destination.assign(static_cast<std::size_t>(d.width) * d.height * d.channels, 128);
    std::vector<Table> tables;
    status = read_tables(container.tables, tables);
    if (status != Status::ok) { return status; }
    Workspace workspace;
    Dimensions m;
    m.width = (d.width + kCell - 1) / kCell; m.height = (d.height + kCell - 1) / kCell; m.channels = d.channels;
    std::vector<std::uint8_t> means(static_cast<std::size_t>(m.width) * m.height * m.channels, 128);
    if (!container.mosaic.empty()) {
        mosaic_ok = decode_tile(tables, container.mosaic, m, workspace, means.data(), static_cast<std::size_t>(m.width) * m.channels) == Status::ok;
        if (!mosaic_ok) { std::fill(means.begin(), means.end(), static_cast<std::uint8_t>(128)); }
    }
    const std::size_t row_bytes = static_cast<std::size_t>(d.width) * d.channels;
    for (std::uint32_t y = 0; y < d.height; ++y) {
        for (std::uint32_t x = 0; x < d.width; ++x) {
            const std::uint8_t* cell = means.data() + (static_cast<std::size_t>(y / kCell) * m.width + x / kCell) * d.channels;
            std::copy(cell, cell + d.channels, destination.data() + static_cast<std::size_t>(y) * row_bytes + static_cast<std::size_t>(x) * d.channels);
        }
    }
    segments_ok.assign(container.payloads.size(), 0);
    std::vector<std::uint8_t> scratch(static_cast<std::size_t>(container.tile_side) * container.segment_rows * d.channels);
    for (std::uint32_t index = 0; index < container.payloads.size(); ++index) {
        if (container.payload_intact[index] == 0) { continue; }
        const std::uint32_t x0 = (index % container.tiles_across) * container.tile_side;
        const std::uint32_t y0 = (index / container.tiles_across) * container.segment_rows;
        Dimensions tile;
        tile.width = std::min(container.tile_side, d.width - x0);
        tile.height = std::min(container.segment_rows, d.height - y0);
        tile.channels = d.channels;
        const std::size_t tile_row = static_cast<std::size_t>(tile.width) * d.channels;
        if (decode_tile(tables, container.payloads[index], tile, workspace, scratch.data(), tile_row) != Status::ok) { continue; }
        for (std::uint32_t y = 0; y < tile.height; ++y) {
            std::copy(scratch.data() + y * tile_row, scratch.data() + (y + 1) * tile_row,
                      destination.data() + static_cast<std::size_t>(y0 + y) * row_bytes + static_cast<std::size_t>(x0) * d.channels);
        }
        segments_ok[index] = 1;
    }
    return Status::ok;
}

Dimensions mosaic_dimensions(Dimensions dimensions) {
    Dimensions mosaic;
    mosaic.width = (dimensions.width + kCell - 1) / kCell;
    mosaic.height = (dimensions.height + kCell - 1) / kCell;
    mosaic.channels = dimensions.channels;
    return mosaic;
}

std::uint64_t segment_count(Dimensions dimensions) {
    const std::uint64_t across = (static_cast<std::uint64_t>(dimensions.width) + kSegmentWidth - 1) / kSegmentWidth;
    const std::uint64_t down = (static_cast<std::uint64_t>(dimensions.height) + kSegmentRows - 1) / kSegmentRows;
    return across * down;
}

}  // namespace stx_cng::detail
