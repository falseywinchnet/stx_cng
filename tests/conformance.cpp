// Conformance vectors (tests/vectors/): every file must decode exactly to its samples, at one and four threads, and
// its mosaic must be the block means of those samples. Encoding the samples must give a file that decodes to them;
// when the linked libzstd is the version that wrote the vectors (named in LIST), it must give the vector byte for byte.
#include "stx_cng/codec.hpp"
#include "test_support.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <zstd.h>

namespace {
using test_support::require;

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return bytes;
}
}  // namespace

int main(int argc, char** argv) {
    require(argc == 2, "usage: stx_cng_conformance <vector folder>");
    const std::filesystem::path folder = argv[1];
    std::ifstream list(folder / "LIST");
    require(static_cast<bool>(list), "the vector list");
    std::string word;
    unsigned zstd_version = 0;
    list >> word >> zstd_version;
    require(word == "zstd", "the vector list names its libzstd version");
    const bool same_zstd = ZSTD_versionNumber() == zstd_version;
    unsigned vectors = 0, identical = 0;
    std::string name;
    std::size_t width = 0, height = 0, channels = 0;
    while (list >> name >> width >> height >> channels) {
        const stx_cng::Dimensions d{.height = height, .width = width, .channels = channels};
        const std::vector<std::uint8_t> file = read_file(folder / (name + ".cng"));
        const std::vector<std::uint8_t> raw = read_file(folder / (name + ".rgb"));
        stx_cng::Dimensions actual;
        require(stx_cng::decode(file, actual) == raw, name.c_str());
        stx_cng::DecodeOptions four;
        four.threads = 4;
        require(stx_cng::decode(file, actual, four) == raw, name.c_str());
        stx_cng::Dimensions mosaic;
        require(stx_cng::decode_mosaic(file, mosaic) == test_support::block_means(raw, d), name.c_str());
        const std::vector<std::uint8_t> encoded = stx_cng::encode(raw, d);
        require(stx_cng::decode(encoded, actual) == raw, name.c_str());
        if (same_zstd) { require(encoded == file, name.c_str()); ++identical; }
        ++vectors;
    }
    require(vectors > 0, "vectors");
    std::cout << "stx_cng conformance: " << vectors << " vectors decode exactly; " << identical << " re-encoded byte for byte"
              << (same_zstd ? "" : " (libzstd differs from the vectors' version, so encoder identity is not checked)") << "\n";
    return 0;
}
