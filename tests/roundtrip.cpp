#include "stx_cng/codec.hpp"
#include "stx_cng/collection.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::string samples(stx_cng::Dimensions dimensions, unsigned pattern) {
    std::string result(dimensions.height * dimensions.width * dimensions.channels, '\0');
    std::uint32_t state = 20261022;
    for (std::size_t y = 0; y < dimensions.height; ++y) {
        for (std::size_t x = 0; x < dimensions.width; ++x) {
            for (std::size_t channel = 0; channel < dimensions.channels; ++channel) {
                state = state * 1664525U + 1013904223U;
                std::uint32_t value = state >> 24;
                if (pattern == 0) { value = static_cast<std::uint32_t>(3 * x + y + 67 * channel); }
                if (pattern == 1) { value = x < dimensions.width / 2 ? 0U : 255U; }
                if (pattern == 2) { value = static_cast<std::uint32_t>((x % 7) * 19 + channel * 31); }
                std::size_t position = (y * dimensions.width + x) * dimensions.channels + channel;
                result[position] = static_cast<char>(value & 255U);
            }
        }
    }
    return result;
}
void image_case(stx_cng::Dimensions dimensions, unsigned pattern) {
    std::string raw = samples(dimensions, pattern);
    std::istringstream source(raw);
    std::ostringstream encoded;
    stx_cng::Measurements measurements;
    stx_cng::encode(source, encoded, dimensions, measurements);
    require(measurements.workspace_bytes < raw.size() * 3 + 1500000, "encoder workspace ceiling");
    std::string packet = encoded.str();
    require(packet.size() > 52 && packet.compare(0, 8, "CNGPHI08") == 0 && static_cast<unsigned char>(packet[17]) == 128, "CNGPHI08 header");
    std::istringstream stream(packet);
    std::ostringstream restored;
    stx_cng::Dimensions actual = stx_cng::decode(stream, restored, measurements);
    require(actual.width == dimensions.width && actual.height == dimensions.height
        && actual.channels == dimensions.channels && restored.str() == raw, "stream round trip");
    std::vector<std::uint8_t> pixels(raw.size());
    std::istringstream direct(packet);
    actual = stx_cng::decode_into(direct, pixels, measurements);
    require(actual.width == dimensions.width, "direct dimensions");
    for (std::size_t index = 0; index < raw.size(); ++index) {
        require(pixels[index] == static_cast<std::uint8_t>(raw[index]), "direct exact sample");
    }
}
void version_case() {
    // A complete, CRC-valid CSP v3 header from the previous release: rejection must occur before pixel output.
    const std::array<std::uint8_t, 36> legacy{137, 67, 83, 80, 13, 10, 26, 10, 0, 0, 0, 16, 67, 72, 68, 82, 3, 3, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 16, 0, 0, 94, 195, 157, 77};
    std::string bytes(reinterpret_cast<const char*>(legacy.data()), legacy.size());
    std::istringstream input(bytes);
    std::array<std::uint8_t, 3> output{17, 23, 41};
    stx_cng::Measurements measurements{};
    bool rejected = false;
    try {
        stx_cng::Dimensions ignored = stx_cng::decode_into(input, output, measurements);
        static_cast<void>(ignored);
    } catch (const std::runtime_error& error) {
        std::string message(error.what());
        rejected = message.find("CNGPHI08") != std::string::npos;
    }
    require(rejected && output[0] == 17 && output[1] == 23 && output[2] == 41, "legacy format boundary");
}
std::uint32_t big_endian(const std::string& bytes, std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 16)
           | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3]));
}
void corruption_case() {
    // A flipped payload byte is reported as a frame checksum failure by decode_into; decode_salvage decodes the
    // other frames exactly and fills the lost one from the mosaic layer, which equals the block means of the image.
    stx_cng::Dimensions dimensions{40, 300, 3};
    std::string raw = samples(dimensions, 2);
    std::istringstream source(raw);
    std::ostringstream encoded;
    stx_cng::Measurements measurements;
    stx_cng::encode(source, encoded, dimensions, measurements);
    std::string packet = encoded.str();
    require(measurements.frames == 3, "three frames across 300 columns");
    std::istringstream for_mosaic(packet);
    std::ostringstream mosaic_out;
    stx_cng::Dimensions from_mosaic = stx_cng::decode_mosaic(for_mosaic, mosaic_out, measurements);
    stx_cng::Dimensions mosaic = stx_cng::mosaic_dimensions(dimensions);
    require(from_mosaic.width == 300 && mosaic.width == 19 && mosaic.height == 3, "mosaic dimensions");
    std::string mosaic_samples = mosaic_out.str();
    require(mosaic_samples.size() == 19 * 3 * 3, "mosaic extent");
    for (std::size_t my = 0; my < 3; ++my) {
        for (std::size_t mx = 0; mx < 19; ++mx) {
            for (std::size_t c = 0; c < 3; ++c) {
                std::uint32_t sum = 0, count = 0;
                for (std::size_t y = my * 16; y < std::min<std::size_t>(40, my * 16 + 16); ++y) {
                    for (std::size_t x = mx * 16; x < std::min<std::size_t>(300, mx * 16 + 16); ++x) { sum += static_cast<unsigned char>(raw[(y * 300 + x) * 3 + c]); ++count; }
                }
                require(static_cast<unsigned char>(mosaic_samples[(my * 19 + mx) * 3 + c]) == (sum + count / 2) / count, "mosaic cell mean");
            }
        }
    }
    const std::size_t mosaic_length = big_endian(packet, 18);
    const std::size_t first_payload = 30 + 3 * 8 + 4 + mosaic_length;        // header, directory, mosaic copy
    const std::size_t flipped = first_payload + big_endian(packet, 30) + 5;  // inside the second frame's payload
    packet[flipped] = static_cast<char>(packet[flipped] ^ 0x5A);
    std::istringstream damaged(packet);
    std::vector<std::uint8_t> pixels(raw.size());
    bool reported = false;
    try {
        stx_cng::Dimensions ignored = stx_cng::decode_into(damaged, pixels, measurements);
        static_cast<void>(ignored);
    } catch (const std::runtime_error& error) {
        reported = std::string(error.what()).find("checksum") != std::string::npos;
    }
    require(reported, "frame checksum detection");
    std::istringstream salvage(packet);
    std::vector<std::uint8_t> frames_ok;
    bool mosaic_ok = false;
    stx_cng::Dimensions salvaged = stx_cng::decode_salvage(salvage, pixels, frames_ok, mosaic_ok, measurements);
    require(salvaged.width == 300 && frames_ok.size() == 3 && frames_ok[0] == 1 && frames_ok[1] == 0 && frames_ok[2] == 1 && mosaic_ok, "salvage mask");
    for (std::size_t y = 0; y < 40; ++y) {
        for (std::size_t x = 0; x < 300; ++x) {
            for (std::size_t c = 0; c < 3; ++c) {
                std::size_t at = (y * 300 + x) * 3 + c;
                unsigned char expected = (x >= 128 && x < 256) ? static_cast<unsigned char>(mosaic_samples[((y / 16) * 19 + x / 16) * 3 + c]) : static_cast<unsigned char>(raw[at]);
                require(pixels[at] == expected, "salvaged samples: exact frames, mosaic fill for the lost frame");
            }
        }
    }
}
void collection_cases(const std::filesystem::path& folder) {
    std::vector<stx_cng_collection::ImageSource> images;
    std::vector<std::string> originals;
    for (std::size_t index = 0; index < 3; ++index) {
        stx_cng::Dimensions dimensions{19 + index, 23 + index, 3 + index % 2};
        std::string raw = samples(dimensions, static_cast<unsigned>(index));
        std::filesystem::path path = folder / (std::to_string(index) + ".raw");
        std::ofstream output(path, std::ios::binary);
        output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        output.close();
        require(static_cast<bool>(output), "fixture file");
        images.push_back({{static_cast<std::uint32_t>(index)}, dimensions, path});
        originals.push_back(std::move(raw));
    }
    for (stx_cng_collection::Form form : {stx_cng_collection::Form::packed, stx_cng_collection::Form::composite}) {
        std::stringstream packet;
        stx_cng_collection::Measurements measurements;
        stx_cng_collection::encode(images, form, packet, measurements);
        packet.seekg(0);
        stx_cng_collection::Decoded decoded;
        stx_cng_collection::decode(packet, decoded, measurements);
        for (std::size_t index = 0; index < images.size(); ++index) {
            std::ostringstream output;
            stx_cng_collection::write_image(decoded, images[index].id, output);
            require(output.str() == originals[index], "collection exact slice");
        }
    }
}
}
int main() {
    std::filesystem::path folder = std::filesystem::current_path() / "stx-cng-test-fixtures";
    try {
        require(std::filesystem::create_directory(folder), "exclusive fixture directory");
        for (std::size_t channels : {3U, 4U}) {
            for (stx_cng::Dimensions dimensions : std::array<stx_cng::Dimensions, 5>{
                stx_cng::Dimensions{1, 1, channels}, {7, 13, channels},
                {257, 1, channels}, {1, 257, channels}, {1025, 513, channels}}) {
                for (unsigned pattern = 0; pattern < 4; ++pattern) { image_case(dimensions, pattern); }
            }
        }
        version_case();
        corruption_case();
        collection_cases(folder);
        std::filesystem::remove_all(folder);
        std::cout << "PASS: 40 image cases, both decode interfaces, both collection forms, legacy rejection, frame checksum detection, mosaic and salvage\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
