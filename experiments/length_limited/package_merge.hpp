#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
namespace cng_package_merge {
struct Symbol {
    std::uint32_t weight;
    std::uint16_t symbol;
};
// Sorted positive weights, distinct byte symbols, 2..256 active entries,
// 1..12 levels, feasible alphabet and total weight <= 2097160. Input/output
// are disjoint. Clears/fills lengths; no allocation or retained storage.
// Larmore-Hirschberg package-merge, with full bounded provenance lists.
inline void lengths(std::span<const Symbol> sorted, unsigned limit,
                    std::array<std::uint8_t, 256>& output) {
    std::size_t count = sorted.size();
    if (count < 2 || count > 256 || limit == 0 || limit > 12 || count > (1U << limit)) {
        throw std::runtime_error("package-merge shape");
    }
    // Each list has fewer than 2*count entries. At level j its total weight
    // is at most (j+1)*input_total, so the admitted frame fits uint32_t.
    std::array<std::array<std::uint32_t, 512>, 2> weights{};
    std::array<std::array<std::uint16_t, 512>, 12> origins{};
    output.fill(0);
    for (std::size_t index = 0; index < count; ++index) {
        weights[0][index] = sorted[index].weight;
        origins[0][index] = sorted[index].symbol;
    }
    std::size_t live = count;
    for (unsigned level = 1; level < limit; ++level) {
        const std::array<std::uint32_t, 512>& previous = weights[(level - 1) & 1U];
        std::array<std::uint32_t, 512>& current = weights[level & 1U];
        std::size_t pairs = live / 2;
        std::size_t leaf = 0;
        std::size_t pair = 0;
        std::size_t written = 0;
        while (leaf < count || pair < pairs) {
            bool take_leaf = pair == pairs;
            std::uint32_t weight = 0;
            if (pair < pairs) {
                weight = previous[2 * pair] + previous[2 * pair + 1];
                if (leaf < count && sorted[leaf].weight <= weight) { take_leaf = true; }
            }
            if (take_leaf) {
                current[written] = sorted[leaf].weight;
                origins[level][written] = sorted[leaf].symbol;
                ++leaf;
            } else {
                current[written] = weight;
                // Values >=256 denote a package, whose two children are at
                // 2*pair and 2*pair+1 in the preceding list.
                origins[level][written] = static_cast<std::uint16_t>(256 + 2 * pair);
                ++pair;
            }
            ++written;
        }
        live = written;
    }
    // The lowest 2*n-2 items of width 1/2 have total width n-1. A prefix of
    // each merged list selects a prefix of its packages; unpacking therefore
    // selects a prefix of the preceding list. No tree walk or flags needed.
    std::size_t selected = 2 * count - 2;
    if (selected > live) { throw std::runtime_error("package-merge feasibility"); }
    for (unsigned remaining = limit; remaining != 0; --remaining) {
        std::size_t packages = 0;
        for (std::size_t index = 0; index < selected; ++index) {
            std::uint16_t origin = origins[remaining - 1][index];
            if (origin < 256) { ++output[origin]; }
            else { ++packages; }
        }
        selected = 2 * packages;
    }
}
}
