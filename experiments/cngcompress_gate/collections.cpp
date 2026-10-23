#include "stx_cng/collection.hpp"
#include "candidate_collection.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
struct Case {
    std::vector<std::string> raw;
    std::vector<stx_cng_collection::ImageSource> original;
    std::vector<cng_candidate_collection::ImageSource> candidate;
};
void create(unsigned kind, const std::filesystem::path& directory, Case& result) {
    std::size_t images = kind == 0 ? 16 : kind == 1 ? 64 : 8;
    std::size_t channels = kind == 2 ? 3 : 4;
    std::uint32_t random = 713;
    for (std::size_t image = 0; image < images; ++image) {
        std::size_t width = kind == 0 ? 24 + image * 2 : kind == 1 ? 32 : 128;
        std::size_t height = kind == 0 ? 24 : width;
        std::string raw(height * width * channels, '\0');
        for (std::size_t y = 0; y < height; ++y) {
            for (std::size_t x = 0; x < width; ++x) {
                bool interior = x > width / 4 && x < 3 * width / 4 && y > height / 4 && y < 3 * height / 4;
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    unsigned value = kind == 2 ? random & 255U : static_cast<unsigned>((interior ? 83 : 15) + channel * 37 + image);
                    if (kind == 1) { value = static_cast<unsigned>(((x + image) % 7 < 3 && y % 9 < 4) ? 220 : 0); }
                    if (channel == 3) { value = interior ? 255 : 0; }
                    raw[(y * width + x) * channels + channel] = static_cast<char>(value);
                }
            }
        }
        std::filesystem::path path = directory / (std::to_string(kind) + "-" + std::to_string(image) + ".raw");
        std::ofstream file(path, std::ios::binary);
        file.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        file.close();
        require(static_cast<bool>(file), "collection fixture write");
        std::uint32_t id = static_cast<std::uint32_t>(image + 100);
        result.original.push_back({{id}, {height, width, channels}, path});
        result.candidate.push_back({{id}, {height, width, channels}, path});
        result.raw.push_back(std::move(raw));
    }
}
struct Result {
    std::size_t bytes = 0;
    std::size_t encode_workspace = 0;
    std::size_t decode_workspace = 0;
    std::array<double, 7> encode{};
    std::array<double, 7> decode{};
};
template<bool candidate>
void execute(const Case& sample, unsigned form, std::size_t trial, Result& result) {
    std::ostringstream output(std::ios::binary);
    Clock::time_point start = Clock::now();
    if constexpr (candidate) {
        cng_candidate_collection::Measurements measurements{};
        cng_candidate_collection::encode(sample.candidate, static_cast<cng_candidate_collection::Form>(form), output, measurements);
        result.encode_workspace = measurements.workspace_bytes;
    } else {
        stx_cng_collection::Measurements measurements{};
        stx_cng_collection::encode(sample.original, static_cast<stx_cng_collection::Form>(form), output, measurements);
        result.encode_workspace = measurements.workspace_bytes;
    }
    Clock::time_point encoded = Clock::now();
    std::string packet = std::move(output).str();
    result.bytes = packet.size();
    std::istringstream input(packet, std::ios::binary);
    double decode_seconds = 0;
    if constexpr (candidate) {
        cng_candidate_collection::Decoded decoded{};
        cng_candidate_collection::Measurements measurements{};
        Clock::time_point before = Clock::now();
        cng_candidate_collection::decode(input, decoded, measurements);
        Clock::time_point after = Clock::now();
        decode_seconds = std::chrono::duration<double>(after - before).count();
        result.decode_workspace = measurements.workspace_bytes;
        require(decoded.planes.size() == (form == 0 ? sample.raw.size() : 1), "candidate plane ownership");
        for (std::size_t index = 0; index < sample.raw.size(); ++index) {
            std::ostringstream slice(std::ios::binary);
            cng_candidate_collection::write_image(decoded, sample.candidate[index].id, slice);
            require(slice.str() == sample.raw[index], "candidate exact slice including hidden RGB");
        }
    } else {
        stx_cng_collection::Decoded decoded{};
        stx_cng_collection::Measurements measurements{};
        Clock::time_point before = Clock::now();
        stx_cng_collection::decode(input, decoded, measurements);
        Clock::time_point after = Clock::now();
        decode_seconds = std::chrono::duration<double>(after - before).count();
        result.decode_workspace = measurements.workspace_bytes;
        require(decoded.planes.size() == (form == 0 ? sample.raw.size() : 1), "reference plane ownership");
        for (std::size_t index = 0; index < sample.raw.size(); ++index) {
            std::ostringstream slice(std::ios::binary);
            stx_cng_collection::write_image(decoded, sample.original[index].id, slice);
            require(slice.str() == sample.raw[index], "reference exact slice including hidden RGB");
        }
    }
    if (trial != 0) {
        result.encode[trial - 1] = std::chrono::duration<double>(encoded - start).count();
        result.decode[trial - 1] = decode_seconds;
    }
}
double median(std::array<double, 7> times) {
    std::sort(times.begin(), times.end());
    return times[3];
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "fresh collection output directory required");
        std::filesystem::path directory(argv[1]);
        require(std::filesystem::create_directory(directory), "fresh collection directory");
        std::ofstream rows(directory / "collections.csv");
        rows << "case,form,method,images,bytes,encode_seconds,decode_seconds,encode_workspace,decode_workspace\n";
        rows << std::setprecision(12);
        for (unsigned kind = 0; kind < 3; ++kind) {
            Case sample{};
            create(kind, directory, sample);
            for (unsigned form = 0; form < 2; ++form) {
                std::array<Result, 2> results{};
                for (std::size_t trial = 0; trial < 8; ++trial) {
                    if ((trial + kind + form) % 2 == 0) {
                        execute<false>(sample, form, trial, results[0]);
                        execute<true>(sample, form, trial, results[1]);
                    } else {
                        execute<true>(sample, form, trial, results[1]);
                        execute<false>(sample, form, trial, results[0]);
                    }
                }
                for (unsigned method = 0; method < 2; ++method) {
                    const Result& result = results[method];
                    rows << kind << ',' << form << ',' << method << ',' << sample.raw.size() << ',' << result.bytes
                         << ',' << median(result.encode) << ',' << median(result.decode)
                         << ',' << result.encode_workspace << ',' << result.decode_workspace << '\n';
                }
            }
        }
        rows.close();
        require(static_cast<bool>(rows), "collection results complete");
        std::cout << "PASS: 3 collections, both forms, both codecs, 7 paired trials; exact slices and plane ownership\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
