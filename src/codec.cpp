// The public CNG 1.0 interface over the codec engine (cng_codec.hpp): argument checks, owned buffers, stream
// conveniences and exceptions.
#include "stx_cng/codec.hpp"
#include "cng_codec.hpp"

#include <algorithm>
#include <iterator>
#include <thread>

namespace stx_cng {
namespace {

void raise(detail::Status status) {
    switch (status) {
        case detail::Status::ok: return;
        case detail::Status::bad_dimensions: throw Error(Reason::dimensions, "CNG: dimensions out of range");
        case detail::Status::bad_sample_count: throw Error(Reason::sample_count, "CNG: sample count does not match the dimensions");
        case detail::Status::bad_header: throw Error(Reason::not_cng, "CNG: not a CNG file, or both header copies are damaged");
        case detail::Status::unsupported_version: throw Error(Reason::unsupported_version, "CNG: unsupported format version");
        case detail::Status::bad_directory: throw Error(Reason::directory, "CNG: both directory copies are damaged");
        case detail::Status::bad_tables: throw Error(Reason::tables, "CNG: both table copies are damaged");
        case detail::Status::bad_payload: throw Error(Reason::damaged, "CNG: a segment is damaged");
    }
    throw Error(Reason::damaged, "CNG: unknown status");
}

[[nodiscard]] detail::Dimensions engine_dimensions(Dimensions d) {
    if (d.width == 0 || d.height == 0 || (d.channels != 3 && d.channels != 4)) { throw Error(Reason::dimensions, "CNG: dimensions out of range"); }
    if (d.width > 0xFFFFFFFFu || d.height > 0xFFFFFFFFu || d.width * d.height > maximum_samples / d.channels) {
        throw Error(Reason::dimensions, "CNG: dimensions out of range");
    }
    detail::Dimensions result;
    result.width = static_cast<std::uint32_t>(d.width);
    result.height = static_cast<std::uint32_t>(d.height);
    result.channels = static_cast<std::uint32_t>(d.channels);
    return result;
}

[[nodiscard]] Dimensions public_dimensions(detail::Dimensions d) {
    Dimensions result;
    result.height = d.height; result.width = d.width; result.channels = d.channels;
    return result;
}

[[nodiscard]] std::size_t sample_count(Dimensions d) { return d.height * d.width * d.channels; }

[[nodiscard]] std::uint32_t worker_count(unsigned requested) {
    unsigned threads = requested;
    if (threads == 0) { threads = std::max(1u, std::thread::hardware_concurrency()); }
    return static_cast<std::uint32_t>(threads);
}

[[nodiscard]] std::vector<std::uint8_t> read_to_end(std::istream& source) {
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    if (source.bad()) { throw Error(Reason::input, "CNG: input stream failed"); }
    return bytes;
}

void write_all(std::ostream& destination, std::span<const std::uint8_t> bytes) {
    destination.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!destination) { throw Error(Reason::input, "CNG: output stream failed"); }
}

}  // namespace

std::vector<std::uint8_t> encode(std::span<const std::uint8_t> samples, Dimensions dimensions, EncodeOptions options) {
    const detail::Dimensions engine = engine_dimensions(dimensions);
    if (samples.size() != sample_count(dimensions)) { throw Error(Reason::sample_count, "CNG: sample count does not match the dimensions"); }
    std::vector<std::uint8_t> file;
    raise(detail::encode(samples, engine, file, worker_count(options.threads)));
    return file;
}

void encode(std::istream& samples, std::ostream& file, Dimensions dimensions, EncodeOptions options) {
    static_cast<void>(engine_dimensions(dimensions));
    std::vector<std::uint8_t> buffer(sample_count(dimensions));
    samples.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    if (static_cast<std::size_t>(samples.gcount()) != buffer.size()) { throw Error(Reason::sample_count, "CNG: sample input is short"); }
    std::vector<std::uint8_t> encoded = encode(buffer, dimensions, options);
    write_all(file, encoded);
}

Dimensions read_dimensions(std::span<const std::uint8_t> file) {
    detail::Dimensions d;
    raise(detail::read_dimensions(file, d));
    return public_dimensions(d);
}

Dimensions mosaic_dimensions(Dimensions image) { return public_dimensions(detail::mosaic_dimensions(engine_dimensions(image))); }

std::size_t segment_count(Dimensions image) { return static_cast<std::size_t>(detail::segment_count(engine_dimensions(image))); }

Dimensions decode_into(std::span<const std::uint8_t> file, std::span<std::uint8_t> samples, DecodeOptions options) {
    const Dimensions d = read_dimensions(file);
    if (samples.size() != sample_count(d)) { throw Error(Reason::sample_count, "CNG: destination size does not match the image"); }
    raise(detail::decode(file, samples, worker_count(options.threads)));
    return d;
}

std::vector<std::uint8_t> decode(std::span<const std::uint8_t> file, Dimensions& dimensions, DecodeOptions options) {
    dimensions = read_dimensions(file);
    std::vector<std::uint8_t> samples(sample_count(dimensions));
    raise(detail::decode(file, samples, worker_count(options.threads)));
    return samples;
}

Dimensions decode(std::istream& file, std::ostream& samples, DecodeOptions options) {
    const std::vector<std::uint8_t> bytes = read_to_end(file);
    Dimensions d;
    const std::vector<std::uint8_t> decoded = decode(bytes, d, options);
    write_all(samples, decoded);
    return d;
}

std::vector<std::uint8_t> decode_mosaic(std::span<const std::uint8_t> file, Dimensions& mosaic) {
    const Dimensions image = read_dimensions(file);
    mosaic = mosaic_dimensions(image);
    std::vector<std::uint8_t> samples(sample_count(mosaic));
    raise(detail::decode_mosaic(file, samples));
    return samples;
}

std::vector<std::uint8_t> decode_salvage(std::span<const std::uint8_t> file, Dimensions& dimensions, SalvageReport& report) {
    report = SalvageReport{};
    std::vector<std::uint8_t> samples;
    detail::Dimensions d;
    raise(detail::decode_salvage(file, samples, d, report.segments_ok, report.mosaic_ok));
    dimensions = public_dimensions(d);
    for (std::uint8_t ok : report.segments_ok) { if (ok == 0) { ++report.segments_lost; } }
    return samples;
}

}  // namespace stx_cng
