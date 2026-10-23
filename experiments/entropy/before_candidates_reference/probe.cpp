#include "rans.hpp"
#ifdef CNG_MATCH
#if defined(CNG_EVENT_PARTITIONED)
#include "match_event_partitioned.hpp"
#elif defined(CNG_EVENT_COMPACT)
#include "match_event_compact_backend.hpp"
#elif defined(CNG_EVENT_BACKEND)
#include "match_event_backend.hpp"
#elif defined(CNG_SPLIT_MATCH)
#include "match_split.hpp"
#else
#include "match_huffman.hpp"
#endif
#endif
#ifdef CNG_EVENTS
#include "events.hpp"
#endif
#ifdef CNG_SEQUENCE_RUNS
#include "runs.hpp"
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <zstd.h>
#include <zlib.h>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t big(std::span<const std::uint8_t> bytes, std::size_t at) {
    require(at <= bytes.size() && bytes.size() - at >= 4, "integer extent");
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) { value = (value << 8) | bytes[at + index]; }
    return value;
}
Bytes read_file(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "packet open");
    std::streamoff size = input.tellg();
    require(size >= 48 && size <= 64 * 1024 * 1024, "packet extent");
    input.seekg(0);
    Bytes result(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    require(static_cast<bool>(input), "packet read");
    return result;
}
struct Context {
    ZSTD_CCtx* encoder = ZSTD_createCCtx();
    ZSTD_DCtx* decoder = ZSTD_createDCtx();
    Context() {
        if (encoder == nullptr || decoder == nullptr) {
            ZSTD_freeCCtx(encoder);
            ZSTD_freeDCtx(decoder);
            throw std::runtime_error("zstd contexts");
        }
    }
    ~Context() { ZSTD_freeCCtx(encoder); ZSTD_freeDCtx(decoder); }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
double entropy(std::span<const std::uint32_t> counts) {
    std::uint64_t count = 0;
    for (std::uint32_t value : counts) { count += value; }
    if (count == 0) { return 0.0; }
    double result = 0.0;
    for (std::uint32_t value : counts) {
        if (value != 0) { result += static_cast<double>(value) * std::log2(static_cast<double>(count) / value); }
    }
    return result;
}
struct Distribution {
    double zero_bits = 0.0;
    double previous_byte_bits = 0.0;
    double field_bits = 0.0;
};
Distribution distribution(std::span<const std::uint8_t> raw, bool sequences) {
    std::array<std::uint32_t, 256> counts{};
    std::vector<std::uint32_t> pairs(65536, 0);
    for (std::uint8_t value : raw) { ++counts[value]; }
    for (std::size_t index = 1; index < raw.size(); ++index) {
        ++pairs[static_cast<std::size_t>(raw[index - 1]) * 256 + raw[index]];
    }
    Distribution result;
    result.zero_bits = entropy(counts);
    result.previous_byte_bits = raw.empty() ? 0.0 : 8.0;
    for (std::size_t previous = 0; previous < 256; ++previous) {
        result.previous_byte_bits += entropy(std::span<const std::uint32_t>(pairs).subspan(previous * 256, 256));
    }
    result.field_bits = result.zero_bits;
    if (sequences) {
        require(raw.size() % 12 == 0, "sequence byte planes");
        result.field_bits = 0.0;
        std::size_t stride = raw.size() / 12;
        for (std::size_t field = 0; field < 12; ++field) {
            counts.fill(0);
            for (std::uint8_t value : raw.subspan(field * stride, stride)) { ++counts[value]; }
            result.field_bits += entropy(counts);
        }
    }
    return result;
}
void print_samples(const std::array<double, 7>& samples) {
    std::cout << '[';
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (index != 0) { std::cout << ','; }
        std::cout << samples[index];
    }
    std::cout << ']';
}
void probe(std::span<const std::uint8_t> frame, bool sequences, std::size_t expected, Context& context) {
    unsigned long long declared = ZSTD_getFrameContentSize(frame.data(), frame.size());
    require(declared != ZSTD_CONTENTSIZE_ERROR && declared != ZSTD_CONTENTSIZE_UNKNOWN
            && declared <= cng_entropy_trial::maximum_input, "bounded source frame");
    if (sequences) { require(declared == expected, "sequence frame output"); }
    Bytes raw(static_cast<std::size_t>(declared));
    std::size_t unpacked = ZSTD_decompressDCtx(context.decoder, raw.data(), raw.size(), frame.data(), frame.size());
    require(ZSTD_isError(unpacked) == 0 && unpacked == raw.size(), "original entropy frame");
    Distribution stats = distribution(raw, sequences);
    std::size_t own_capacity = raw.size();
#ifdef CNG_SEQUENCE_RUNS
    if (sequences) { own_capacity = 2 * raw.size() + 4; }
#endif
    Bytes separated(own_capacity);
    Bytes restored_separated(own_capacity);
    Bytes encoded(2 * own_capacity + 2048);
#ifdef CNG_EVENTS
    Bytes event_scratch(2 * raw.size() + 64);
#endif
    Bytes zstd(ZSTD_compressBound(raw.size()));
    Bytes decoded(raw.size());
#ifdef CNG_MATCH
    std::unique_ptr<cng_match_trial::EncodeWorkspace> match_encoder = std::make_unique<cng_match_trial::EncodeWorkspace>();
    std::unique_ptr<cng_match_trial::DecodeWorkspace> match_decoder = std::make_unique<cng_match_trial::DecodeWorkspace>();
#endif
    std::array<double, 7> own_encode{};
    std::array<double, 7> own_decode{};
    std::array<double, 7> zstd_encode{};
    std::array<double, 7> zstd_decode{};
    std::size_t own_size = 0;
    std::size_t zstd_size = 0;
    for (std::size_t trial = 0; trial < 8; ++trial) {
        for (std::size_t order = 0; order < 2; ++order) {
            std::size_t kind = (trial + order) % 2;
            Clock::time_point start = Clock::now();
            if (kind == 0) {
#ifdef CNG_MATCH
                own_size = cng_match_trial::encode(raw, encoded, *match_encoder);
#else
#ifdef CNG_EVENTS
                if (sequences) { own_size = cng_event_trial::encode(raw, encoded, event_scratch); } else
#endif
#ifdef CNG_SEQUENCE_RUNS
                if (sequences) {
                    std::size_t separated_size = cng_entropy_trial::separate_runs(raw, separated);
                    own_size = cng_entropy_trial::encode(std::span<const std::uint8_t>(separated).first(separated_size), encoded);
                } else
#endif
                { own_size = cng_entropy_trial::encode(raw, encoded); }
#endif
            }
            else {
                zstd_size = ZSTD_compressCCtx(context.encoder, zstd.data(), zstd.size(), raw.data(), raw.size(), 3);
                require(ZSTD_isError(zstd_size) == 0, "zstd recompress");
            }
            Clock::time_point middle = Clock::now();
            if (kind == 0) {
#ifdef CNG_MATCH
                cng_match_trial::decode(std::span<const std::uint8_t>(encoded).first(own_size), decoded, *match_decoder);
#else
#ifdef CNG_EVENTS
                if (sequences) { cng_event_trial::decode(std::span<const std::uint8_t>(encoded).first(own_size), decoded); } else
#endif
#ifdef CNG_SEQUENCE_RUNS
                if (sequences) {
                    std::size_t separated_size = 0;
                    for (unsigned byte = 0; byte < 4; ++byte) {
                        separated_size |= static_cast<std::size_t>(encoded[1 + byte]) << (8 * byte);
                    }
                    require(separated_size <= restored_separated.size(), "separated output extent");
                    std::span<std::uint8_t> expanded(restored_separated.data(), separated_size);
                    cng_entropy_trial::decode(std::span<const std::uint8_t>(encoded).first(own_size), expanded);
                    cng_entropy_trial::restore_runs(expanded, decoded);
                } else
#endif
                { cng_entropy_trial::decode(std::span<const std::uint8_t>(encoded).first(own_size), decoded); }
#endif
            }
            else {
                unpacked = ZSTD_decompressDCtx(context.decoder, decoded.data(), decoded.size(), zstd.data(), zstd_size);
                require(ZSTD_isError(unpacked) == 0 && unpacked == raw.size(), "zstd decode");
            }
            Clock::time_point end = Clock::now();
            require(decoded == raw, "exact entropy round trip");
            if (trial != 0) {
                double encode_time = std::chrono::duration<double>(middle - start).count();
                double decode_time = std::chrono::duration<double>(end - middle).count();
                if (kind == 0) { own_encode[trial - 1] = encode_time; own_decode[trial - 1] = decode_time; }
                else { zstd_encode[trial - 1] = encode_time; zstd_decode[trial - 1] = decode_time; }
            }
        }
    }
    require(zstd_size == frame.size() && std::equal(frame.begin(), frame.end(), zstd.begin()), "frozen Zstandard bytes");
    std::size_t candidate_bytes = sequences ? own_size : std::min(own_size, expected);
    std::cout << "{\"kind\":\"" << (sequences ? "sequences" : "literals")
              << "\",\"raw_bytes\":" << raw.size() << ",\"zstd_bytes\":" << frame.size()
              << ",\"candidate_payload_bytes\":" << candidate_bytes
              << ",\"rans_bytes\":" << own_size << ",\"stored\":" << (encoded[0] == 0 ? "true" : "false")
              << ",\"h0_bytes\":" << stats.zero_bits / 8.0
              << ",\"h1_oracle_bytes\":" << stats.previous_byte_bits / 8.0
              << ",\"field_oracle_bytes\":" << stats.field_bits / 8.0
              << ",\"rans_encode\":";
    print_samples(own_encode);
    std::cout << ",\"rans_decode\":"; print_samples(own_decode);
    std::cout << ",\"zstd_encode\":"; print_samples(zstd_encode);
    std::cout << ",\"zstd_decode\":"; print_samples(zstd_decode);
    std::cout << '}';
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "entropy probe expects a frozen CSP file");
        Bytes packet = read_file(argv[1]);
        constexpr std::array<std::uint8_t, 8> signature{137, 67, 83, 80, 13, 10, 26, 10};
        require(std::equal(signature.begin(), signature.end(), packet.begin()), "CSP signature");
        Context context;
        std::size_t offset = 8;
        std::size_t index = 0;
        std::size_t channels = 0;
        bool header_seen = false;
        bool end_seen = false;
        std::cout.precision(12);
        std::cout << "{\"zstd_version\":\"" << ZSTD_versionString() << "\",\"frames\":[";
        while (offset < packet.size()) {
            require(packet.size() - offset >= 12 && !end_seen, "CSP chunk extent");
            std::size_t size = big(packet, offset);
            require(size <= packet.size() - offset - 12, "CSP payload extent");
            std::uint32_t kind = big(packet, offset + 4);
            uLong crc = crc32(0, packet.data() + offset + 4, static_cast<uInt>(size + 4));
            require(crc == big(packet, offset + 8 + size), "CSP chunk CRC");
            std::span<const std::uint8_t> payload(packet.data() + offset + 8, size);
            if (kind == 0x43484452U) {
                require(!header_seen && offset == 8 && size == 16 && payload[0] == 2, "CSP header");
                channels = payload[1];
                require(channels == 3 || channels == 4, "CSP channels");
                header_seen = true;
            } else if (kind == 0x42414e44U) {
                require(header_seen && size >= 13, "CSP band");
                std::size_t sequence_count = big(payload, 0);
                std::size_t sequence_size = big(payload, 8);
                require(sequence_count <= cng_entropy_trial::maximum_input / 12 && sequence_size <= size - 13, "sequence bound");
                if (index != 0) { std::cout << ','; }
                probe(payload.subspan(12, sequence_size), true, sequence_count * 12, context);
                ++index;
                std::size_t coding = 12 + sequence_size;
                require(payload[coding] <= 1, "literal coding");
                if (payload[coding] == 1) {
                    std::cout << ',';
                    std::size_t literals = big(payload, 4);
                    require(literals <= cng_entropy_trial::maximum_input / channels, "literal population");
                    probe(payload.subspan(coding + 1), false, literals * channels, context);
                    ++index;
                }
            } else {
                require(header_seen && kind == 0x43454e44U && size == 0, "CSP end");
                end_seen = true;
            }
            offset += size + 12;
        }
        require(header_seen && end_seen, "complete CSP");
        std::cout << "],\"exact\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
