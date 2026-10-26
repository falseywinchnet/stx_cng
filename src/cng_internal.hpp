// Internal structures of the CNG 1.0 decoder: tables, the parsed container, the per-segment workspace.
#pragma once
#include "cng_codec.hpp"
#include "cng_format.hpp"

namespace stx_cng::detail {

// One context's static rANS table: 12-bit frequencies of the listed symbols and the escape entry (index `escape`).
struct Table {
    std::vector<std::uint16_t> frequency;   // per symbol, sum 4096 when used
    std::vector<std::uint16_t> start;       // cumulative frequency before each symbol
    std::vector<std::uint16_t> slot;        // 4096 entries: the symbol owning each slot
    std::uint32_t escape = 0;               // symbol index of the escape entry
    bool used = false;                      // false when the context codes nothing in this image
};

// The parsed container. Spans borrow the file.
struct Container {
    Dimensions dimensions;
    std::uint32_t tile_side = 0;       // segment width
    std::uint32_t segment_rows = 0;    // segment height
    std::uint32_t tiles_across = 0;
    std::span<const std::uint8_t> tables;               // the intact copy of the tables frame
    std::span<const std::uint8_t> mosaic;               // empty when both copies are damaged
    std::vector<std::span<const std::uint8_t>> payloads;
    std::vector<std::uint8_t> payload_intact;           // 1 when the payload's length and checksum match
};

// Reads the header, directory and copies; checks extents and checksums. Does not decode anything.
[[nodiscard]] Status read_container(std::span<const std::uint8_t> file, Container& container);

// Decompresses and parses the tables frame into 38 tables.
[[nodiscard]] Status read_tables(std::span<const std::uint8_t> frame, std::vector<Table>& tables);

// Reusable storage for decoding one segment at a time (packed residuals, energies, row-above activity sums).
// Every element is written before it is read (matches and the activity read only earlier pixels), so the storage is
// resized, never cleared, between segments.
struct Workspace {
    std::vector<std::uint32_t> residuals;  // per pixel of the segment: the plane residuals in [-128, 127], one byte each
    std::vector<std::uint64_t> energy;     // a ring of three padded rows of packed green, red and blue energies
    std::vector<std::uint64_t> above;      // per column: the row-above part of every pixel's activity sum
};

// Decodes one segment payload of `tile` dimensions into `destination`, the segment's top-left sample in an
// interleaved image whose rows are `row_bytes` apart.
[[nodiscard]] Status decode_tile(const std::vector<Table>& tables, std::span<const std::uint8_t> payload, Dimensions tile,
                                 Workspace& workspace, std::uint8_t* destination, std::size_t row_bytes);

}  // namespace stx_cng::detail
