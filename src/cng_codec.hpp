// CNG 1.0 codec engine (internal): encode and decode complete files from and into caller-owned memory.
//
// A file is a signature, checksummed header fields, a directory of 128 x 32 segment payloads (raster order, the last
// column and row smaller), per-image rANS tables (one zstd frame), a 16 x 16-cell mosaic, the segment payloads, and
// second copies of the mosaic, tables (images of two or more segments), directory and header fields. Segments decode
// independently given the tables.
//
// Ownership: every function borrows its inputs for the duration of the call and retains nothing. Sample storage is
// exactly width * height * channels interleaved bytes (RGB8 or RGBA8, rows without padding) and must not overlap the
// file. A failed decode can leave its destination partially written.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stx_cng::detail {

struct Dimensions {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;   // 3 (RGB8) or 4 (RGBA8)
};

enum class Status : std::uint8_t {
    ok,
    bad_dimensions,       // encoder: zero size, channels other than 3 or 4, or more than 2^31 samples
    bad_sample_count,     // a sample span does not match the dimensions
    bad_header,           // decoder: signature, both header copies, or header fields
    unsupported_version,  // decoder: a format version, or segment size, this release does not read
    bad_directory,        // decoder: both directory copies, or payload extents
    bad_tables,           // decoder: both table copies, the zstd frame, or the table body; encoder: zstd failed
    bad_payload,          // decoder: a segment or the mosaic failed its checksum or its decoding
};

// Encodes `samples` of `dimensions` into `file` (replacing its contents) with `threads` workers (1 = serial); the file
// is the same for every thread count.
[[nodiscard]] Status encode(std::span<const std::uint8_t> samples, Dimensions dimensions, std::vector<std::uint8_t>& file, std::uint32_t threads);

// Reads and checks the header (either copy) and the directory; decodes nothing.
[[nodiscard]] Status read_dimensions(std::span<const std::uint8_t> file, Dimensions& dimensions);

// Decodes every segment (any failure fails the call) with `threads` workers (1 = serial) into exactly sized storage.
[[nodiscard]] Status decode(std::span<const std::uint8_t> file, std::span<std::uint8_t> destination, std::uint32_t threads);

// Decodes the mosaic layer, the mean colour of every 16 x 16 cell, into mosaic_dimensions(image) samples.
[[nodiscard]] Status decode_mosaic(std::span<const std::uint8_t> file, std::span<std::uint8_t> destination);

// Salvage: decodes every segment whose checksum passes and fills the others from the mosaic (mid grey without one).
// Tolerates a damaged signature; needs one intact copy of the header fields, the directory and the tables. Sizes
// `destination` and sets `dimensions` from the header; sets segments_ok[i] to 1 for every segment decoded exactly and
// mosaic_ok when a mosaic copy decoded.
[[nodiscard]] Status decode_salvage(std::span<const std::uint8_t> file, std::vector<std::uint8_t>& destination, Dimensions& dimensions,
                                    std::vector<std::uint8_t>& segments_ok, bool& mosaic_ok);

// ceil(width / 16) x ceil(height / 16), same channels.
[[nodiscard]] Dimensions mosaic_dimensions(Dimensions dimensions);

// Segments of an image: ceil(width / 128) * ceil(height / 32).
[[nodiscard]] std::uint64_t segment_count(Dimensions dimensions);

}  // namespace stx_cng::detail
