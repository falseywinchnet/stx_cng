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
    std::size_t workspace_bytes = 0;   // owned storage of the call: sample buffer, stream buffer and one frame's engine state
    std::size_t frames = 0;            // frames of the image (128x128 grid, last column and row smaller)
    double codec_seconds = 0.0;        // time inside the frame engine, excluding stream reads and writes
};
// CNG standalone image: the phase codec in independent 128x128 frames (stream CNGPHI07). Encode reads exactly
// height*width*channels interleaved RGB8/RGBA8 bytes and writes one complete file. Decode reads one complete file.
// Frames carry a CRC32 each in a checksummed directory; a corrupted frame is reported and decoding stops there.
// Errors throw; stream output may be partial after an error. The whole compressed file is held in memory during a
// call, together with the image samples; frame engine state is a few hundred kilobytes.
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
