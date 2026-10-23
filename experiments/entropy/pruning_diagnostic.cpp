#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zlib.h>
#include <cmath>
#include <memory>
// Diagnostic-only inclusion exposes the parser without changing measured code.
#if defined(CNG_PRUNE_DIAGNOSTIC)
#include "match_event_pruned.cpp"
#else
#include "match_event_eight.cpp"
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>
namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t big(std::span<const std::uint8_t> input, std::size_t position) {
    require(position <= input.size() && input.size() - position >= 4, "diagnostic integer extent");
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { value = (value << 8) | input[position + byte]; }
    return value;
}
struct Context {
    ZSTD_CCtx* encoder = ZSTD_createCCtx();
    Context() { require(encoder != nullptr, "diagnostic context"); }
    ~Context() { ZSTD_freeCCtx(encoder); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
struct ParseStatistics {
    std::size_t events = 0;
    std::size_t matches = 0;
    std::size_t literals = 0;
    std::size_t copied = 0;
    std::array<std::size_t, 6> lengths{};
};
void inspect_parser(std::span<const std::uint8_t> raw, bool prune) {
    std::unique_ptr<cng_match_trial::EncodeWorkspace> storage = std::make_unique<cng_match_trial::EncodeWorkspace>();
    cng_match_trial::EncodeWorkspace& workspace = *storage;
    std::size_t slots = std::bit_floor(std::min<std::size_t>(16384, std::max<std::size_t>(16, raw.size() / 4)));
    std::fill_n(workspace.index.data(), slots, 0);
    ParseStatistics statistics;
    for (std::size_t begin = 0; begin < raw.size();) {
        std::size_t count = std::min<std::size_t>(131072, raw.size() - begin);
        cng_match_trial::ParseResult result{};
#if defined(CNG_PRUNE_DIAGNOSTIC)
        if (prune) { result = cng_match_trial::parse(raw, begin, count, slots - 1, workspace); }
        else { result = cng_match_trial::collect(raw, begin, count, slots - 1, workspace); }
#else
        require(!prune, "eight-byte diagnostic selection");
        result = cng_match_trial::parse(raw, begin, count, slots - 1, workspace);
#endif
        statistics.events += result.events;
        if (result.events == 0) { statistics.literals += count; begin += count; continue; }
        std::size_t position = begin;
        std::size_t literals = 0;
        for (std::size_t index = 0; index < result.events; ++index) {
            const cng_match_event::Event& event = workspace.events[index];
            require(event.literals <= begin + count - position, "parser literal bound");
            literals += event.literals;
            position += event.literals;
            if (event.length == 0) { require(index + 1 == result.events && event.distance == 0, "parser terminal"); continue; }
            require(event.distance != 0 && event.distance <= position && event.length <= begin + count - position, "parser match bound");
            for (std::size_t byte = 0; byte < event.length; ++byte) {
                require(raw[position + byte] == raw[position + byte - event.distance], "parser exact reference");
            }
            ++statistics.matches;
            statistics.copied += event.length;
            std::size_t bucket = event.length < 4 ? 0 : event.length < 8 ? 1 : event.length < 16 ? 2 : event.length < 32 ? 3 : event.length < 128 ? 4 : 5;
            ++statistics.lengths[bucket];
            position += event.length;
        }
        require(position == begin + count && literals == result.literals, "parser complete accounting");
        statistics.literals += literals;
        begin += count;
    }
    require(statistics.literals + statistics.copied == raw.size(), "parser complete frame");
    std::cout << "{\"events\":" << statistics.events << ",\"matches\":" << statistics.matches
              << ",\"literals\":" << statistics.literals << ",\"copied\":" << statistics.copied << ",\"length_counts\":[";
    for (std::size_t index = 0; index < statistics.lengths.size(); ++index) {
        if (index != 0) { std::cout << ','; }
        std::cout << statistics.lengths[index];
    }
    std::cout << "]}";
}
void inspect(std::span<const std::uint8_t> frame, const char* kind) {
    unsigned long long declared = ZSTD_getFrameContentSize(frame.data(), frame.size());
    require(declared <= 2097160, "diagnostic frame bound");
    Bytes raw(static_cast<std::size_t>(declared));
    std::size_t unpacked = ZSTD_decompress(raw.data(), raw.size(), frame.data(), frame.size());
    require(ZSTD_isError(unpacked) == 0 && unpacked == raw.size(), "diagnostic frame inverse");
    Context context;
    Bytes control(ZSTD_compressBound(raw.size()));
    std::size_t control_size = ZSTD_compressCCtx(context.encoder, control.data(), control.size(), raw.data(), raw.size(), 3);
    require(ZSTD_isError(control_size) == 0 && control_size == frame.size()
            && std::equal(frame.begin(), frame.end(), control.begin()), "diagnostic frozen control bytes");
    std::cout << "{\"kind\":\"" << kind << "\",\"raw_bytes\":" << raw.size()
              << ",\"zstd_bytes\":" << frame.size() << ",\"initial\":";
    inspect_parser(raw, false);
#if defined(CNG_PRUNE_DIAGNOSTIC)
    std::cout << ",\"pruned\":";
    inspect_parser(raw, true);
#endif
    std::cout << ",\"exact\":true}";
}
}
int main(int argc, char** argv) {
    try {
#if defined(CNG_PRUNE_DIAGNOSTIC)
        // The pricing domain is exactly the 1..1024 normalized populations.
        for (std::uint32_t population = 1; population <= 1024; ++population) {
            double logarithm = std::log2(static_cast<double>(population));
            std::uint32_t expected = static_cast<std::uint32_t>(std::floor(256.0 * logarithm));
            require(cng_match_trial::logarithm_q8(population) == expected, "pricing logarithm reference");
        }
#endif
        require(argc == 2, "diagnostic frozen CSP path required");
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        require(static_cast<bool>(file), "diagnostic input open");
        std::streamoff length = file.tellg();
        require(length >= 48 && length <= 64 * 1024 * 1024, "diagnostic file bound");
        file.seekg(0);
        Bytes packet(static_cast<std::size_t>(length));
        file.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()));
        require(static_cast<bool>(file), "diagnostic input read");
        constexpr std::array<std::uint8_t, 8> signature{137, 67, 83, 80, 13, 10, 26, 10};
        require(std::equal(signature.begin(), signature.end(), packet.begin()), "diagnostic signature");
        std::size_t position = 8;
        std::size_t frames = 0;
        std::cout << "{\"zstd_version\":\"" << ZSTD_versionString() << "\",\"frames\":[";
        while (position < packet.size()) {
            require(packet.size() - position >= 12, "diagnostic chunk");
            std::size_t size = big(packet, position);
            require(size <= packet.size() - position - 12, "diagnostic chunk extent");
            std::uint32_t kind = big(packet, position + 4);
            uLong checksum = crc32(0, packet.data() + position + 4, static_cast<uInt>(size + 4));
            require(checksum == big(packet, position + 8 + size), "diagnostic checksum");
            if (kind == 0x42414e44U) {
                require(size >= 13, "diagnostic band");
                std::span<const std::uint8_t> payload(packet.data() + position + 8, size);
                std::size_t sequence_size = big(payload, 8);
                require(sequence_size <= size - 13, "diagnostic sequence extent");
                if (frames != 0) { std::cout << ','; }
                inspect(payload.subspan(12, sequence_size), "sequences");
                ++frames;
                std::size_t literal = 12 + sequence_size;
                require(payload[literal] <= 1, "diagnostic literal coding");
                if (payload[literal] == 1) {
                    std::cout << ',';
                    inspect(payload.subspan(literal + 1), "literals");
                    ++frames;
                }
            }
            position += size + 12;
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
