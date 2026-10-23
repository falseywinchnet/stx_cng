#include "merge.hpp"
#include <algorithm>
#include <array>
#include <cstring>
namespace cngcompress_bulk {
namespace {
struct Selectors {
    std::array<std::uint8_t, 8> index{};
    std::uint8_t right = 0;
};
struct Movements {
    std::array<std::uint64_t, 3> left{};
    std::array<std::uint64_t, 3> right{};
};
consteval std::array<Selectors, 256> selectors() {
    std::array<Selectors, 256> result{};
    for (unsigned mask = 0; mask < 256; ++mask) {
        unsigned left = 0;
        unsigned right = 0;
        for (unsigned lane = 0; lane < 8; ++lane) {
            if ((mask & (1U << lane)) != 0) {
                result[mask].index[lane] = static_cast<std::uint8_t>(8 + right);
                ++right;
            } else {
                result[mask].index[lane] = static_cast<std::uint8_t>(left);
                ++left;
            }
        }
        result[mask].right = static_cast<std::uint8_t>(right);
    }
    return result;
}
consteval std::array<Movements, 256> movements() {
    std::array<Movements, 256> result{};
    for (unsigned mask = 0; mask < 256; ++mask) {
        std::array<unsigned, 2> rank{};
        for (unsigned lane = 0; lane < 8; ++lane) {
            unsigned side = (mask >> lane) & 1U;
            unsigned position = rank[side];
            unsigned distance = lane - position;
            ++rank[side];
            // Destinations and displacements are monotone within either input.
            // Descending 4/2/1-byte moves cannot collide with another live byte.
            for (unsigned stage = 0; stage < 3; ++stage) {
                unsigned step = 4U >> stage;
                if ((distance & step) == 0) { continue; }
                std::uint64_t selected = UINT64_C(255) << (8 * position);
                if (side == 0) { result[mask].left[stage] |= selected; }
                else { result[mask].right[stage] |= selected; }
                position += step;
            }
        }
    }
    return result;
}
constexpr std::array<Selectors, 256> selection = selectors();
constexpr std::array<Movements, 256> movement = movements();
std::uint64_t load_word(std::span<const std::uint8_t> input, std::size_t position) {
    std::uint64_t value = 0;
    std::size_t count = std::min<std::size_t>(8, input.size() - position);
    // Explicit little-endian arithmetic also defines behavior on big-endian
    // hosts. The full eight-byte loop folds to a word load on tested targets.
    for (std::size_t byte = 0; byte < count; ++byte) {
        value |= static_cast<std::uint64_t>(input[position + byte]) << (8 * byte);
    }
    return value;
}
void store_word(std::span<std::uint8_t> output, std::size_t position, std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) { output[position + byte] = static_cast<std::uint8_t>(value >> (8 * byte)); }
}
std::uint64_t spread(std::uint64_t word, unsigned count, const std::array<std::uint64_t, 3>& masks) {
    // Only mixed groups enter here, so 1<=count<=7 and shifts remain <64.
    std::uint64_t live = (UINT64_C(1) << (8 * count)) - 1;
    word &= live;
    std::uint64_t moved = word & masks[0];
    word = (word ^ moved) | (moved << 32);
    moved = word & masks[1];
    word = (word ^ moved) | (moved << 16);
    moved = word & masks[2];
    word = (word ^ moved) | (moved << 8);
    return word;
}
void tail(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
          std::span<const std::uint8_t> right, std::span<std::uint8_t> output,
          std::size_t position, std::size_t left_position, std::size_t right_position) {
    for (; position < output.size(); ++position) {
        unsigned bit = (bitmap[position / 8] >> (position % 8)) & 1U;
        if (bit == 0) { output[position] = left[left_position]; ++left_position; }
        else { output[position] = right[right_position]; ++right_position; }
    }
}
enum class Operation { indexed, words, spread };
template<Operation operation>
void merge(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
           std::span<const std::uint8_t> right, std::span<std::uint8_t> output) {
    std::size_t position = 0;
    std::size_t left_position = 0;
    std::size_t right_position = 0;
    std::size_t interior = output.size() - output.size() % 8;
    for (; position < interior; position += 8) {
        unsigned mask = bitmap[position / 8];
        if (mask == 0) {
            std::memcpy(output.data() + position, left.data() + left_position, 8);
            left_position += 8;
            continue;
        }
        if (mask == 255) {
            std::memcpy(output.data() + position, right.data() + right_position, 8);
            right_position += 8;
            continue;
        }
        const Selectors& selectors = selection[mask];
        unsigned right_count = selectors.right;
        std::uint64_t left_word = load_word(left, left_position);
        std::uint64_t right_word = load_word(right, right_position);
        if constexpr (operation == Operation::indexed) {
            std::array<std::uint8_t, 16> both{};
            store_word(both, 0, left_word);
            store_word(both, 8, right_word);
            for (unsigned lane = 0; lane < 8; ++lane) { output[position + lane] = both[selectors.index[lane]]; }
        } else if constexpr (operation == Operation::words) {
            std::uint64_t result = 0;
            for (unsigned lane = 0; lane < 8; ++lane) {
                unsigned selector = selectors.index[lane];
                std::uint64_t selected = selector < 8 ? left_word : right_word;
                unsigned shift = (selector & 7U) * 8;
                std::uint64_t value = (selected >> shift) & UINT64_C(255);
                result |= value << (8 * lane);
            }
            store_word(output, position, result);
        } else {
            const Movements& masks = movement[mask];
            std::uint64_t left_spread = spread(left_word, 8 - right_count, masks.left);
            std::uint64_t right_spread = spread(right_word, right_count, masks.right);
            std::uint64_t result = left_spread | right_spread;
            store_word(output, position, result);
        }
        left_position += 8 - right_count;
        right_position += right_count;
    }
    tail(bitmap, left, right, output, position, left_position, right_position);
}
}
void merge_scalar(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                  std::span<const std::uint8_t> right, std::span<std::uint8_t> output) {
    tail(bitmap, left, right, output, 0, 0, 0);
}
void merge_indexed(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                   std::span<const std::uint8_t> right, std::span<std::uint8_t> output) {
    merge<Operation::indexed>(bitmap, left, right, output);
}
void merge_words(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                 std::span<const std::uint8_t> right, std::span<std::uint8_t> output) {
    merge<Operation::words>(bitmap, left, right, output);
}
void merge_spread(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> left,
                  std::span<const std::uint8_t> right, std::span<std::uint8_t> output) {
    merge<Operation::spread>(bitmap, left, right, output);
}
Traffic traffic(std::span<const std::uint8_t> bitmap, std::size_t left, std::size_t right) {
    Traffic result{};
    std::size_t count = left + right;
    result.bitmap = (count + 7) / 8;
    result.output = count;
    result.selected_input = count;
    std::size_t position = 0;
    for (; count - position >= 8; position += 8) {
        unsigned mask = bitmap[position / 8];
        unsigned r = selection[mask].right;
        if (mask == 0 || mask == 255) {
            result.word_input += 8;
            ++result.pure_groups;
        } else {
            result.word_input += std::min<std::size_t>(8, left) + std::min<std::size_t>(8, right);
            result.selector_table += 9;
            result.spread_table += 49;
            ++result.mixed_groups;
        }
        left -= 8 - r;
        right -= r;
    }
    result.word_input += count - position;
    return result;
}
}
