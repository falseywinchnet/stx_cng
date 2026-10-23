#include "stx_cng/codec.hpp"
#include "release.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
// Borrow live mutable string storage only for input; the codec never writes it.
// The string owner is declared before this buffer and its consuming stream.
class InputBuffer : public std::streambuf {
public:
    explicit InputBuffer(std::string& bytes) {
        char* first = bytes.data();
        setg(first, first, first + bytes.size());
    }
};
std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "whole input open");
    std::streamoff count = input.tellg();
    require(count >= 48 && count <= 64 * 1024 * 1024, "whole input extent");
    input.seekg(0);
    std::string bytes(static_cast<std::size_t>(count), '\0');
    input.read(bytes.data(), count);
    require(static_cast<bool>(input), "whole input read");
    return bytes;
}
void write_file(const char* path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "whole output open");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(static_cast<bool>(output), "whole output write");
}
struct Record {
    std::array<double, 7> encode{};
    std::array<double, 7> decode{};
    std::array<double, 7> encode_entropy{};
    std::array<double, 7> decode_entropy{};
    std::size_t encode_workspace = 0;
    std::size_t decode_workspace = 0;
    std::size_t stored = 0;
    std::size_t compressed = 0;
};
template<bool candidate>
std::string encode(std::string& raw, cng_release_v2::Dimensions dimensions, Record& record, std::size_t trial) {
    InputBuffer buffer(raw);
    std::istream source(&buffer);
    std::ostringstream destination(std::ios::binary);
    Clock::time_point start = Clock::now();
    if constexpr (candidate) {
        stx_cng::Measurements measurements;
        stx_cng::encode(source, destination, {dimensions.height, dimensions.width, dimensions.channels}, measurements);
        Clock::time_point end = Clock::now();
        record.encode[trial] = std::chrono::duration<double>(end - start).count();
        record.encode_entropy[trial] = measurements.entropy_seconds;
        record.encode_workspace = measurements.workspace_bytes;
        record.stored = measurements.stored_bands;
        record.compressed = measurements.compressed_bands;
    } else {
        cng_release_v2::Measurements measurements;
        cng_release_v2::encode(source, destination, dimensions, measurements);
        Clock::time_point end = Clock::now();
        record.encode[trial] = std::chrono::duration<double>(end - start).count();
        record.encode_entropy[trial] = measurements.entropy_seconds;
        record.encode_workspace = measurements.workspace_bytes;
        record.stored = measurements.stored_bands;
        record.compressed = measurements.compressed_bands;
    }
    std::string result = std::move(destination).str();
    return result;
}
template<bool candidate>
void decode(std::string& packet, std::span<std::uint8_t> pixels, cng_release_v2::Dimensions expected, Record& record, std::size_t trial) {
    InputBuffer buffer(packet);
    std::istream source(&buffer);
    Clock::time_point start = Clock::now();
    if constexpr (candidate) {
        stx_cng::Measurements measurements;
        stx_cng::Dimensions actual = stx_cng::decode_into(source, pixels, measurements);
        Clock::time_point end = Clock::now();
        record.decode[trial] = std::chrono::duration<double>(end - start).count();
        record.decode_entropy[trial] = measurements.entropy_seconds;
        record.decode_workspace = measurements.workspace_bytes;
        require(actual.height == expected.height && actual.width == expected.width && actual.channels == expected.channels, "candidate dimensions");
    } else {
        cng_release_v2::Measurements measurements;
        cng_release_v2::Dimensions actual = cng_release_v2::decode_into(source, pixels, measurements);
        Clock::time_point end = Clock::now();
        record.decode[trial] = std::chrono::duration<double>(end - start).count();
        record.decode_entropy[trial] = measurements.entropy_seconds;
        record.decode_workspace = measurements.workspace_bytes;
        require(actual.height == expected.height && actual.width == expected.width && actual.channels == expected.channels, "reference dimensions");
    }
}
void exact(std::span<const std::uint8_t> pixels, const std::string& raw) {
    require(pixels.size() == raw.size(), "whole sample count");
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        require(pixels[index] == static_cast<std::uint8_t>(raw[index]), "whole exact sample");
    }
}
void numbers(const std::array<double, 7>& values) {
    std::cout << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) { std::cout << ','; }
        std::cout << values[index];
    }
    std::cout << ']';
}
void output(const Record& record) {
    std::cout << "{\"encode_seconds\":"; numbers(record.encode);
    std::cout << ",\"decode_seconds\":"; numbers(record.decode);
    std::cout << ",\"encode_entropy_seconds\":"; numbers(record.encode_entropy);
    std::cout << ",\"decode_entropy_seconds\":"; numbers(record.decode_entropy);
    std::cout << ",\"encode_workspace_bytes\":" << record.encode_workspace
              << ",\"decode_workspace_bytes\":" << record.decode_workspace
              << ",\"stored_bands\":" << record.stored << ",\"compressed_bands\":" << record.compressed << '}';
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 4, "whole input.csp output.cne output.raw required");
        std::string frozen = read_file(argv[1]);
        InputBuffer frozen_buffer(frozen);
        std::istream frozen_source(&frozen_buffer);
        std::ostringstream restored(std::ios::binary);
        cng_release_v2::Measurements initial;
        cng_release_v2::Dimensions dimensions = cng_release_v2::decode(frozen_source, restored, initial);
        std::string raw = std::move(restored).str();
        Record reference;
        Record candidate;
        std::string control = encode<false>(raw, dimensions, reference, 0);
        require(control == frozen, "whole frozen reference bytes");
        std::string proposed = encode<true>(raw, dimensions, candidate, 0);
        std::vector<std::uint8_t> pixels(raw.size());
        decode<false>(control, pixels, dimensions, reference, 0); exact(pixels, raw);
        decode<true>(proposed, pixels, dimensions, candidate, 0); exact(pixels, raw);
        // Also check the stream-output interface once, outside measurements.
        InputBuffer candidate_buffer(proposed);
        std::istream candidate_source(&candidate_buffer);
        std::ostringstream candidate_pixels(std::ios::binary);
        stx_cng::Measurements candidate_stream;
        stx_cng::Dimensions streamed = stx_cng::decode(candidate_source, candidate_pixels, candidate_stream);
        require(streamed.width == dimensions.width && streamed.height == dimensions.height && streamed.channels == dimensions.channels
            && candidate_pixels.str() == raw, "candidate stream inverse");
        for (std::size_t trial = 0; trial < 7; ++trial) {
            std::string first;
            std::string second;
            if ((trial & 1U) == 0) {
                first = encode<false>(raw, dimensions, reference, trial);
                second = encode<true>(raw, dimensions, candidate, trial);
                decode<false>(first, pixels, dimensions, reference, trial); exact(pixels, raw);
                decode<true>(second, pixels, dimensions, candidate, trial); exact(pixels, raw);
            } else {
                second = encode<true>(raw, dimensions, candidate, trial);
                first = encode<false>(raw, dimensions, reference, trial);
                decode<true>(second, pixels, dimensions, candidate, trial); exact(pixels, raw);
                decode<false>(first, pixels, dimensions, reference, trial); exact(pixels, raw);
            }
            require(first == control && second == proposed, "whole deterministic packets");
        }
        write_file(argv[2], proposed);
        write_file(argv[3], raw);
        std::cout << std::setprecision(12) << "{\"height\":" << dimensions.height << ",\"width\":" << dimensions.width
                  << ",\"channels\":" << dimensions.channels << ",\"raw_bytes\":" << raw.size()
                  << ",\"reference_bytes\":" << control.size() << ",\"candidate_bytes\":" << proposed.size()
                  << ",\"stream_decode_workspace_bytes\":" << candidate_stream.workspace_bytes << ",\"reference\":";
        output(reference); std::cout << ",\"candidate\":"; output(candidate);
        std::cout << ",\"exact\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
