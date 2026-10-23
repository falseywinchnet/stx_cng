#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace cngcompress_bulk {
// Prepared-run contract: all four buffers are disjoint; bitmap holds ceil(n/8)
// bytes, its n live bits contain right.size() ones, and n=left.size()+right.size().
// Output holds exactly n bytes. No allocation, retained borrow or speculative
// access beyond a span. The caller validates the complete run before entry.
void merge_scalar(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                  std::span<const std::uint8_t> right, std::span<std::uint8_t> output);
void merge_indexed(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                   std::span<const std::uint8_t> right, std::span<std::uint8_t> output);
void merge_words(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                 std::span<const std::uint8_t> right, std::span<std::uint8_t> output);
void merge_spread(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                  std::span<const std::uint8_t> right, std::span<std::uint8_t> output);
void merge_lanes(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                 std::span<const std::uint8_t> right, std::span<std::uint8_t> output);
struct Traffic {
    std::uint64_t bitmap = 0;
    std::uint64_t selected_input = 0;
    std::uint64_t word_input = 0;
    std::uint64_t output = 0;
    std::uint64_t selector_table = 0;
    std::uint64_t spread_table = 0;
    std::uint64_t mixed_groups = 0;
    std::uint64_t pure_groups = 0;
};
// Source-level byte access counts for these kernels, excluding cache-line
// effects, register/stack allocation and scalar-tail repeated bitmap loads.
Traffic traffic(std::span<const std::uint8_t> bitmap, std::size_t left, std::size_t right);
}
