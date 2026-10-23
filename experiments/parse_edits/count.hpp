#pragma once
// Included after the retained Huffman implementation in this diagnostic.
std::size_t huffman_size(std::span<const std::uint8_t> input) {
    if (input.empty()) { return 5; }
    std::array<std::array<std::uint32_t, 256>, 4> lane_counts{};
    std::size_t lanes = cng_huffman_kernel::stream_count(input.size());
    std::size_t quarter = input.size() / lanes;
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        std::size_t first = lane * quarter;
        std::size_t last = lane + 1 == lanes ? input.size() : first + quarter;
        for (std::size_t index = first; index < last; ++index) { ++lane_counts[lane][input[index]]; }
    }
    std::array<std::uint32_t, 256> counts{};
    std::size_t active = 0;
    for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
        for (std::size_t lane = 0; lane < lanes; ++lane) { counts[symbol] += lane_counts[lane][symbol]; }
        if (counts[symbol] != 0) { ++active; }
    }
    if (active == 1) { return 6; }
    std::array<std::uint8_t, 256> lengths{};
    cng_huffman_kernel::make_lengths(counts, lengths);
    std::array<std::uint8_t, 128> table{};
    std::size_t table_size = cng_huffman_kernel::write_lengths(lengths, table);
    std::size_t size = 5 + table_size + 4 * (lanes - 1);
    for (std::size_t lane = 0; lane < lanes; ++lane) {
        std::size_t bits = 0;
        for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
            bits += static_cast<std::size_t>(lane_counts[lane][symbol]) * lengths[symbol];
        }
        size += (bits + 7) / 8;
    }
    std::size_t result = std::min(size, input.size() + 5);
    return result;
}
