#include "layout.hpp"
#include "../cngcompress_bulk/merge.hpp"
#include <bit>
#include <cstring>
#define cng_huffman_kernel cng_layout_model
#include "../length_limited/huffman_exact.cpp"
#undef cng_huffman_kernel
namespace cngcompress_layout {
namespace {
using cng_layout_model::require;
struct Node {
    std::array<std::uint16_t, 2> child{511, 511};
    std::uint16_t symbol = 256;
    std::uint16_t map_offset = 0;
    unsigned depth = 0;
    unsigned flat = 0;
};
struct Tree {
    std::array<Node, 511> nodes{};
    // Maximal flat subtrees have disjoint leaves: maps total <=256 bytes.
    std::array<std::uint8_t, 256> maps{};
    std::array<std::uint16_t, 256> codes{};
    std::size_t count = 1;
};
void make_tree(const std::array<std::uint8_t, 256>& lengths, Tree& tree) {
    cng_layout_model::canonical(lengths, tree.codes);
    for (unsigned symbol = 0; symbol < 256; ++symbol) {
        unsigned length = lengths[symbol];
        if (length == 0) { continue; }
        std::uint16_t index = 0;
        for (unsigned depth = 0; depth < length; ++depth) {
            unsigned bit = (tree.codes[symbol] >> depth) & 1U;
            Node& node = tree.nodes[index];
            if (node.child[bit] == 511) {
                require(tree.count < 511, "layout tree capacity");
                node.child[bit] = static_cast<std::uint16_t>(tree.count);
                tree.nodes[tree.count].depth = depth + 1;
                ++tree.count;
            }
            index = node.child[bit];
        }
        tree.nodes[index].symbol = static_cast<std::uint16_t>(symbol);
    }
    for (std::size_t index = tree.count; index != 0;) {
        --index;
        Node& node = tree.nodes[index];
        if (node.symbol != 256) { continue; }
        require(node.child[0] < tree.count && node.child[1] < tree.count, "layout tree children");
        const Node& left = tree.nodes[node.child[0]];
        const Node& right = tree.nodes[node.child[1]];
        if ((left.symbol != 256 || left.flat != 0) && (right.symbol != 256 || right.flat != 0) && left.flat == right.flat) {
            node.flat = left.flat + 1;
        }
    }
    std::size_t map_size = 0;
    std::array<bool, 511> reachable{};
    reachable[0] = true;
    for (std::size_t index = 0; index < tree.count; ++index) {
        Node& node = tree.nodes[index];
        if (!reachable[index] || node.symbol != 256) { continue; }
        if (node.flat == 0) {
            reachable[node.child[0]] = true;
            reachable[node.child[1]] = true;
            continue;
        }
        std::size_t entries = std::size_t{1} << node.flat;
        require(entries <= 256 - map_size, "flat map capacity");
        node.map_offset = static_cast<std::uint16_t>(map_size);
        for (unsigned code = 0; code < entries; ++code) {
            std::size_t child = index;
            for (unsigned bit = 0; bit < node.flat; ++bit) { child = tree.nodes[child].child[(code >> bit) & 1U]; }
            require(tree.nodes[child].symbol < 256, "flat map leaf");
            tree.maps[map_size + code] = static_cast<std::uint8_t>(tree.nodes[child].symbol);
        }
        map_size += entries;
    }
}
std::size_t emit(const Tree& tree, std::size_t index, std::span<const std::uint8_t> input,
                 std::span<std::uint8_t> output, std::span<std::uint8_t> partition,
                 std::span<std::uint8_t> scratch) {
    const Node& node = tree.nodes[index];
    if (node.symbol != 256 || input.empty()) { return 0; }
    unsigned width = node.flat == 0 ? 1 : node.flat;
    std::size_t bytes = (input.size() * width + 7) / 8;
    require(output.size() >= bytes, "layout emit capacity");
    std::fill_n(output.data(), bytes, 0);
    unsigned mask = (1U << width) - 1;
    std::size_t right_count = 0;
    for (std::size_t position = 0; position < input.size(); ++position) {
        unsigned value = (tree.codes[input[position]] >> node.depth) & mask;
        std::size_t bit = position * width;
        unsigned shifted = value << (bit % 8);
        output[bit / 8] |= static_cast<std::uint8_t>(shifted);
        if (bit % 8 + width > 8) { output[bit / 8 + 1] |= static_cast<std::uint8_t>(shifted >> 8); }
        right_count += value;
    }
    if (node.flat != 0) { return bytes; }
    std::size_t left_count = input.size() - right_count;
    std::size_t l = 0;
    std::size_t r = left_count;
    for (std::uint8_t value : input) {
        unsigned bit = (tree.codes[value] >> node.depth) & 1U;
        if (bit == 0) { partition[l] = value; ++l; }
        else { partition[r] = value; ++r; }
    }
    std::size_t left_bytes = emit(tree, node.child[0], partition.first(left_count), output.subspan(bytes),
                                  scratch.first(left_count), partition.first(left_count));
    bytes += left_bytes;
    std::size_t right_bytes = emit(tree, node.child[1], partition.subspan(left_count, right_count), output.subspan(bytes),
                                   scratch.subspan(left_count, right_count), partition.subspan(left_count, right_count));
    bytes += right_bytes;
    return bytes;
}
template<unsigned width>
void flat_decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
                 const std::uint8_t* map) {
    constexpr unsigned mask = (1U << width) - 1;
    std::size_t position = 0;
    std::size_t offset = 0;
    for (; output.size() - position >= 8; position += 8, offset += width) {
        std::uint64_t word = 0;
        // width is selected before the loop and at most eight bytes.
        for (unsigned byte = 0; byte < width; ++byte) { word |= static_cast<std::uint64_t>(input[offset + byte]) << (8 * byte); }
        for (unsigned lane = 0; lane < 8; ++lane) { output[position + lane] = map[(word >> (width * lane)) & mask]; }
    }
    for (; position < output.size(); ++position) {
        std::size_t bit = position * width;
        unsigned value = input[bit / 8];
        if (bit % 8 + width > 8) { value |= static_cast<unsigned>(input[bit / 8 + 1]) << 8; }
        output[position] = map[(value >> (bit % 8)) & mask];
    }
}
void flat(unsigned width, std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
          const std::uint8_t* map) {
    switch (width) {
        case 1: flat_decode<1>(input, output, map); return;
        case 2: flat_decode<2>(input, output, map); return;
        case 3: flat_decode<3>(input, output, map); return;
        case 4: flat_decode<4>(input, output, map); return;
        case 5: flat_decode<5>(input, output, map); return;
        case 6: flat_decode<6>(input, output, map); return;
        case 7: flat_decode<7>(input, output, map); return;
        case 8: flat_decode<8>(input, output, map); return;
        default: require(false, "flat width"); return;
    }
}
void merge_constant(std::span<const std::uint8_t> bitmap, std::span<const std::uint8_t> values,
                    std::uint8_t constant, unsigned constant_side, std::span<std::uint8_t> output) {
    std::size_t source = 0;
    for (std::size_t position = 0; position < output.size(); ++position) {
        unsigned side = (bitmap[position / 8] >> (position % 8)) & 1U;
        if (side == constant_side) { output[position] = constant; }
        else { output[position] = values[source]; ++source; }
    }
}
std::size_t reconstruct(const Tree& tree, std::size_t index, std::span<const std::uint8_t> input,
                        std::span<std::uint8_t> output, std::span<std::uint8_t> workspace) {
    const Node& node = tree.nodes[index];
    if (output.empty()) { return 0; }
    require(node.symbol == 256, "nonconstant reconstruction");
    unsigned width = node.flat == 0 ? 1 : node.flat;
    std::size_t bytes = (output.size() * width + 7) / 8;
    require(input.size() >= bytes, "node bit extent");
    unsigned live = static_cast<unsigned>(output.size() * width % 8);
    if (live != 0) { require((input[bytes - 1] >> live) == 0, "node padding"); }
    if (node.flat != 0) {
        flat(width, input.first(bytes), output, tree.maps.data() + node.map_offset);
        return bytes;
    }
    std::span<const std::uint8_t> bitmap = input.first(bytes);
    std::size_t right_count = 0;
    for (std::uint8_t byte : bitmap) { right_count += static_cast<unsigned>(std::popcount(byte)); }
    std::size_t left_count = output.size() - right_count;
    const Node& left = tree.nodes[node.child[0]];
    const Node& right = tree.nodes[node.child[1]];
    // Alternate the two disjoint n-byte buffers at each recursion level. A
    // child's temporary output occupies only its own slice, so it cannot
    // overwrite its sibling. Recursion is bounded by the 12-bit tree depth.
    if (left.symbol == 256) {
        bytes += reconstruct(tree, node.child[0], input.subspan(bytes), workspace.first(left_count), output.first(left_count));
    }
    if (right.symbol == 256) {
        bytes += reconstruct(tree, node.child[1], input.subspan(bytes), workspace.subspan(left_count, right_count), output.subspan(left_count, right_count));
    }
    if (left.symbol != 256) {
        merge_constant(bitmap, workspace.subspan(left_count, right_count), static_cast<std::uint8_t>(left.symbol), 0, output);
    } else if (right.symbol != 256) {
        merge_constant(bitmap, workspace.first(left_count), static_cast<std::uint8_t>(right.symbol), 1, output);
    } else {
        cngcompress_bulk::merge_lanes(bitmap, workspace.first(left_count), workspace.subspan(left_count, right_count), output);
    }
    return bytes;
}
}
std::size_t encode(std::span<const std::uint8_t> input, const std::array<std::uint8_t, 256>& lengths,
                   std::span<std::uint8_t> output, std::span<std::uint8_t> workspace) {
    require(input.size() <= cng_entropy_trial::maximum_input, "layout input bound");
    require(output.size() >= 2 * input.size() + 1024 && workspace.size() >= 2 * input.size(), "layout encode storage");
    Tree tree{};
    make_tree(lengths, tree);
    for (std::uint8_t value : input) { require(lengths[value] != 0, "layout input symbol"); }
    output[0] = 177;
    cng_layout_model::put32(output, 1, static_cast<std::uint32_t>(input.size()));
    std::size_t table = cng_layout_model::write_lengths(lengths, output.subspan(5, 128));
    std::size_t bytes = emit(tree, 0, input, output.subspan(5 + table), workspace.first(input.size()), workspace.subspan(input.size(), input.size()));
    std::size_t result = 5 + table + bytes;
    return result;
}
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
            std::span<std::uint8_t> workspace) {
    require(input.size() >= 5 && input[0] == 177, "layout header");
    require(output.size() <= cng_entropy_trial::maximum_input && cng_layout_model::get32(input, 1) == output.size(), "layout size");
    require(workspace.size() >= output.size(), "layout decode workspace");
    std::array<std::uint8_t, 256> lengths{};
    std::size_t table = cng_layout_model::read_lengths(input.subspan(5), lengths);
    Tree tree{};
    make_tree(lengths, tree);
    std::size_t consumed = reconstruct(tree, 0, input.subspan(5 + table), output, workspace.first(output.size()));
    require(consumed == input.size() - 5 - table, "layout trailing bytes");
}
}
