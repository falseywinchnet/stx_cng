// Malformed-input sweep for the standalone codec and the collection reader. Every input below is decoded through the
// public interfaces; the requirement is that no input crashes, reads outside its buffers or writes outside the
// destination, and that any decode reporting success reproduces the original samples. Built with the address and
// undefined-behaviour sanitizers by the stx_cng_malformed target, so a violation aborts the run.
#include "stx_cng/codec.hpp"
#include "stx_cng/collection.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
// Deterministic sample pattern mixing gradients, edges, repeats and noise.
std::string samples(stx_cng::Dimensions d, std::uint32_t seed) {
    std::string out(d.height * d.width * d.channels, '\0');
    std::uint32_t state = seed * 2654435761u + 12345u;
    for (std::size_t y = 0; y < d.height; ++y) {
        for (std::size_t x = 0; x < d.width; ++x) {
            state = state * 1664525u + 1013904223u;
            for (std::size_t c = 0; c < d.channels; ++c) {
                std::size_t at = (y * d.width + x) * d.channels + c;
                std::uint32_t v = (x * 3 + y * 5 + c * 40) & 255;
                if (((x / 7) + (y / 5)) % 3 == 0) { v = (state >> (8 * c)) & 255; }
                if (x % 23 < 4) { v = 200 - c * 30; }
                out[at] = static_cast<char>(v);
            }
        }
    }
    return out;
}
std::string encode(stx_cng::Dimensions d, const std::string& raw) {
    std::istringstream source(raw);
    std::ostringstream encoded;
    stx_cng::Measurements m;
    stx_cng::encode(source, encoded, d, m);
    return encoded.str();
}
// Decodes a (possibly damaged) stream; returns true when decoding reported success. The destination has a guard
// region after the expected extent that must remain untouched.
bool decode_guarded(const std::string& stream, std::size_t expected, std::string& pixels) {
    std::istringstream source(stream);
    pixels.assign(expected + 64, static_cast<char>(0x7E));
    std::span<std::uint8_t> destination(reinterpret_cast<std::uint8_t*>(pixels.data()), expected);
    stx_cng::Measurements m;
    bool ok = false;
    try {
        stx_cng::Dimensions ignored = stx_cng::decode_into(source, destination, m);
        static_cast<void>(ignored);
        ok = true;
    } catch (const std::runtime_error&) {
        ok = false;
    }
    for (std::size_t i = expected; i < pixels.size(); ++i) { require(pixels[i] == static_cast<char>(0x7E), "guard region written"); }
    return ok;
}
// Stream decode into an ostream for the same input (exercises the allocating path and header-derived sizes).
void decode_stream(const std::string& stream) {
    std::istringstream source(stream);
    std::ostringstream pixels;
    stx_cng::Measurements m;
    try {
        stx_cng::Dimensions ignored = stx_cng::decode(source, pixels, m);
        static_cast<void>(ignored);
    } catch (const std::runtime_error&) {
    }
}
std::uint32_t crc32_of(const std::string& bytes) {
    static std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) { c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : bytes) { c = table[(c ^ b) & 0xFFu] ^ (c >> 8); }
    return c ^ 0xFFFFFFFFu;
}
void put_u32(std::string& s, std::size_t at, std::uint32_t v) {
    s[at] = static_cast<char>(v >> 24); s[at + 1] = static_cast<char>(v >> 16); s[at + 2] = static_cast<char>(v >> 8); s[at + 3] = static_cast<char>(v);
}
std::uint32_t get_u32(const std::string& s, std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(s[at])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 16)
           | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 3]));
}
struct Counts { std::size_t inputs = 0, accepted = 0, exact = 0; };
void sweep_image(stx_cng::Dimensions d, std::uint32_t seed, Counts& counts, std::uint32_t& rng) {
    std::string raw = samples(d, seed);
    std::string stream = encode(d, raw);
    std::string pixels;
    require(decode_guarded(stream, raw.size(), pixels) && pixels.compare(0, raw.size(), raw) == 0, "intact stream round trip");
    // Damage that leaves checksums as they were must be rejected or decode exactly; a payload whose checksum was
    // recomputed over arbitrary bytes (forged) may decode to anything, but must still never crash or write outside.
    auto check = [&](const std::string& damaged, bool forged = false) {
        ++counts.inputs;
        if (decode_guarded(damaged, raw.size(), pixels)) {
            ++counts.accepted;
            if (pixels.compare(0, raw.size(), raw) == 0) { ++counts.exact; } else { require(forged, "accepted stream with different pixels"); }
        }
        decode_stream(damaged);
    };
    // Every truncation length.
    for (std::size_t length = 0; length < stream.size(); ++length) { check(stream.substr(0, length)); }
    // Every single-bit flip of the header and directory, and of the first and last 64 payload bytes.
    // Directory extent from the header: frames = ceil(W/side) * ceil(H/side).
    std::uint32_t side = static_cast<unsigned char>(stream[17]);
    std::uint32_t across = (d.width + side - 1) / side, down = (d.height + side - 1) / side;
    std::size_t directory_block = static_cast<std::size_t>(across) * down * 8 + 4;
    std::size_t header_and_directory = 22 + directory_block;
    for (std::size_t byte = 0; byte < stream.size(); ++byte) {
        bool near_edge = byte < header_and_directory + 64 || byte + 64 >= stream.size();
        if (!near_edge) { continue; }
        for (int bit = 0; bit < 8; ++bit) {
            std::string damaged = stream;
            damaged[byte] = static_cast<char>(damaged[byte] ^ (1 << bit));
            check(damaged);
        }
    }
    // Payloads replaced by arbitrary bytes whose checksum is made valid, so the frame engine itself sees them.
    std::size_t first_payload = header_and_directory;
    for (int trial = 0; trial < 160; ++trial) {
        std::string damaged = stream;
        std::uint32_t length0 = get_u32(stream, 22);
        std::string payload;
        rng = rng * 1664525u + 1013904223u;
        std::size_t length = (trial % 4 == 0) ? 0 : (trial % 4 == 1) ? 1 + (rng >> 20) % 8 : (trial % 4 == 2) ? length0 : (rng >> 8) % (2 * length0 + 1);
        for (std::size_t i = 0; i < length; ++i) {
            rng = rng * 1664525u + 1013904223u;
            unsigned char b = static_cast<unsigned char>(rng >> 24);
            if (trial % 8 == 5) { b = 0xFF; }
            if (trial % 8 == 6) { b = 0x00; }
            if (trial % 8 == 7 && i < stream.size() - first_payload) { b = static_cast<unsigned char>(stream[first_payload + i] ^ ((i % 13 == 0) ? 0x40 : 0)); }
            payload.push_back(static_cast<char>(b));
        }
        damaged.replace(first_payload, length0, payload);
        put_u32(damaged, 22, static_cast<std::uint32_t>(payload.size()));
        put_u32(damaged, 26, crc32_of(payload));
        std::string directory = damaged.substr(22, directory_block - 4);
        put_u32(damaged, 22 + directory.size(), crc32_of(directory));
        damaged.replace(damaged.size() - 14 - directory_block, directory_block, damaged.substr(22, directory_block));
        check(damaged, true);
    }
    // Header claims: huge dimensions, zero dimensions, odd channels, frame side extremes, with the rest intact.
    const std::array<std::array<std::uint32_t, 2>, 8> dims = {{{0, 1}, {1, 0}, {0xFFFFFFFFu, 0xFFFFFFFFu}, {65536, 65536}, {46341, 46341}, {1, 0x7FFFFFFFu}, {static_cast<std::uint32_t>(d.width + 1), static_cast<std::uint32_t>(d.height)}, {static_cast<std::uint32_t>(d.width), static_cast<std::uint32_t>(d.height + 1)}}};
    auto claim = [&](std::uint32_t w, std::uint32_t h, unsigned channels, unsigned side_value) {
        std::string damaged = stream;
        put_u32(damaged, 8, w); put_u32(damaged, 12, h); damaged[16] = static_cast<char>(channels); damaged[17] = static_cast<char>(side_value);
        put_u32(damaged, 18, crc32_of(damaged.substr(8, 10)));
        check(damaged, true);                                    // front copy forged, tail copy intact
        damaged.replace(damaged.size() - 14, 14, damaged.substr(8, 14));
        check(damaged, true);                                    // both copies forged
    };
    for (const auto& wh : dims) { claim(wh[0], wh[1], static_cast<unsigned>(d.channels), side); }
    for (unsigned channels : {0u, 1u, 2u, 5u, 255u}) { claim(static_cast<std::uint32_t>(d.width), static_cast<std::uint32_t>(d.height), channels, side); }
    for (unsigned side_value : {0u, 1u, 15u, 16u, 17u, 127u, 129u, 255u}) { claim(static_cast<std::uint32_t>(d.width), static_cast<std::uint32_t>(d.height), static_cast<unsigned>(d.channels), side_value); }
    // Damage confined to one index copy must be repaired from the other: zero the whole front index, then the tail.
    {
        std::string damaged = stream;
        for (std::size_t i = 8; i < header_and_directory; ++i) { damaged[i] = 0; }
        require(decode_guarded(damaged, raw.size(), pixels) && pixels.compare(0, raw.size(), raw) == 0, "repair from the trailing index copy");
        damaged = stream;
        for (std::size_t i = stream.size() - 14 - directory_block; i < stream.size(); ++i) { damaged[i] = static_cast<char>(0xFF); }
        require(decode_guarded(damaged, raw.size(), pixels) && pixels.compare(0, raw.size(), raw) == 0, "decode with a damaged trailing index copy");
        ++counts.inputs; ++counts.inputs; counts.accepted += 2; counts.exact += 2;
    }
}
void sweep_collections(Counts& counts) {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / "stx_cng_malformed";
    std::filesystem::create_directories(folder);
    stx_cng::Dimensions a{20, 30, 3}, b{9, 7, 4};
    std::string raw_a = samples(a, 11), raw_b = samples(b, 12);
    std::vector<stx_cng_collection::ImageSource> images;
    for (std::size_t index = 0; index < 2; ++index) {
        std::filesystem::path path = folder / (std::to_string(index) + ".raw");
        std::ofstream output(path, std::ios::binary);
        const std::string& raw = index == 0 ? raw_a : raw_b;
        output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        output.close();
        require(static_cast<bool>(output), "fixture file");
        images.push_back({{static_cast<std::uint32_t>(index)}, index == 0 ? a : b, path});
    }
    for (stx_cng_collection::Form form : {stx_cng_collection::Form::packed, stx_cng_collection::Form::composite}) {
        std::stringstream encoded;
        stx_cng_collection::Measurements m;
        stx_cng_collection::encode(images, form, encoded, m);
        std::string stream = encoded.str();
        auto check = [&](const std::string& damaged) {
            ++counts.inputs;
            std::stringstream source(damaged);
            stx_cng_collection::Decoded decoded;
            stx_cng_collection::Measurements dm;
            try {
                stx_cng_collection::decode(source, decoded, dm);
                ++counts.accepted;
                std::ostringstream out_a, out_b;
                stx_cng_collection::write_image(decoded, stx_cng_collection::ImageId{0}, out_a);
                stx_cng_collection::write_image(decoded, stx_cng_collection::ImageId{1}, out_b);
                if (out_a.str() == raw_a && out_b.str() == raw_b) { ++counts.exact; } else { require(false, "accepted collection with different members"); }
            } catch (const std::exception&) {
            }
        };
        check(stream);
        for (std::size_t length = 0; length < stream.size(); ++length) { check(stream.substr(0, length)); }
        for (std::size_t byte = 0; byte < stream.size(); ++byte) {
            for (int bit = 0; bit < 8; bit += 1) {
                std::string damaged = stream; damaged[byte] = static_cast<char>(damaged[byte] ^ (1 << bit)); check(damaged);
            }
        }
    }
    std::filesystem::remove_all(folder);
}
}  // namespace

int main() {
    Counts counts;
    std::uint32_t rng = 7;
    const std::array<stx_cng::Dimensions, 6> cases = {{{1, 1, 3}, {1, 1, 4}, {5, 7, 3}, {17, 3, 4}, {40, 300, 3}, {130, 129, 4}}};
    std::uint32_t seed = 1;
    for (stx_cng::Dimensions d : cases) { sweep_image(d, seed++, counts, rng); }
    sweep_collections(counts);
    std::cout << "PASS: " << counts.inputs << " malformed inputs, " << counts.accepted << " accepted, " << counts.exact
              << " of those exact, no sanitizer report\n";
    return 0;
}
