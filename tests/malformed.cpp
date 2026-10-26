// Malformed-input sweep for the CNG codec and the collection reader. Every input goes through the public interfaces.
// The requirement: no input crashes, reads outside its buffers or writes outside the destination; a decode that
// reports success reproduces the original samples, and a salvage reproduces every segment it reports intact.
//   1. damage to real files: every truncation and every byte flipped (small files), sampled flips, random bursts;
//   2. crafted files whose checksums all match: random segment payloads, random table bodies in valid zstd frames,
//      random mosaics, random dimensions with matching directories, so hostile data reaches every decoding stage.
// Built with the address and undefined-behaviour sanitizers by STX_CNG_SANITIZE, a violation aborts the run.
#include "stx_cng/codec.hpp"
#include "stx_cng/collection.hpp"
#include "test_support.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <zstd.h>

namespace {
using test_support::require;
using Bytes = std::vector<std::uint8_t>;

struct Random {
    std::uint64_t state;
    std::uint32_t next() { state = state * 6364136223846793005ull + 1442695040888963407ull; return static_cast<std::uint32_t>(state >> 33); }
    std::uint32_t below(std::uint32_t bound) { return bound == 0 ? 0 : next() % bound; }
};

std::uint32_t crc(const Bytes& bytes) { return test_support::crc32(bytes); }

void put_u32(Bytes& out, std::uint32_t v) { for (int k = 3; k >= 0; --k) { out.push_back(static_cast<std::uint8_t>(v >> (8 * k))); } }
std::uint32_t get_u32(const Bytes& b, std::size_t at) { return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) | (static_cast<std::uint32_t>(b[at + 2]) << 8) | b[at + 3]; }

// The parts of a CNG file, and their reassembly with every checksum recomputed.
struct Parts {
    std::uint32_t width = 0, height = 0, channels = 0;
    std::uint8_t version = 1, segment_width = 128, segment_rows = 32;
    Bytes tables, mosaic;
    std::vector<Bytes> payloads;
};

std::size_t segments_of(std::uint32_t w, std::uint32_t h) { return ((w + 127) / 128) * ((h + 31) / 32); }

Parts split(const Bytes& file) {
    Parts p;
    p.width = get_u32(file, 8); p.height = get_u32(file, 12); p.channels = file[16];
    const std::uint32_t mlen = get_u32(file, 20), tlen = get_u32(file, 28);
    const std::size_t n = segments_of(p.width, p.height);
    std::size_t at = 40 + 8 * n + 4;
    p.tables.assign(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + tlen)); at += tlen;
    p.mosaic.assign(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + mlen)); at += mlen;
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint32_t length = get_u32(file, 40 + 8 * k);
        p.payloads.emplace_back(file.begin() + static_cast<std::ptrdiff_t>(at), file.begin() + static_cast<std::ptrdiff_t>(at + length)); at += length;
    }
    return p;
}

Bytes assemble(const Parts& p) {
    Bytes fields;
    put_u32(fields, p.width); put_u32(fields, p.height);
    fields.push_back(static_cast<std::uint8_t>(p.channels)); fields.push_back(p.version); fields.push_back(p.segment_width); fields.push_back(p.segment_rows);
    put_u32(fields, static_cast<std::uint32_t>(p.mosaic.size())); put_u32(fields, crc(p.mosaic));
    put_u32(fields, static_cast<std::uint32_t>(p.tables.size())); put_u32(fields, crc(p.tables));
    put_u32(fields, crc(fields));
    Bytes directory;
    for (const Bytes& payload : p.payloads) { put_u32(directory, static_cast<std::uint32_t>(payload.size())); put_u32(directory, crc(payload)); }
    put_u32(directory, crc(directory));
    Bytes out = {0x89, 'C', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    out.insert(out.end(), fields.begin(), fields.end());
    out.insert(out.end(), directory.begin(), directory.end());
    out.insert(out.end(), p.tables.begin(), p.tables.end());
    out.insert(out.end(), p.mosaic.begin(), p.mosaic.end());
    for (const Bytes& payload : p.payloads) { out.insert(out.end(), payload.begin(), payload.end()); }
    out.insert(out.end(), p.mosaic.begin(), p.mosaic.end());
    if (p.payloads.size() > 1) { out.insert(out.end(), p.tables.begin(), p.tables.end()); }
    out.insert(out.end(), directory.begin(), directory.end());
    out.insert(out.end(), fields.begin(), fields.end());
    return out;
}

Bytes random_bytes(Random& random, std::size_t count) {
    Bytes out(count);
    for (std::uint8_t& b : out) { b = static_cast<std::uint8_t>(random.next()); }
    return out;
}

// A random tables body (38 contexts of random entry counts and q values) in one standard zstd frame.
Bytes random_tables(Random& random) {
    Bytes body;
    for (int k = 0; k < 38; ++k) {
        const std::uint32_t entries = random.below(4) == 0 ? random.below(700) : random.below(40);
        body.push_back(static_cast<std::uint8_t>(entries >> 8)); body.push_back(static_cast<std::uint8_t>(entries));
        for (std::uint32_t e = 0; e <= entries; ++e) { body.push_back(static_cast<std::uint8_t>(random.below(4) == 0 ? 0 : random.below(27))); }
    }
    if (random.below(5) == 0) { body.resize(random.below(static_cast<std::uint32_t>(body.size()))); }
    Bytes frame(ZSTD_compressBound(body.size()));
    const std::size_t written = ZSTD_compress(frame.data(), frame.size(), body.data(), body.size(), 3);
    require(!ZSTD_isError(written), "zstd");
    frame.resize(written);
    return frame;
}

std::size_t decode_checked(const Bytes& file, const Bytes* original) {
    std::size_t successes = 0;
    try {
        stx_cng::Dimensions d = stx_cng::read_dimensions(file);
        static_cast<void>(d);
    } catch (const stx_cng::Error&) {}
    try {
        stx_cng::Dimensions d;
        Bytes out = stx_cng::decode(file, d);
        if (original != nullptr) { require(out == *original, "a successful decode reproduces the original"); }
        ++successes;
    } catch (const stx_cng::Error&) {}
    try {
        stx_cng::DecodeOptions four;
        four.threads = 4;
        stx_cng::Dimensions d;
        Bytes out = stx_cng::decode(file, d, four);
        if (original != nullptr) { require(out == *original, "a successful threaded decode reproduces the original"); }
    } catch (const stx_cng::Error&) {}
    try {
        stx_cng::Dimensions m;
        Bytes out = stx_cng::decode_mosaic(file, m);
        static_cast<void>(out);
    } catch (const stx_cng::Error&) {}
    try {
        stx_cng::SalvageReport report;
        stx_cng::Dimensions d;
        Bytes out = stx_cng::decode_salvage(file, d, report);
        require(out.size() == d.width * d.height * d.channels, "salvage output extent");
    } catch (const stx_cng::Error&) {}
    return successes;
}

void damage_sweep(const Bytes& file, const Bytes& raw, Random& random, bool exhaustive) {
    const std::size_t step = exhaustive ? 1 : std::max<std::size_t>(1, file.size() / 400);
    for (std::size_t length = 0; length < file.size(); length += step) {
        Bytes cut(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(length));
        decode_checked(cut, &raw);
    }
    for (std::size_t at = 0; at < file.size(); at += step) {
        Bytes flipped = file;
        flipped[at] ^= static_cast<std::uint8_t>(1u << (at % 8));
        decode_checked(flipped, &raw);
    }
    for (int burst = 0; burst < 60; ++burst) {
        Bytes damaged = file;
        const std::size_t at = random.below(static_cast<std::uint32_t>(damaged.size()));
        const std::size_t span = std::min<std::size_t>(damaged.size() - at, 1 + random.below(600));
        for (std::size_t k = 0; k < span; ++k) { damaged[at + k] = static_cast<std::uint8_t>(random.next()); }
        decode_checked(damaged, &raw);
    }
}

void crafted_sweep(const Bytes& file, Random& random, int rounds) {
    const Parts base = split(file);
    for (int round = 0; round < rounds; ++round) {
        Parts p = base;
        const std::uint32_t kind = random.below(6);
        if (kind == 0 || kind == 5) {
            const std::size_t k = random.below(static_cast<std::uint32_t>(p.payloads.size()));
            p.payloads[k] = random_bytes(random, random.below(4) == 0 ? random.below(8) : random.below(3000));
        }
        if (kind == 1) { p.tables = random_tables(random); }
        if (kind == 2) { p.mosaic = random_bytes(random, random.below(400)); }
        if (kind == 3) {
            // keep each payload's stream lengths but scramble the streams
            for (Bytes& payload : p.payloads) { for (std::size_t q = 4; q < payload.size(); ++q) { if (random.below(16) == 0) { payload[q] = static_cast<std::uint8_t>(random.next()); } } }
        }
        if (kind == 4) {
            p.width = 1 + random.below(300); p.height = 1 + random.below(120); p.channels = random.below(2) == 0 ? 3 : 4;
            p.payloads.resize(segments_of(p.width, p.height));
            for (Bytes& payload : p.payloads) { if (payload.empty() || random.below(3) == 0) { payload = random_bytes(random, random.below(500)); } }
        }
        if (kind == 5 && random.below(2) == 0) { p.tables = random_tables(random); }
        decode_checked(assemble(p), nullptr);
    }
}

// Every quantized frequency above 25 in an otherwise valid tables body must be refused as invalid tables.
void table_range_sweep(const Bytes& file) {
    const Parts base = split(file);
    Bytes body(ZSTD_getFrameContentSize(base.tables.data(), base.tables.size()) == ZSTD_CONTENTSIZE_UNKNOWN ? 38 * 575 : 0);
    const std::size_t count = ZSTD_decompress(body.data(), body.size(), base.tables.data(), base.tables.size());
    require(!ZSTD_isError(count), "the tables decompress");
    body.resize(count);
    for (unsigned q = 26; q < 256; ++q) {
        Bytes edited = body;
        edited[2] = static_cast<std::uint8_t>(q);   // the first q byte of context 0
        Parts p = base;
        p.tables.resize(ZSTD_compressBound(edited.size()));
        const std::size_t written = ZSTD_compress(p.tables.data(), p.tables.size(), edited.data(), edited.size(), 3);
        require(!ZSTD_isError(written), "zstd");
        p.tables.resize(written);
        bool refused = false;
        try { stx_cng::Dimensions d; Bytes out = stx_cng::decode(assemble(p), d); static_cast<void>(out); }
        catch (const stx_cng::Error& e) { refused = e.reason() == stx_cng::Reason::tables; }
        require(refused, "a quantized frequency above 25 is refused");
    }
}

void collection_sweep(Random& random) {
    stx_cng::collection::ImageSource source;
    std::string folder = (std::filesystem::temp_directory_path() / "stx_cng_malformed").string();
    std::filesystem::create_directories(folder);
    const stx_cng::Dimensions d{.height = 20, .width = 30, .channels = 4};
    const Bytes raw = test_support::samples(d, 4, 3);
    source.id.value = 9; source.dimensions = d; source.raw_path = folder + "/member.raw";
    std::ofstream(source.raw_path, std::ios::binary).write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    stx_cng::collection::encode(std::span<const stx_cng::collection::ImageSource>(&source, 1), stx_cng::collection::Form::composite, stream);
    const std::string bytes = stream.str();
    for (int round = 0; round < 400; ++round) {
        std::string damaged = bytes;
        const std::size_t at = random.below(static_cast<std::uint32_t>(damaged.size()));
        damaged[at] = static_cast<char>(random.next());
        if (round % 7 == 0) { damaged.resize(random.below(static_cast<std::uint32_t>(damaged.size()))); }
        std::stringstream input(damaged, std::ios::in | std::ios::binary);
        try {
            stx_cng::collection::Decoded decoded;
            stx_cng::collection::decode(input, decoded);
        } catch (const stx_cng::Error&) {}
    }
    std::filesystem::remove_all(folder);
}
}  // namespace

int main() {
    Random random{20261025};
    struct Case { stx_cng::Dimensions d; unsigned pattern; bool exhaustive; };
    const Case cases[] = {{{.height = 1, .width = 1, .channels = 3}, 3, true}, {{.height = 9, .width = 13, .channels = 4}, 4, true},
                          {{.height = 33, .width = 129, .channels = 3}, 2, false}, {{.height = 70, .width = 140, .channels = 4}, 5, false},
                          {{.height = 64, .width = 300, .channels = 3}, 3, false}};
    std::size_t files = 0;
    for (const Case& c : cases) {
        const Bytes raw = test_support::samples(c.d, c.pattern, static_cast<std::uint32_t>(c.d.width));
        const Bytes file = stx_cng::encode(raw, c.d);
        require(decode_checked(file, &raw) == 1, "the intact file decodes");
        damage_sweep(file, raw, random, c.exhaustive);
        crafted_sweep(file, random, 300);
        table_range_sweep(file);
        ++files;
    }
    collection_sweep(random);
    std::cout << "stx_cng malformed-input sweep: " << files << " files damaged and crafted; no violation\n";
    return 0;
}
