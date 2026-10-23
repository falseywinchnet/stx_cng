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
    require(measurements.workspace_bytes < 14001000, "encoder capacity ceiling");
    std::string packet = encoded.str();
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
        collection_cases(folder);
        std::filesystem::remove_all(folder);
        std::cout << "PASS: 40 image cases, both decode interfaces, both collection forms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
