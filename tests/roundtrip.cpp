#include "stx_cng/codec.hpp"
#include "stx_cng/collection.hpp"
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
    require(packet.size() > 36 && packet.compare(0, 8, "CNGPHI07") == 0 && static_cast<unsigned char>(packet[17]) == 128, "CNGPHI07 header");
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
        rejected = message.find("CNGPHI07") != std::string::npos;
    }
    require(rejected && output[0] == 17 && output[1] == 23 && output[2] == 41, "legacy format boundary");
}
void corruption_case() {
    // A flipped payload byte is reported as a frame checksum failure; the header and directory still parse.
    stx_cng::Dimensions dimensions{40, 300, 3};
    std::string raw = samples(dimensions, 2);
    std::istringstream source(raw);
    std::ostringstream encoded;
    stx_cng::Measurements measurements;
    stx_cng::encode(source, encoded, dimensions, measurements);
    std::string packet = encoded.str();
    require(measurements.frames == 3, "three frames across 300 columns");
    std::size_t index_copy = 3 * 8 + 4 + 14;                      // trailing directory copy and header copy
    packet[packet.size() - index_copy - 1] = static_cast<char>(packet[packet.size() - index_copy - 1] ^ 0x5A);
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
        std::cout << "PASS: 40 image cases, both decode interfaces, both collection forms, legacy rejection, frame checksum detection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
