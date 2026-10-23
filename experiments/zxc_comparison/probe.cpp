#include "../whole_codec/match_bounded.hpp"
#include "zxc.h"
#include <zstd.h>
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
struct FreeCompressor { void operator()(zxc_cctx* context) const { zxc_free_cctx(context); } };
struct FreeDecoder { void operator()(zxc_dctx* context) const { zxc_free_dctx(context); } };
struct FreeZstdCompressor { void operator()(ZSTD_CCtx* context) const { ZSTD_freeCCtx(context); } };
struct FreeZstdDecoder { void operator()(ZSTD_DCtx* context) const { ZSTD_freeDCtx(context); } };
struct Contexts {
    std::unique_ptr<cng_match_trial::EncodeWorkspace> encoder = std::make_unique_for_overwrite<cng_match_trial::EncodeWorkspace>();
    std::unique_ptr<cng_match_trial::DecodeWorkspace> decoder = std::make_unique_for_overwrite<cng_match_trial::DecodeWorkspace>();
    std::unique_ptr<zxc_cctx, FreeCompressor> zxc_encoder;
    std::unique_ptr<zxc_dctx, FreeDecoder> zxc_decoder;
    std::unique_ptr<ZSTD_CCtx, FreeZstdCompressor> zstd_encoder;
    std::unique_ptr<ZSTD_DCtx, FreeZstdDecoder> zstd_decoder;
    Contexts() {
        zxc_compress_opts_t options{};
        options.level = 7;
        options.n_threads = 1;
        options.block_size = 524288;
        zxc_encoder.reset(zxc_create_cctx(&options));
        zxc_decoder.reset(zxc_create_dctx());
        zstd_encoder.reset(ZSTD_createCCtx());
        zstd_decoder.reset(ZSTD_createDCtx());
        require(zxc_encoder && zxc_decoder && zstd_encoder && zstd_decoder, "codec context allocation");
    }
};
struct Case {
    int level = 0; // 0 is current CNG; -3 is Zstandard level 3; positive is zxc.
    std::vector<std::uint8_t> encoded;
    std::size_t size = 0;
    std::array<double, 3> encode_seconds{};
    std::array<double, 3> decode_seconds{};
};
std::size_t encode(Case& codec, std::span<const std::uint8_t> input, Contexts& context) {
    if (codec.level == 0) {
        std::size_t size = cng_match_trial::encode(input, codec.encoded, *context.encoder);
        return size;
    }
    if (codec.level == -3) {
        std::size_t size = ZSTD_compressCCtx(context.zstd_encoder.get(), codec.encoded.data(), codec.encoded.size(), input.data(), input.size(), 3);
        require(!ZSTD_isError(size), "Zstandard encode");
        return size;
    }
    zxc_compress_opts_t options{};
    options.level = codec.level;
    options.n_threads = 1;
    options.block_size = 524288;
    std::int64_t size = zxc_compress_cctx(context.zxc_encoder.get(), input.data(), input.size(), codec.encoded.data(), codec.encoded.size(), &options);
    require(size > 0, "zxc encode");
    std::size_t result = static_cast<std::size_t>(size);
    return result;
}
void decode(const Case& codec, std::span<std::uint8_t> output, Contexts& context) {
    if (codec.level == 0) {
        cng_match_trial::decode(std::span<const std::uint8_t>(codec.encoded).first(codec.size), output, *context.decoder);
        return;
    }
    if (codec.level == -3) {
        std::size_t size = ZSTD_decompressDCtx(context.zstd_decoder.get(), output.data(), output.size(), codec.encoded.data(), codec.size);
        require(!ZSTD_isError(size) && size == output.size(), "Zstandard decode");
        return;
    }
    zxc_decompress_opts_t options{};
    options.n_threads = 1;
    std::int64_t size = zxc_decompress_dctx(context.zxc_decoder.get(), codec.encoded.data(), codec.size, output.data(), output.size(), &options);
    require(size >= 0 && static_cast<std::size_t>(size) == output.size(), "zxc decode");
}
double median(std::array<double, 3> values) {
    std::sort(values.begin(), values.end());
    return values[1];
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
        require(ZSTD_versionNumber() == 10507, "Zstandard pin");
        rows.precision(12);
        rows << "frame,level,raw_bytes,coded_bytes,encode_seconds,decode_seconds\n";
        Contexts context{};
        std::array<int, 7> levels{0, -3, 1, 3, 5, 6, 7};
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
            std::size_t capacity = static_cast<std::size_t>(zxc_compress_bound(count));
            capacity = std::max(capacity, ZSTD_compressBound(count));
            capacity = std::max(capacity, count + 4 * ((count + 131071) / 131072) + 5);
            std::array<Case, 7> cases{};
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
            for (std::size_t trial = 0; trial < 3; ++trial) {
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
        std::cout << "PASS: " << ordinal << " frames, seven codecs, three measured trials and warmup, exact inverses\n";
        for (int level : levels) {
            if (level <= 0) { continue; }
            std::cout << "zxc level " << level << " static encoder bytes " << zxc_static_cctx_workspace_size(524288, level)
                      << " static decoder bytes " << zxc_static_dctx_workspace_size(524288) << '\n';
        }
        std::cout << "CNG encoder bytes " << sizeof(cng_match_trial::EncodeWorkspace) << " decoder bytes " << sizeof(cng_match_trial::DecodeWorkspace) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
