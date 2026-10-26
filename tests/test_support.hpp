// Shared helpers of the stx_cng tests: deterministic sample patterns, the mosaic definition, failure reporting.
#pragma once
#include "stx_cng/codec.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace test_support {

inline void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

// CRC-32 (IEEE, as zlib's crc32), for crafting files in tests.
inline std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint8_t b : bytes) {
        c ^= b;
        for (int k = 0; k < 8; ++k) { c = (c & 1u) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
    }
    return ~c;
}

// Pattern 0: gradients; 1: sharp steps; 2: a 7-pixel periodic tile; 3: noise; 4: gradients with alpha steps and
// colour beneath alpha 0; 5: a few flat colours with noise patches.
inline std::vector<std::uint8_t> samples(stx_cng::Dimensions d, unsigned pattern, std::uint32_t seed) {
    std::vector<std::uint8_t> out(d.height * d.width * d.channels);
    std::uint32_t state = seed * 2654435761u + 12345u;
    for (std::size_t y = 0; y < d.height; ++y) {
        for (std::size_t x = 0; x < d.width; ++x) {
            state = state * 1664525u + 1013904223u;
            for (std::size_t c = 0; c < d.channels; ++c) {
                std::uint32_t v = static_cast<std::uint32_t>(3 * x + 2 * y + 61 * c);
                if (pattern == 1) { v = ((x / 9 + y / 5) % 3) * 100 + c * 20; }
                if (pattern == 2) { v = static_cast<std::uint32_t>(((x % 7) * 37 + (y % 7) * 11 + c * 50)); }
                if (pattern == 3) { v = state >> (8 * c); }
                if (pattern == 4 && c == 3) { v = (x / 13) % 3 == 0 ? 0u : ((x / 13) % 3 == 1 ? 255u : 128u); }
                if (pattern == 5) { v = ((x / 40 + y / 30) % 2) * 180 + c * 10; if ((x / 6 + y / 6) % 11 == 0) { v = state >> (8 * c); } }
                out[(y * d.width + x) * d.channels + c] = static_cast<std::uint8_t>(v & 255u);
            }
        }
    }
    return out;
}

// The mosaic: the rounded mean of every 16 x 16 cell over the pixels inside the image.
inline std::vector<std::uint8_t> block_means(const std::vector<std::uint8_t>& samples, stx_cng::Dimensions d) {
    stx_cng::Dimensions m = stx_cng::mosaic_dimensions(d);
    std::vector<std::uint8_t> out(m.height * m.width * m.channels);
    for (std::size_t cy = 0; cy < m.height; ++cy) {
        for (std::size_t cx = 0; cx < m.width; ++cx) {
            for (std::size_t c = 0; c < d.channels; ++c) {
                std::uint64_t sum = 0, count = 0;
                for (std::size_t y = cy * 16; y < std::min(d.height, cy * 16 + 16); ++y) {
                    for (std::size_t x = cx * 16; x < std::min(d.width, cx * 16 + 16); ++x) { sum += samples[(y * d.width + x) * d.channels + c]; ++count; }
                }
                out[(cy * m.width + cx) * d.channels + c] = static_cast<std::uint8_t>((sum + count / 2) / count);
            }
        }
    }
    return out;
}

}  // namespace test_support
