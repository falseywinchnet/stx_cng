#include "stx_cng/codec.hpp"
#include "phase_codec.hpp"
#include <chrono>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace stx_cng {
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
cng_phase::Dimensions engine_dimensions(Dimensions dimensions) {
    require(dimensions.width > 0 && dimensions.height > 0 && (dimensions.channels == 3 || dimensions.channels == 4), "CNG dimensions");
    require(dimensions.width <= 0xFFFFFFFFu && dimensions.height <= 0xFFFFFFFFu, "CNG dimensions");
    cng_phase::Dimensions result;
    result.width = static_cast<std::uint32_t>(dimensions.width);
    result.height = static_cast<std::uint32_t>(dimensions.height);
    result.channels = static_cast<std::uint32_t>(dimensions.channels);
    return result;
}
Dimensions public_dimensions(cng_phase::Dimensions dimensions) {
    return Dimensions{dimensions.height, dimensions.width, dimensions.channels};
}
void raise(cng_phase::Status status) {
    switch (status) {
        case cng_phase::Status::ok: return;
        case cng_phase::Status::bad_dimensions: throw std::runtime_error("CNG dimensions");
        case cng_phase::Status::bad_sample_count: throw std::runtime_error("CNG sample count");
        case cng_phase::Status::bad_header: throw std::runtime_error("CNG header: not a CNGPHI08 stream");
        case cng_phase::Status::stream_exhausted: throw std::runtime_error("CNG stream exhausted");
        case cng_phase::Status::bad_directory: throw std::runtime_error("CNG frame directory");
        case cng_phase::Status::bad_checksum: throw std::runtime_error("CNG frame checksum");
    }
    throw std::runtime_error("CNG status");
}
std::vector<std::uint8_t> read_all(std::istream& source) {
    std::vector<std::uint8_t> bytes;
    std::istreambuf_iterator<char> begin(source), end;
    for (std::istreambuf_iterator<char> it = begin; it != end; ++it) { bytes.push_back(static_cast<std::uint8_t>(*it)); }
    return bytes;
}
void read_exact(std::istream& source, std::vector<std::uint8_t>& bytes) {
    source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<std::size_t>(source.gcount()) == bytes.size(), "CNG input short");
}
void write_all(std::ostream& destination, std::span<const std::uint8_t> bytes) {
    destination.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(destination), "CNG output write");
}
double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

void encode(std::istream& source, std::ostream& destination, Dimensions dimensions, Measurements& measurements) {
    measurements = {};
    cng_phase::Dimensions engine = engine_dimensions(dimensions);
    std::vector<std::uint8_t> samples(static_cast<std::size_t>(engine.width) * engine.height * engine.channels);
    read_exact(source, samples);
    std::vector<std::uint8_t> stream;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    cng_phase::Status status = cng_phase::encode(samples, engine, stream);
    measurements.codec_seconds = seconds_since(start);
    raise(status);
    write_all(destination, stream);
    measurements.frames = cng_phase::frame_count(stream, engine);
    measurements.workspace_bytes = samples.capacity() + stream.capacity() + cng_phase::frame_workspace_bytes(engine, 128);
}

Dimensions decode(std::istream& source, std::ostream& destination, Measurements& measurements) {
    measurements = {};
    std::vector<std::uint8_t> stream = read_all(source);
    cng_phase::Dimensions engine;
    raise(cng_phase::read_header(stream, engine));
    std::vector<std::uint8_t> samples(static_cast<std::size_t>(engine.width) * engine.height * engine.channels);
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    cng_phase::Status status = cng_phase::decode(stream, samples);
    measurements.codec_seconds = seconds_since(start);
    raise(status);
    write_all(destination, samples);
    measurements.frames = cng_phase::frame_count(stream, engine);
    measurements.workspace_bytes = samples.capacity() + stream.capacity() + cng_phase::frame_workspace_bytes(engine, cng_phase::frame_side(stream));
    return public_dimensions(engine);
}

Dimensions mosaic_dimensions(Dimensions dimensions) {
    return public_dimensions(cng_phase::mosaic_dimensions(engine_dimensions(dimensions)));
}

Dimensions decode_mosaic(std::istream& source, std::ostream& destination, Measurements& measurements) {
    measurements = {};
    std::vector<std::uint8_t> stream = read_all(source);
    cng_phase::Dimensions engine;
    raise(cng_phase::read_header(stream, engine));
    cng_phase::Dimensions mosaic = cng_phase::mosaic_dimensions(engine);
    std::vector<std::uint8_t> samples(static_cast<std::size_t>(mosaic.width) * mosaic.height * mosaic.channels);
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    cng_phase::Status status = cng_phase::decode_mosaic(stream, samples);
    measurements.codec_seconds = seconds_since(start);
    raise(status);
    write_all(destination, samples);
    measurements.frames = cng_phase::frame_count(stream, engine);
    measurements.workspace_bytes = samples.capacity() + stream.capacity() + cng_phase::frame_workspace_bytes(mosaic, mosaic.width);
    return public_dimensions(engine);
}

Dimensions decode_salvage(std::istream& source, std::span<std::uint8_t> destination, std::vector<std::uint8_t>& frames_ok, bool& mosaic_ok, Measurements& measurements) {
    measurements = {};
    std::vector<std::uint8_t> stream = read_all(source);
    cng_phase::Dimensions engine;
    raise(cng_phase::read_header(stream, engine));
    require(destination.size() == static_cast<std::size_t>(engine.width) * engine.height * engine.channels, "CNG output extent");
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    cng_phase::Status status = cng_phase::decode_salvage(stream, destination, frames_ok, mosaic_ok);
    measurements.codec_seconds = seconds_since(start);
    raise(status);
    measurements.frames = frames_ok.size();
    measurements.workspace_bytes = stream.capacity() + cng_phase::frame_workspace_bytes(engine, cng_phase::frame_side(stream));
    return public_dimensions(engine);
}

Dimensions decode_into(std::istream& source, std::span<std::uint8_t> destination, Measurements& measurements) {
    measurements = {};
    std::vector<std::uint8_t> stream = read_all(source);
    cng_phase::Dimensions engine;
    raise(cng_phase::read_header(stream, engine));
    require(destination.size() == static_cast<std::size_t>(engine.width) * engine.height * engine.channels, "CNG output extent");
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    cng_phase::Status status = cng_phase::decode(stream, destination);
    measurements.codec_seconds = seconds_since(start);
    raise(status);
    measurements.frames = cng_phase::frame_count(stream, engine);
    measurements.workspace_bytes = stream.capacity() + cng_phase::frame_workspace_bytes(engine, cng_phase::frame_side(stream));
    return public_dimensions(engine);
}
}  // namespace stx_cng
