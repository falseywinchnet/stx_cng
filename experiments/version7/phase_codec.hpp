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

constexpr std::size_t kHeaderBytes = 18;  // 8 signature + 4 width + 4 height + 1 channels + 1 frame side, big-endian

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

// Number of frames (raster order; the last column and row may be smaller) of a stream with these dimensions.
[[nodiscard]] std::uint32_t frame_count(std::span<const std::uint8_t> stream, Dimensions dimensions);

// Decodes one frame into its rectangle of caller-owned storage of exactly width*height*channels bytes; frames are
// independent, so callers may decode them in any order or concurrently into disjoint rectangles of one buffer.
[[nodiscard]] Status decode_frame(std::span<const std::uint8_t> stream, std::uint32_t index, std::span<std::uint8_t> destination);

// Decodes into caller-owned storage of exactly width*height*channels bytes (from read_header).
// Output and stream must be disjoint. Failure before the first sample leaves the output untouched;
// stream_exhausted can leave partial output, which the caller must discard.
[[nodiscard]] Status decode(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination);

}  // namespace cng_phase
