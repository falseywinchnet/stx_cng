// CNG 1.0 images: encoding, decoding, inspection, the mosaic layer and salvage.
//
// Samples are interleaved RGB8 or RGBA8 bytes in row-major order without row padding. The codec preserves bytes: it
// performs no colour conversion and no premultiplication, and the colour beneath alpha 0 is kept. Every function is
// synchronous, borrows its arguments for the duration of the call only, and reports failure by throwing
// stx_cng::Error. A failed decode can leave its destination partially written.
#pragma once
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace stx_cng {

struct Dimensions {
    std::size_t height = 0;
    std::size_t width = 0;
    std::size_t channels = 0;   // 3 (RGB8) or 4 (RGBA8)
};

// Format constants of CNG 1.0.
inline constexpr std::uint8_t format_version = 1;
inline constexpr std::size_t segment_width = 128;   // segments are 128 x 32 pixels, the last column and row smaller
inline constexpr std::size_t segment_height = 32;
inline constexpr std::size_t mosaic_cell = 16;      // the mosaic holds the mean colour of every 16 x 16 cell
inline constexpr std::size_t maximum_samples = std::size_t{1} << 31;   // width * height * channels

enum class Reason : std::uint8_t {
    dimensions,           // zero width or height, channels other than 3 or 4, or more than 2^31 samples
    sample_count,         // a sample buffer whose size does not match the dimensions
    not_cng,              // the signature or both copies of the header fields
    unsupported_version,  // a format version, or segment size, this library does not read
    directory,            // both copies of the segment directory, or payload extents
    tables,               // both copies of the tables, or their contents
    damaged,              // a segment or the mosaic failed its checksum or its decoding
    input,                // a stream that could not be read or written
    collection,           // a collection's structure: signature, header, directory, chunk or member extents
};

class Error : public std::runtime_error {
public:
    Error(Reason reason, const char* message) : std::runtime_error(message), reason_(reason) {}
    [[nodiscard]] Reason reason() const { return reason_; }

private:
    Reason reason_;
};

struct EncodeOptions {
    unsigned threads = 1;   // workers over the segments; 0 uses one per available core. The file is the same for every count.
};

struct DecodeOptions {
    unsigned threads = 1;   // segments decode independently; 0 uses one thread per available core
};

// ------------------------------------------------------------------ encoding
// One complete file from exactly height * width * channels samples.
[[nodiscard]] std::vector<std::uint8_t> encode(std::span<const std::uint8_t> samples, Dimensions dimensions, EncodeOptions options = {});

// Reads exactly height * width * channels samples from `samples` and writes one complete file to `file`.
void encode(std::istream& samples, std::ostream& file, Dimensions dimensions, EncodeOptions options = {});

// ------------------------------------------------------------------ inspection
// The dimensions in a file's header (either copy), after checking the header and the directory; decodes nothing.
[[nodiscard]] Dimensions read_dimensions(std::span<const std::uint8_t> file);

// ceil(width / 16) x ceil(height / 16), same channels.
[[nodiscard]] Dimensions mosaic_dimensions(Dimensions image);

// ceil(width / 128) * ceil(height / 32).
[[nodiscard]] std::size_t segment_count(Dimensions image);

// ------------------------------------------------------------------ decoding
// Decodes the whole image into `samples`, which must hold exactly height * width * channels bytes and must not overlap
// `file`. Returns the dimensions.
Dimensions decode_into(std::span<const std::uint8_t> file, std::span<std::uint8_t> samples, DecodeOptions options = {});

// Decodes the whole image into owned storage; sets `dimensions`.
[[nodiscard]] std::vector<std::uint8_t> decode(std::span<const std::uint8_t> file, Dimensions& dimensions, DecodeOptions options = {});

// Reads one complete file from `file` to its end and writes the samples to `samples`. Returns the dimensions.
Dimensions decode(std::istream& file, std::ostream& samples, DecodeOptions options = {});

// The mosaic layer alone, from whichever stored copy is intact: mosaic_dimensions(image) samples. Sets `mosaic` to
// those dimensions.
[[nodiscard]] std::vector<std::uint8_t> decode_mosaic(std::span<const std::uint8_t> file, Dimensions& mosaic);

// ------------------------------------------------------------------ salvage
struct SalvageReport {
    std::vector<std::uint8_t> segments_ok;   // one byte per segment in raster order: 1 = decoded exactly
    std::size_t segments_lost = 0;
    bool mosaic_ok = false;                  // a mosaic copy decoded; lost segments were filled from it
};

// Decodes a possibly damaged file as far as it goes: every segment whose checksum passes exactly, the others filled
// with the mosaic colour of their cells (mid grey when both mosaic copies are lost). Tolerates a damaged signature.
// Throws only when no copy of the header fields, the directory or the tables can be read. Sets `dimensions`.
[[nodiscard]] std::vector<std::uint8_t> decode_salvage(std::span<const std::uint8_t> file, Dimensions& dimensions, SalvageReport& report);

}  // namespace stx_cng
