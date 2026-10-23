#include "stx_cngcompress.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
struct FreeEncoder { void operator()(stx_cngcompress_encoder* handle) const { stx_cngcompress_encoder_destroy(handle); } };
struct FreeDecoder { void operator()(stx_cngcompress_decoder* handle) const { stx_cngcompress_decoder_destroy(handle); } };
using Encoder = std::unique_ptr<stx_cngcompress_encoder, FreeEncoder>;
using Decoder = std::unique_ptr<stx_cngcompress_decoder, FreeDecoder>;
void exact(std::size_t size, std::size_t period, unsigned disturbances,
           stx_cngcompress_encoder* encoder, stx_cngcompress_decoder* decoder) {
    stx_cngcompress_result bound = stx_cngcompress_bound(size);
    require(bound.status == STX_CNGCOMPRESS_OK, "input bound");
    std::vector<std::uint8_t> raw(size);
    std::vector<std::uint8_t> coded(bound.size + 8, 177);
    std::vector<std::uint8_t> decoded(size + 8, 177);
    std::uint32_t state = 20261022;
    for (std::size_t index = 0; index < size; ++index) {
        state = state * 1664525U + 1013904223U;
        std::uint8_t value = static_cast<std::uint8_t>(state >> 24);
        if (period != 0 && index >= period) { value = raw[index - period]; }
        if (disturbances != 0 && index % 1031 == 0) { value ^= 77U; }
        raw[index] = value;
    }
    stx_cngcompress_result written = stx_cngcompress_compress(encoder, raw.data(), size, coded.data(), bound.size);
    require(written.status == STX_CNGCOMPRESS_OK && written.size <= size + 5, "encode result");
    stx_cngcompress_result header = stx_cngcompress_decoded_size(coded.data(), written.size);
    require(header.status == STX_CNGCOMPRESS_OK && header.size == size, "frame header");
    stx_cngcompress_result restored = stx_cngcompress_decompress(decoder, coded.data(), written.size, decoded.data(), size);
    require(restored.status == STX_CNGCOMPRESS_OK && restored.size == size, "decode result");
    require(std::equal(raw.begin(), raw.end(), decoded.begin()), "exact bytes");
    for (std::size_t index = bound.size; index < coded.size(); ++index) { require(coded[index] == 177, "encode extent"); }
    for (std::size_t index = size; index < decoded.size(); ++index) { require(decoded[index] == 177, "decode extent"); }
}
void failures(stx_cngcompress_encoder* encoder, stx_cngcompress_decoder* decoder) {
    std::array<std::uint8_t, 32> output{};
    output.fill(177);
    std::array<std::uint8_t, 6> stored{0, 1, 0, 0, 0, 42};
    stx_cngcompress_result result = stx_cngcompress_bound(std::numeric_limits<std::size_t>::max());
    require(result.status == STX_CNGCOMPRESS_INPUT_TOO_LARGE, "checked bound arithmetic");
    result = stx_cngcompress_compress(encoder, stored.data(), 1, output.data(), 0);
    require(result.status == STX_CNGCOMPRESS_OUTPUT_TOO_SMALL && result.size == 10, "encode capacity requirement");
    result = stx_cngcompress_decompress(decoder, stored.data(), stored.size(), output.data(), 0);
    require(result.status == STX_CNGCOMPRESS_OUTPUT_TOO_SMALL && result.size == 1, "decode capacity requirement");
    result = stx_cngcompress_compress(encoder, output.data(), 1, output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_ARGUMENT, "input/output overlap");
    result = stx_cngcompress_compress(encoder, stored.data(), 1, encoder, stx_cngcompress_encoder_bytes());
    require(result.status == STX_CNGCOMPRESS_INVALID_ARGUMENT, "handle/output overlap");
    result = stx_cngcompress_decompress(decoder, decoder, 6, output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_ARGUMENT, "handle/input overlap before header read");
    result = stx_cngcompress_compress(nullptr, nullptr, 0, output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_ARGUMENT, "null context");
    result = stx_cngcompress_compress(encoder, nullptr, 1, output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_ARGUMENT, "null live input");
    result = stx_cngcompress_decompress(decoder, stored.data(), 4, output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_DATA, "short header");
    for (std::uint8_t value : output) { require(value == 177, "preflight failures preserve output"); }
    stored[0] = 255;
    result = stx_cngcompress_decompress(decoder, stored.data(), stored.size(), output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_DATA, "unknown frame mode");
    stored[0] = 16;
    result = stx_cngcompress_decompress(decoder, stored.data(), stored.size(), output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_DATA, "truncated block");
    std::array<std::uint8_t, 10> reserved{16, 1, 0, 0, 0, 1, 0, 0, 192, 42};
    result = stx_cngcompress_decompress(decoder, reserved.data(), reserved.size(), output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_INVALID_DATA, "reserved block flags");
    stored[0] = 0;
    result = stx_cngcompress_decompress(decoder, stored.data(), stored.size(), output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_OK && output[0] == 42, "reuse after failure");
    result = stx_cngcompress_compress(encoder, nullptr, 0, output.data(), output.size());
    require(result.status == STX_CNGCOMPRESS_OK && result.size == 5, "empty frame");
    for (std::size_t index = 0; index < 5; ++index) { require(output[index] == 0, "empty golden bytes"); }
    result = stx_cngcompress_decompress(decoder, output.data(), 5, nullptr, 0);
    require(result.status == STX_CNGCOMPRESS_OK && result.size == 0, "null empty output");
}
void concurrent(bool* passed) {
    try {
        Encoder encoder(stx_cngcompress_encoder_create());
        Decoder decoder(stx_cngcompress_decoder_create());
        require(encoder && decoder, "thread contexts");
        exact(131073, 7, 1, encoder.get(), decoder.get());
        *passed = true;
    } catch (...) { *passed = false; }
}
}
int main() {
    try {
        Encoder encoder(stx_cngcompress_encoder_create());
        Decoder decoder(stx_cngcompress_decoder_create());
        require(encoder && decoder, "context creation");
        constexpr std::array<std::size_t, 24> sizes{0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 255, 256, 257,
            65534, 65535, 65536, 65537, 131071, 131072, 131073, 262143, 262144, 262145, STX_CNGCOMPRESS_MAX_INPUT_SIZE};
        constexpr std::array<std::size_t, 9> periods{0, 1, 2, 3, 7, 257, 65535, 65536, 131072};
        std::size_t cases = 0;
        for (std::size_t size : sizes) {
            for (std::size_t period : periods) {
                for (unsigned disturbance = 0; disturbance < 2; ++disturbance) {
                    exact(size, period, disturbance, encoder.get(), decoder.get());
                    ++cases;
                }
            }
        }
        failures(encoder.get(), decoder.get());
        bool first = false;
        bool second = false;
        std::jthread first_thread(concurrent, &first);
        std::jthread second_thread(concurrent, &second);
        first_thread.join();
        second_thread.join();
        require(first && second, "independent concurrent contexts");
        stx_cngcompress_encoder_destroy(nullptr);
        stx_cngcompress_decoder_destroy(nullptr);
        std::cout << "PASS: " << cases << " public frame cases; failure contracts, golden empty frame and independent concurrent contexts\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
