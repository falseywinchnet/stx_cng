#pragma once
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <span>

namespace stx_cng {
struct Dimensions {
    std::size_t height = 0;
    std::size_t width = 0;
    std::size_t channels = 0;
};
struct Measurements {
    std::size_t workspace_bytes = 0;
    std::size_t stored_bands = 0;
    std::size_t compressed_bands = 0;
    double transform_seconds = 0.0;
    double entropy_seconds = 0.0;
    double sequence_seconds = 0.0;
    double literal_seconds = 0.0;
    double reconstruction_seconds = 0.0;
};
// CSP v3, stored literals are original samples; compressed literals remain
// gradient residuals. Fixed 1 MiB bands; borrow distinct streams synchronously.
// Encode reads exactly height*width*channels interleaved RGB8/RGBA8 bytes.
// Errors throw and can leave partial stream output. Internal owners retain
// neither a complete image nor a complete file. Workspace sums all owned
// capacities and the cngcompress handle; process RSS is separate.
// Version 2 inputs are rejected before pixel output; use the v2 release to migrate.
void encode(std::istream& source, std::ostream& destination,
            Dimensions dimensions, Measurements& measurements);
[[nodiscard]] Dimensions decode(std::istream& source, std::ostream& destination,
                                Measurements& measurements);
// Caller output must be disjoint from all bytes backing the input stream and
// have exactly the image's sample count. Header/extent errors leave it intact;
// later errors can leave partial pixels. No output borrow survives this call.
[[nodiscard]] Dimensions decode_into(std::istream& source,
                                     std::span<std::uint8_t> destination,
                                     Measurements& measurements);
}
