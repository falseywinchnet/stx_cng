#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace cng_entropy_trial {
// Retained CNG reverse-tail byte-run transform. Disjoint caller buffers;
// separated capacity >= 2*packed.size()+4, packed extent <= 1 MiB.
// No allocation/retained borrows; failure may leave partial output.
[[nodiscard]] std::size_t separate_runs(std::span<const std::uint8_t> packed,
                                      std::span<std::uint8_t> separated);
void restore_runs(std::span<const std::uint8_t> separated,
                  std::span<std::uint8_t> packed);
}
