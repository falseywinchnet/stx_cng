// CNG collections: a set of 1 to 4,096 images stored in one of two explicitly chosen forms.
//   packed     every member is a complete standalone CNG file in its own chunk, decoded on its own;
//   composite  one complete CNG file holds a canvas with the members stacked vertically, decoded in full, then sliced.
// Members keep their dimensions and channel counts; a composite canvas is BGR when every member is BGR, otherwise BGRA.
// Failures throw stx_cng::Error with Reason::collection (or the reason of the member image that failed).
#pragma once
#include "codec.hpp"
#include <filesystem>
#include <ostream>
#include <istream>
#include <span>
#include <vector>

namespace stx_cng::collection {

enum class Form : std::uint32_t { packed = 0, composite = 1 };

struct ImageId { std::uint32_t value = 0; };
struct FileOffset { std::uint64_t bytes = 0; };

// One member to encode: its identity, dimensions and a file of exactly height * width * channels raw samples.
struct ImageSource {
    ImageId id;
    Dimensions dimensions;
    std::filesystem::path raw_path;
};

// One member as stored: its rectangle in the canvas (composite) and the extent of the chunk holding its file.
struct Entry {
    ImageId id;
    Dimensions dimensions;
    std::size_t x = 0;
    std::size_t y = 0;
    FileOffset offset;
    std::uint64_t length = 0;
};

struct Layout {
    Form form = Form::packed;
    Dimensions canvas;   // composite only
    std::vector<Entry> entries;
};

// Encodes the members in `form` to `destination`, which must be seekable, distinct from every input and at offset
// zero. Input files must not change during encoding. A failure can leave partial output.
void encode(std::span<const ImageSource> images, Form form, std::ostream& destination);

// The decoded collection: packed, one sample plane per member; composite, exactly one plane holding the canvas.
struct Decoded {
    Layout layout;
    std::vector<std::vector<std::uint8_t>> planes;
};

// Decodes every member (packed) or the canvas (composite) from a seekable `source`.
void decode(std::istream& source, Decoded& destination);

// Writes the samples of member `id` of an already decoded collection to `destination`, with its original channels and
// every sample, including the colour beneath alpha 0.
void write_image(const Decoded& source, ImageId id, std::ostream& destination);

}  // namespace stx_cng::collection
