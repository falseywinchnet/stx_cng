#include "../whole_codec/match_bounded.hpp"
#include "grammar.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t get32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t result = 0;
    for (unsigned byte = 0; byte < 4; ++byte) { result |= static_cast<std::uint32_t>(bytes[at + byte]) << (8 * byte); }
    return result;
}
struct Contexts {
    std::unique_ptr<cng_match_trial::EncodeWorkspace> encoder = std::make_unique_for_overwrite<cng_match_trial::EncodeWorkspace>();
    std::unique_ptr<cng_match_trial::DecodeWorkspace> decoder = std::make_unique_for_overwrite<cng_match_trial::DecodeWorkspace>();
    std::unique_ptr<cng_grammar::SplitWorkspace> split = std::make_unique_for_overwrite<cng_grammar::SplitWorkspace>();
};
struct Case {
    int level = 0; // 0 baseline, 1 prefix integers, 2 split controls, 3 both.
    std::vector<std::uint8_t> encoded;
    std::size_t size = 0;
    std::array<double, 5> encode_seconds{};
    std::array<double, 5> decode_seconds{};
};
std::size_t encode(Case& codec, std::span<const std::uint8_t> input, Contexts& context) {
    std::size_t size = 0;
    switch (codec.level) {
        case 0: size = cng_match_trial::encode(input, codec.encoded, *context.encoder); break;
        case 1: size = cng_prefix::encode(input, codec.encoded, *context.encoder); break;
        case 2: size = cng_split::encode(input, codec.encoded, *context.split); break;
        case 3: size = cng_combined::encode(input, codec.encoded, *context.split); break;
        default: require(false, "grammar method"); break;
    }
    return size;
}
void decode(const Case& codec, std::span<std::uint8_t> output, Contexts& context) {
    std::span<const std::uint8_t> input = std::span<const std::uint8_t>(codec.encoded).first(codec.size);
    switch (codec.level) {
        case 0: cng_match_trial::decode(input, output, *context.decoder); break;
        case 1: cng_prefix::decode(input, output, *context.decoder); break;
        case 2: cng_split::decode(input, output, *context.decoder); break;
        case 3: cng_combined::decode(input, output, *context.decoder); break;
        default: require(false, "grammar method"); break;
    }
}
double median(std::array<double, 5> values) {
    std::sort(values.begin(), values.end());
    return values[2];
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "frame bundle and new result CSV required");
        std::ifstream existing(argv[2]);
        require(!existing.good(), "refuse result overwrite");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        require(static_cast<bool>(input) && static_cast<bool>(rows), "comparison files open");
        rows.precision(12);
        rows << "frame,level,raw_bytes,coded_bytes,encode_seconds,decode_seconds\n";
        Contexts context{};
        std::array<int, 4> levels{0, 1, 2, 3};
        std::size_t ordinal = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            require(static_cast<bool>(input), "frame header");
            std::size_t size = get32(header, 0);
            require(size >= 5 && size <= 2097160 + 512, "frame extent");
            std::vector<std::uint8_t> frame(size);
            input.read(reinterpret_cast<char*>(frame.data()), static_cast<std::streamsize>(size));
            require(static_cast<bool>(input), "frame read");
            std::size_t count = get32(frame, 1);
            require(count <= 2097160, "decoded frame bound");
            std::vector<std::uint8_t> raw(count);
            std::vector<std::uint8_t> decoded(count);
            cng_match_trial::decode(frame, raw, *context.decoder);
            std::size_t capacity = count + 4 * ((count + 131071) / 131072) + 5;
            std::array<Case, 4> cases{};
            for (std::size_t index = 0; index < cases.size(); ++index) {
                Case& codec = cases[index];
                codec.level = levels[index];
                codec.encoded.resize(capacity);
                codec.size = encode(codec, raw, context);
                decode(codec, decoded, context);
                require(decoded == raw, "warmup exact inverse");
                if (codec.level == 0) {
                    require(codec.size == frame.size() && std::equal(frame.begin(), frame.end(), codec.encoded.begin()), "current candidate byte identity");
                }
            }
            // Rotate complete codec calls. Allocation, file I/O and verification
            // stay outside timing; contexts retain their warmed allocations.
            for (std::size_t trial = 0; trial < 5; ++trial) {
                for (std::size_t order = 0; order < cases.size(); ++order) {
                    std::size_t index = (order + ordinal + trial) % cases.size();
                    Case& codec = cases[index];
                    Clock::time_point start = Clock::now();
                    std::size_t written = encode(codec, raw, context);
                    Clock::time_point encoded = Clock::now();
                    decode(codec, decoded, context);
                    Clock::time_point finished = Clock::now();
                    require(written == codec.size && decoded == raw, "deterministic length and exact inverse");
                    codec.encode_seconds[trial] = std::chrono::duration<double>(encoded - start).count();
                    codec.decode_seconds[trial] = std::chrono::duration<double>(finished - encoded).count();
                }
            }
            for (const Case& codec : cases) {
                rows << ordinal << ',' << codec.level << ',' << count << ',' << codec.size << ',' << median(codec.encode_seconds) << ',' << median(codec.decode_seconds) << '\n';
            }
            ++ordinal;
            if (ordinal % 100 == 0) { std::cout << "Compared " << ordinal << " frames\n" << std::flush; }
        }
        rows.close();
        require(static_cast<bool>(rows), "complete CSV");
        std::cout << "PASS: " << ordinal << " frames, four grammars, five measured trials and warmup, exact inverses\n";
        std::cout << "Baseline/prefix encoder bytes " << sizeof(cng_match_trial::EncodeWorkspace)
                  << " split encoder bytes " << sizeof(cng_grammar::SplitWorkspace)
                  << " decoder bytes " << sizeof(cng_match_trial::DecodeWorkspace) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
