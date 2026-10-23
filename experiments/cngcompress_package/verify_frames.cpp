#include "stx_cngcompress.h"
#include "../whole_codec/match_bounded.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace {
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
struct FreeEncoder { void operator()(stx_cngcompress_encoder* handle) const { stx_cngcompress_encoder_destroy(handle); } };
struct FreeDecoder { void operator()(stx_cngcompress_decoder* handle) const { stx_cngcompress_decoder_destroy(handle); } };
std::size_t size(std::istream& input) {
    std::array<std::uint8_t, 4> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), 4);
    require(static_cast<bool>(input), "identity record size");
    std::size_t result = 0;
    for (unsigned index = 0; index < 4; ++index) { result |= static_cast<std::size_t>(bytes[index]) << (8 * index); }
    require(result <= STX_CNGCOMPRESS_MAX_INPUT_SIZE + 512, "identity record bound");
    return result;
}
void read(std::istream& input, std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) { return; }
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(input), "identity record body");
}
void compare(const std::vector<std::uint8_t>& raw, const std::vector<std::uint8_t>& frame,
             stx_cngcompress_encoder* encoder, stx_cngcompress_decoder* decoder) {
    stx_cngcompress_result bound = stx_cngcompress_bound(raw.size());
    require(bound.status == STX_CNGCOMPRESS_OK, "identity bound");
    std::vector<std::uint8_t> encoded(bound.size);
    std::vector<std::uint8_t> decoded(raw.size());
    stx_cngcompress_result result = stx_cngcompress_compress(encoder, raw.data(), raw.size(), encoded.data(), encoded.size());
    require(result.status == STX_CNGCOMPRESS_OK && result.size == frame.size(), "public frame size identity");
    require(std::equal(frame.begin(), frame.end(), encoded.begin()), "public frame byte identity");
    result = stx_cngcompress_decompress(decoder, frame.data(), frame.size(), decoded.data(), decoded.size());
    require(result.status == STX_CNGCOMPRESS_OK && result.size == raw.size() && decoded == raw, "public frame raw identity");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "vectors and frames required");
        std::unique_ptr<stx_cngcompress_encoder, FreeEncoder> encoder(stx_cngcompress_encoder_create());
        std::unique_ptr<stx_cngcompress_decoder, FreeDecoder> decoder(stx_cngcompress_decoder_create());
        std::unique_ptr<cng_match_trial::DecodeWorkspace> prior = std::make_unique<cng_match_trial::DecodeWorkspace>();
        require(encoder && decoder, "public identity contexts");
        std::ifstream vectors(argv[1], std::ios::binary);
        require(static_cast<bool>(vectors), "vectors open");
        std::size_t vector_count = 0;
        while (vectors.peek() != std::char_traits<char>::eof()) {
            std::size_t raw_size = size(vectors);
            std::size_t frame_size = size(vectors);
            std::vector<std::uint8_t> raw(raw_size);
            std::vector<std::uint8_t> frame(frame_size);
            read(vectors, raw);
            read(vectors, frame);
            compare(raw, frame, encoder.get(), decoder.get());
            ++vector_count;
        }
        std::ifstream frames(argv[2], std::ios::binary);
        require(static_cast<bool>(frames), "frames open");
        std::size_t frame_count = 0;
        while (frames.peek() != std::char_traits<char>::eof()) {
            std::vector<std::uint8_t> frame(size(frames));
            read(frames, frame);
            stx_cngcompress_result header = stx_cngcompress_decoded_size(frame.data(), frame.size());
            require(header.status == STX_CNGCOMPRESS_OK, "retained frame header");
            std::vector<std::uint8_t> raw(header.size);
            cng_match_trial::decode(frame, raw, *prior);
            compare(raw, frame, encoder.get(), decoder.get());
            ++frame_count;
        }
        require(vector_count == 414 && frame_count == 1413, "complete identity corpus");
        std::cout << "PASS: 414 known-raw vectors and 1413 retained frames, public encode byte identity and exact decode\n";
        std::cout << "Public handles: " << stx_cngcompress_encoder_bytes() << " encoder bytes; " << stx_cngcompress_decoder_bytes() << " decoder bytes\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
