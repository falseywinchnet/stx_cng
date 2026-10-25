#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Phase-aware causal codec, native reference (docs/PHASE_AWARE_CAUSAL_CODING.md, docs/PHASE_NATIVE_PORT.md).
// Research code: one fixed construction, no search, no tables transmitted. Stream signature CNGPHI07 (V7): CNGPHI05 frames of 128x128 with a checksummed directory.
// The decoder throws nothing and reads nothing it was not given; malformed input is reported by status.
namespace cng_phase {

struct Dimensions {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 0;  // 3 (RGB) or 4 (RGBA), interleaved, row-major
};

enum class Status : std::uint8_t {
    ok,
    bad_dimensions,      // zero extent, channels not 3 or 4, or more than 2^31 samples
    bad_sample_count,    // caller buffer does not hold width * height * channels bytes
    bad_header,          // signature or header fields do not describe a decodable stream
    stream_exhausted,    // the coder read far past the end of the supplied bytes
    bad_directory,       // frame directory inconsistent with the stream length or its checksum
    bad_checksum         // a frame payload does not match its directory checksum
};

constexpr std::size_t kHeaderBytes = 30;  // 8 signature + 18 header fields (width, height, channels, frame side, mosaic length, mosaic CRC32) + 4 CRC32 of the fields, big-endian
constexpr std::uint32_t kMosaicCell = 16;  // side of the cells whose mean colours form the mosaic layer

// Encodes samples (exactly width*height*channels bytes) into destination, which is cleared first.
// Workspace is allocated once per call and released on return; samples and destination are disjoint.
[[nodiscard]] Status encode(std::span<const std::uint8_t> samples, Dimensions dimensions,
                            std::vector<std::uint8_t>& destination);

// One coded residual symbol with the flat model contexts the kernel used for it (research instrumentation:
// the coder-cost oracles re-cost this stream under other coders). zero_context indexes the zero-flag models,
// coarse_context the prefix/suffix families (x 16 prefix states), sign_context the sign models.
struct SymbolTrace {
    std::uint32_t pixel = 0;          // y * width + x
    std::uint32_t zero_context = 0;
    std::uint32_t coarse_context = 0;
    std::uint32_t sign_context = 0;
    std::uint32_t symbol = 0;         // zigzag of the wrapped residual, 0..255
};
// Encodes exactly as encode() and additionally appends every coded symbol to trace (cleared first).
[[nodiscard]] Status encode_traced(std::span<const std::uint8_t> samples, Dimensions dimensions,
                                   std::vector<std::uint8_t>& destination, std::vector<SymbolTrace>& trace);

// Reads the header only; returns the dimensions a decode would produce.
[[nodiscard]] Status read_header(std::span<const std::uint8_t> stream, Dimensions& dimensions);

// Encodes with a chosen frame side (16..255; `encode` uses 128). The stream carries the side.
[[nodiscard]] Status encode_with_frame(std::span<const std::uint8_t> samples, Dimensions dimensions, std::uint32_t frame_side,
                                       std::vector<std::uint8_t>& destination);

// Owned working storage of the engine for one frame of an image with these dimensions and frame side.
[[nodiscard]] std::size_t frame_workspace_bytes(Dimensions dimensions, std::uint32_t frame_side);

// Number of frames (raster order; the last column and row may be smaller) of a stream with these dimensions.
[[nodiscard]] std::uint32_t frame_count(std::span<const std::uint8_t> stream, Dimensions dimensions);

// Dimensions of the mosaic layer of an image: ceil(width / kMosaicCell) x ceil(height / kMosaicCell), same channels.
[[nodiscard]] Dimensions mosaic_dimensions(Dimensions dimensions);

// Decodes the mosaic layer (the mean colour of every kMosaicCell x kMosaicCell cell) into caller-owned storage of
// exactly its sample count, from whichever stored copy has a valid checksum; bad_checksum when neither does.
[[nodiscard]] Status decode_mosaic(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination);

// Decodes every frame whose checksum passes and fills the others from the mosaic layer (mid grey when the mosaic is
// lost too); frames_ok gets one byte per frame (1 = decoded exactly), mosaic_ok whether the mosaic decoded. Fails
// only when the header or the directory cannot be read from either copy.
[[nodiscard]] Status decode_salvage(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination, std::vector<std::uint8_t>& frames_ok, bool& mosaic_ok);

// Frame side recorded in a stream whose header (front or trailing copy) is valid; 0 otherwise.
[[nodiscard]] std::uint32_t frame_side(std::span<const std::uint8_t> stream);

// Decodes one frame into its rectangle of caller-owned storage of exactly width*height*channels bytes; frames are
// independent, so callers may decode them in any order or concurrently into disjoint rectangles of one buffer.
[[nodiscard]] Status decode_frame(std::span<const std::uint8_t> stream, std::uint32_t index, std::span<std::uint8_t> destination);

// Decodes frames first, first + stride, ... into their rectangles of a caller-owned buffer of the whole image
// (one workspace for the set; a worker's share of a threaded decode).
[[nodiscard]] Status decode_frame_range(std::span<const std::uint8_t> stream, std::uint32_t first, std::uint32_t stride, std::span<std::uint8_t> destination);

// Decodes into caller-owned storage of exactly width*height*channels bytes (from read_header).
// Output and stream must be disjoint. Failure before the first sample leaves the output untouched;
// stream_exhausted can leave partial output, which the caller must discard.
[[nodiscard]] Status decode(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination);

}  // namespace cng_phase
