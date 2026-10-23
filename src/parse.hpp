#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace stx_cng_detail {
inline constexpr std::size_t band_capacity = 1048576;
inline constexpr std::size_t index_capacity = 65536;

struct Sequence {
    std::uint32_t literals = 0;
    std::uint32_t length = 0;
    std::uint32_t distance = 0;
};

struct Result {
    std::size_t sequences = 0;
    std::size_t literals = 0;
    std::size_t matched = 0;
};

// Borrow disjoint storage for this synchronous call. Source is RGB8 or RGBA8,
// at most band_capacity bytes. Index has index_capacity entries; output has at
// least pixel_count / 4 + 1 entries. Only returned live sequences are written.
// Validation failures throw before mutation. No allocation or retained borrows.
[[nodiscard]] Result parse(std::span<const std::uint8_t> source,
                          std::size_t channels,
                          std::span<std::int32_t> index,
                          std::span<Sequence> output);
}
