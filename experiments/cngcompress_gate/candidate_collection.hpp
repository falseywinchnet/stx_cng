#pragma once
#include "../whole_codec/candidate_codec.hpp"
#include <filesystem>
#include <vector>

namespace cng_candidate_collection {
enum class Form : std::uint32_t { packed = 0, composite = 1 };
struct ImageId { std::uint32_t value = 0; };
struct FileOffset { std::uint64_t bytes = 0; };
using Dimensions = cng_candidate::Dimensions;
struct ImageSource {
    ImageId id;
    Dimensions dimensions;
    std::filesystem::path raw_path;
};
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
    Dimensions canvas;
    std::vector<Entry> entries;
};
struct Measurements {
    std::size_t workspace_bytes = 0;
};
// Explicit collection form, not an automatic codec selector. Packed members
// are complete private CNE images. Composite is one complete private CNE canvas with
// slices, decoded in full. Input paths must remain unchanged during encoding.
// The output is distinct, seekable, and at zero. Errors can leave partial output.
// Composite input is synthesized one row at a time; no full canvas is retained.
void encode(std::span<const ImageSource> images, Form form,
            std::ostream& destination, Measurements& measurements);
// Packed: one owned plane per entry. Composite: exactly one owned canvas.
// Decode owns the complete decoded collection; it provides no independent
// composite member decoding. Errors may leave partial, invalid destination.
struct Decoded {
    Layout layout;
    std::vector<std::vector<std::uint8_t>> planes;
};
void decode(std::istream& source, Decoded& destination, Measurements& measurements);
// Copy the selected slice from an already fully decoded collection. Preserve
// original channels and every sample, including hidden RGB. No retained borrow.
void write_image(const Decoded& source, ImageId id, std::ostream& destination);
}
