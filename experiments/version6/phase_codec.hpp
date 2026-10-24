#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Phase-aware causal codec, native reference (docs/PHASE_AWARE_CAUSAL_CODING.md, docs/PHASE_NATIVE_PORT.md).
// Research code: one fixed construction, no search, no tables transmitted. Stream signature CNGPHI03.
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
    stream_exhausted     // the coder read far past the end of the supplied bytes
};

constexpr std::size_t kHeaderBytes = 17;  // 8 signature + 4 width + 4 height + 1 channels, big-endian

// Encodes samples (exactly width*height*channels bytes) into destination, which is cleared first.
// Workspace is allocated once per call and released on return; samples and destination are disjoint.
[[nodiscard]] Status encode(std::span<const std::uint8_t> samples, Dimensions dimensions,
                            std::vector<std::uint8_t>& destination);

// Reads the header only; returns the dimensions a decode would produce.
[[nodiscard]] Status read_header(std::span<const std::uint8_t> stream, Dimensions& dimensions);

// Decodes into caller-owned storage of exactly width*height*channels bytes (from read_header).
// Output and stream must be disjoint. Failure before the first sample leaves the output untouched;
// stream_exhausted can leave partial output, which the caller must discard.
[[nodiscard]] Status decode(std::span<const std::uint8_t> stream, std::span<std::uint8_t> destination);

}  // namespace cng_phase
