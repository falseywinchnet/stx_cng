#pragma once
#include <cstdint>
#include <span>
namespace cngcompress_layout {
// Same disjoint prepared-run contract as merge_lanes, with one constant child.
// Live bitmap bits selecting the variable side equal values.size().
void merge_constant_words(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> values,
                          std::uint8_t constant, unsigned constant_side, std::span<std::uint8_t> output);
}
