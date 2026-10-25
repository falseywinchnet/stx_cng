#pragma once
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <span>
#include <vector>

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
// CNG standalone image: the phase codec in independent 128x128 frames (stream CNGPHI08). Encode reads exactly
// height*width*channels interleaved RGB8/RGBA8 bytes and writes one complete file. Decode reads one complete file.
// Frames carry a CRC32 each in a checksummed directory; the header fields, the directory and a mosaic layer (the mean
// colour of every 16x16 cell) are each stored twice. A corrupted frame is reported and decoding stops there, except
// in decode_salvage, which decodes every intact frame and fills the others from the mosaic. Errors throw; stream
// output may be partial after an error. The whole compressed file is held in memory during a call, together with
// the image samples; frame engine state is a few hundred kilobytes.
constexpr std::size_t mosaic_cell = 16;

// Dimensions of the mosaic layer of an image of these dimensions: ceil(width / 16) x ceil(height / 16), same channels.
[[nodiscard]] Dimensions mosaic_dimensions(Dimensions dimensions);

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

// The mosaic layer of one complete file: raw interleaved samples of mosaic_dimensions(image), from whichever stored
// copy is intact. Returns the image dimensions. The whole file is read from the stream; only the mosaic is decoded.
Dimensions decode_mosaic(std::istream& source, std::ostream& destination, Measurements& measurements);

// Decodes every frame whose checksum passes into the exactly sized destination and fills the others from the mosaic
// layer (mid grey when the mosaic is lost too). frames_ok receives one byte per frame (1 = decoded exactly), and
// mosaic_ok whether the mosaic decoded. Throws only when neither copy of the header or of the directory can be read.
Dimensions decode_salvage(std::istream& source, std::span<std::uint8_t> destination, std::vector<std::uint8_t>& frames_ok, bool& mosaic_ok, Measurements& measurements);
}
