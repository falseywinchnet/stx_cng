// Round trips through every public call of stx_cng: images of every size class, channel count and pattern; inspection,
// mosaic and salvage; the documented error reasons; and both collection forms.
#include "stx_cng/codec.hpp"
#include "stx_cng/collection.hpp"
#include "test_support.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
using test_support::require;

stx_cng::Reason reason_of_decode(const std::vector<std::uint8_t>& file) {
    try {
        stx_cng::Dimensions d;
        std::vector<std::uint8_t> out = stx_cng::decode(file, d);
        static_cast<void>(out);
    } catch (const stx_cng::Error& e) {
        return e.reason();
    }
    require(false, "decode succeeded where it must fail");
    return stx_cng::Reason::damaged;
}

void image_case(stx_cng::Dimensions d, unsigned pattern) {
    std::vector<std::uint8_t> raw = test_support::samples(d, pattern, static_cast<std::uint32_t>(d.width * 31 + d.height + pattern));
    std::vector<std::uint8_t> file = stx_cng::encode(raw, d);
    stx_cng::EncodeOptions four;
    four.threads = 4;
    require(stx_cng::encode(raw, d, four) == file, "encoding with four threads gives the same file");
    require(file.size() > 72 && file[0] == 0x89 && file[1] == 'C' && file[2] == 'N' && file[3] == 'G' && file[17] == 1, "signature and version");
    stx_cng::Dimensions read = stx_cng::read_dimensions(file);
    require(read.width == d.width && read.height == d.height && read.channels == d.channels, "read_dimensions");
    for (unsigned threads : {1u, 4u, 0u}) {
        stx_cng::DecodeOptions options;
        options.threads = threads;
        stx_cng::Dimensions actual;
        require(stx_cng::decode(file, actual, options) == raw, "decode");
        std::vector<std::uint8_t> into(raw.size());
        stx_cng::decode_into(file, into, options);
        require(into == raw, "decode_into");
    }
    std::istringstream input(std::string(raw.begin(), raw.end()));
    std::ostringstream encoded;
    stx_cng::encode(input, encoded, d);
    require(encoded.str() == std::string(file.begin(), file.end()), "stream encode");
    std::istringstream encoded_input(encoded.str());
    std::ostringstream decoded;
    stx_cng::Dimensions streamed = stx_cng::decode(encoded_input, decoded);
    require(streamed.width == d.width && decoded.str() == std::string(raw.begin(), raw.end()), "stream decode");
    stx_cng::Dimensions mosaic;
    require(stx_cng::decode_mosaic(file, mosaic) == test_support::block_means(raw, d), "mosaic");
    stx_cng::SalvageReport report;
    stx_cng::Dimensions salvaged;
    require(stx_cng::decode_salvage(file, salvaged, report) == raw && report.segments_lost == 0 && report.mosaic_ok
            && report.segments_ok.size() == stx_cng::segment_count(d), "salvage of an intact file");
}

void damage_case() {
    stx_cng::Dimensions d{.height = 96, .width = 256, .channels = 3};
    std::vector<std::uint8_t> raw = test_support::samples(d, 0, 7);
    std::vector<std::uint8_t> file = stx_cng::encode(raw, d);
    const std::size_t segments = stx_cng::segment_count(d);
    require(segments == 6, "segment grid");
    // the first payload follows the header, directory, tables and the first mosaic copy
    const std::size_t directory = 40;
    const std::size_t tables = (static_cast<std::size_t>(file[28]) << 24) | (static_cast<std::size_t>(file[29]) << 16) | (static_cast<std::size_t>(file[30]) << 8) | file[31];
    const std::size_t mosaic = (static_cast<std::size_t>(file[20]) << 24) | (static_cast<std::size_t>(file[21]) << 16) | (static_cast<std::size_t>(file[22]) << 8) | file[23];
    const std::size_t first = directory + 8 * segments + 4 + tables + mosaic;
    std::vector<std::uint8_t> damaged = file;
    damaged[first + 3] ^= 0x10;   // inside segment 0
    damaged[0] ^= 0xFF;           // and the signature
    require(reason_of_decode(damaged) == stx_cng::Reason::not_cng, "a damaged signature is not CNG for the decoder");
    stx_cng::SalvageReport report;
    stx_cng::Dimensions salvaged;
    std::vector<std::uint8_t> out = stx_cng::decode_salvage(damaged, salvaged, report);
    require(report.segments_lost == 1 && report.segments_ok[0] == 0 && report.mosaic_ok, "salvage reports the lost segment");
    std::vector<std::uint8_t> means = test_support::block_means(raw, d);
    stx_cng::Dimensions m = stx_cng::mosaic_dimensions(d);
    for (std::size_t y = 0; y < 96; ++y) {
        for (std::size_t x = 0; x < 256; ++x) {
            for (std::size_t c = 0; c < 3; ++c) {
                const bool lost = y < 32 && x < 128;
                const std::uint8_t want = lost ? means[((y / 16) * m.width + x / 16) * 3 + c] : raw[(y * 256 + x) * 3 + c];
                require(out[(y * 256 + x) * 3 + c] == want, "salvage fills the lost segment from the mosaic");
            }
        }
    }
    damaged[0] ^= 0xFF;
    require(reason_of_decode(damaged) == stx_cng::Reason::damaged, "a damaged segment fails the decoder");
    std::vector<std::uint8_t> version = file;
    version[17] = 2;   // both header copies, with their checksums made to match
    std::vector<std::uint8_t> fields(version.begin() + 8, version.begin() + 36);
    const std::uint32_t crc = test_support::crc32(fields);
    for (std::size_t k = 0; k < 4; ++k) { version[36 + k] = static_cast<std::uint8_t>(crc >> (24 - 8 * k)); }
    std::copy(version.begin() + 8, version.begin() + 40, version.end() - 32);
    require(reason_of_decode(version) == stx_cng::Reason::unsupported_version, "another format version is refused");
}

void error_cases() {
    stx_cng::Dimensions d{.height = 4, .width = 4, .channels = 3};
    std::vector<std::uint8_t> raw(47);
    bool thrown = false;
    try { std::vector<std::uint8_t> f = stx_cng::encode(raw, d); static_cast<void>(f); } catch (const stx_cng::Error& e) { thrown = e.reason() == stx_cng::Reason::sample_count; }
    require(thrown, "sample count");
    thrown = false;
    try { std::vector<std::uint8_t> f = stx_cng::encode(raw, stx_cng::Dimensions{.height = 4, .width = 4, .channels = 2}); static_cast<void>(f); } catch (const stx_cng::Error& e) { thrown = e.reason() == stx_cng::Reason::dimensions; }
    require(thrown, "channels");
    std::vector<std::uint8_t> garbage(100, 7);
    require(reason_of_decode(garbage) == stx_cng::Reason::not_cng, "not a CNG file");
    std::vector<std::uint8_t> good = stx_cng::encode(test_support::samples(d, 0, 1), d);
    std::vector<std::uint8_t> small(good.size());
    thrown = false;
    try { stx_cng::decode_into(good, std::span<std::uint8_t>(small.data(), 47)); } catch (const stx_cng::Error& e) { thrown = e.reason() == stx_cng::Reason::sample_count; }
    require(thrown, "destination size");
}

void collection_cases() {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / "stx_cng_collection_test";
    std::filesystem::create_directories(folder);
    std::vector<stx_cng::collection::ImageSource> sources;
    std::vector<std::vector<std::uint8_t>> raws;
    const std::array<stx_cng::Dimensions, 4> dims = {stx_cng::Dimensions{.height = 20, .width = 30, .channels = 3}, stx_cng::Dimensions{.height = 40, .width = 17, .channels = 4},
                                                     stx_cng::Dimensions{.height = 1, .width = 1, .channels = 3}, stx_cng::Dimensions{.height = 70, .width = 140, .channels = 4}};
    for (std::size_t k = 0; k < dims.size(); ++k) {
        raws.push_back(test_support::samples(dims[k], static_cast<unsigned>(k % 6), static_cast<std::uint32_t>(k)));
        std::filesystem::path path = folder / ("member" + std::to_string(k) + ".raw");
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(raws.back().data()), static_cast<std::streamsize>(raws.back().size()));
        stx_cng::collection::ImageSource source;
        source.id.value = static_cast<std::uint32_t>(100 + k); source.dimensions = dims[k]; source.raw_path = path;
        sources.push_back(source);
    }
    for (stx_cng::collection::Form form : {stx_cng::collection::Form::packed, stx_cng::collection::Form::composite}) {
        std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
        stx_cng::collection::encode(sources, form, stream);
        const std::string bytes = stream.str();
        require(bytes.size() > 8 && static_cast<unsigned char>(bytes[0]) == 0x8A && bytes.compare(1, 3, "CNG") == 0, "collection signature");
        stream.seekg(0);
        stx_cng::collection::Decoded decoded;
        stx_cng::collection::decode(stream, decoded);
        for (std::size_t k = 0; k < sources.size(); ++k) {
            std::ostringstream out;
            stx_cng::collection::write_image(decoded, sources[k].id, out);
            const std::string member = out.str();
            require(member == std::string(raws[k].begin(), raws[k].end()), "collection member");
        }
    }
    std::filesystem::remove_all(folder);
}
}  // namespace

int main() {
    const std::array<std::array<std::size_t, 2>, 12> sizes = {{{1, 1}, {1, 9}, {9, 1}, {5, 7}, {31, 127}, {32, 128}, {33, 129}, {64, 300}, {97, 260}, {200, 300}, {3, 513}, {130, 2}}};
    unsigned cases = 0;
    for (const std::array<std::size_t, 2>& size : sizes) {
        for (std::size_t channels : {3u, 4u}) {
            for (unsigned pattern = 0; pattern < 6; ++pattern) {
                if (pattern == 4 && channels == 3) { continue; }
                image_case(stx_cng::Dimensions{.height = size[0], .width = size[1], .channels = channels}, pattern);
                ++cases;
            }
        }
    }
    damage_case();
    error_cases();
    collection_cases();
    std::cout << "stx_cng round trip: " << cases << " image cases, damage, errors and collections pass\n";
    return 0;
}
