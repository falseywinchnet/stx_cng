#include "constant.hpp"
#include <array>
#include <bit>
#include <cstring>
namespace cngcompress_layout {
namespace {
struct Expansion {
    std::array<std::uint64_t, 3> moves{};
    std::uint64_t slots = 0;
    unsigned count = 0;
};
consteval std::array<Expansion, 256> expansions() {
    std::array<Expansion, 256> result{};
    for (unsigned mask = 0; mask < 256; ++mask) {
        Expansion& row = result[mask];
        for (unsigned lane = 0; lane < 8; ++lane) {
            if ((mask & (1U << lane)) == 0) { continue; }
            row.slots |= UINT64_C(255) << (8 * lane);
            unsigned position = row.count;
            unsigned distance = lane - position;
            ++row.count;
            for (unsigned stage = 0; stage < 3; ++stage) {
                unsigned step = 4U >> stage;
                if ((distance & step) == 0) { continue; }
                row.moves[stage] |= UINT64_C(255) << (8 * position);
                position += step;
            }
        }
    }
    return result;
}
constexpr std::array<Expansion, 256> expansion = expansions();
std::uint64_t load(std::span<const std::uint8_t> input, std::size_t offset) {
    std::uint64_t word = 0;
    if (input.size() - offset >= 8 && std::endian::native == std::endian::little) {
        std::memcpy(&word, input.data() + offset, 8);
        return word;
    }
    std::size_t remaining = input.size() - offset;
    if (remaining > 8) { remaining = 8; }
    for (std::size_t byte = 0; byte < remaining; ++byte) { word |= static_cast<std::uint64_t>(input[offset + byte]) << (8 * byte); }
    return word;
}
}
void merge_constant_words(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> values,
                          std::uint8_t constant, unsigned constant_side, std::span<std::uint8_t> output) {
    std::size_t source = 0;
    std::size_t position = 0;
    std::uint64_t repeated = UINT64_C(0x0101010101010101) * constant;
    unsigned invert = constant_side == 1 ? 255 : 0;
    for (; output.size() - position >= 8; position += 8) {
        unsigned mask = bitmap[position / 8] ^ invert;
        std::uint64_t word = repeated;
        if (mask == 255) { word = load(values, source); source += 8; }
        else if (mask != 0) {
            const Expansion& row = expansion[mask];
            word = load(values, source);
            word &= (UINT64_C(1) << (8 * row.count)) - 1;
            std::uint64_t moved = word & row.moves[0];
            word = (word ^ moved) | (moved << 32);
            moved = word & row.moves[1];
            word = (word ^ moved) | (moved << 16);
            moved = word & row.moves[2];
            word = (word ^ moved) | (moved << 8);
            word |= repeated & ~row.slots;
            source += row.count;
        }
        if constexpr (std::endian::native == std::endian::little) { std::memcpy(output.data() + position, &word, 8); }
        else {
            for (unsigned byte = 0; byte < 8; ++byte) { output[position + byte] = static_cast<std::uint8_t>(word >> (8 * byte)); }
        }
    }
    for (; position < output.size(); ++position) {
        unsigned side = (bitmap[position / 8] >> (position % 8)) & 1U;
        if (side == constant_side) { output[position] = constant; }
        else { output[position] = values[source]; ++source; }
    }
}
}
