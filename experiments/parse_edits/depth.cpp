#include "../entropy_reduction/huffman_reduced.cpp"
#include <fstream>
#include <iostream>
#include <vector>
namespace {
// Ordinary unconstrained Huffman gives a lower bound on any 12-bit code's
// payload cost. This diagnostic never writes its possibly overlong codes.
unsigned unrestricted(const std::array<std::uint32_t, 256>& counts,
                      std::array<std::uint8_t, 256>& lengths) {
    std::array<cng_huffman_kernel::Leaf, 256> leaves{};
    std::array<std::uint32_t, 511> weights{};
    std::array<std::size_t, 511> parents{};
    std::size_t count = 0;
    for (std::size_t symbol = 0; symbol < counts.size(); ++symbol) {
        if (counts[symbol] == 0) { continue; }
        leaves[count] = cng_huffman_kernel::Leaf{counts[symbol], static_cast<std::uint16_t>(symbol)};
        ++count;
    }
    if (count < 2) { return 0; }
    std::sort(leaves.begin(), leaves.begin() + count, cng_huffman_kernel::Before{});
    for (std::size_t index = 0; index < count; ++index) { weights[index] = leaves[index].weight; }
    std::size_t leaf = 0;
    std::size_t branch = count;
    for (std::size_t built = count; built < 2 * count - 1; ++built) {
        std::size_t first = cng_huffman_kernel::take_minimum(weights, count, built, leaf, branch);
        std::size_t second = cng_huffman_kernel::take_minimum(weights, count, built, leaf, branch);
        weights[built] = weights[first] + weights[second];
        parents[first] = built;
        parents[second] = built;
    }
    unsigned maximum = 0;
    for (std::size_t index = 0; index < count; ++index) {
        unsigned depth = 0;
        for (std::size_t node = index; node != 2 * count - 2; node = parents[node]) { ++depth; }
        maximum = std::max(maximum, depth);
        lengths[leaves[index].symbol] = static_cast<std::uint8_t>(depth);
    }
    return maximum;
}
}
int main(int argc, char** argv) {
    try {
        cng_huffman_kernel::require(argc == 3, "packet bundle and depth CSV required");
        std::ifstream stream(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        cng_huffman_kernel::require(static_cast<bool>(stream) && static_cast<bool>(rows), "depth files open");
        rows << "packet,bytes,symbols,max_depth,current_bits,unrestricted_bits\n";
        std::size_t ordinal = 0;
        while (stream.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            stream.read(reinterpret_cast<char*>(header.data()), 4);
            cng_huffman_kernel::require(static_cast<bool>(stream), "depth packet header");
            std::size_t size = cng_huffman_kernel::get32(header, 0);
            cng_huffman_kernel::require(size >= 5 && size <= cng_entropy_trial::maximum_input + 512, "depth packet size");
            std::vector<std::uint8_t> packet(size);
            stream.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            cng_huffman_kernel::require(static_cast<bool>(stream), "depth packet read");
            std::size_t count = cng_huffman_kernel::get32(packet, 1);
            cng_huffman_kernel::require(count <= cng_entropy_trial::maximum_input, "depth sample size");
            std::vector<std::uint8_t> raw(count);
            cng_huffman_kernel::decode(packet, raw);
            std::array<std::uint32_t, 256> populations{};
            for (std::uint8_t symbol : raw) { ++populations[symbol]; }
            std::array<std::uint8_t, 256> normal{};
            std::array<std::uint8_t, 256> limited{};
            unsigned maximum = unrestricted(populations, normal);
            std::size_t normal_bits = 0;
            std::size_t limited_bits = 0;
            if (maximum != 0) {
                cng_huffman_kernel::make_lengths(populations, limited);
                for (std::size_t symbol = 0; symbol < populations.size(); ++symbol) {
                    normal_bits += populations[symbol] * normal[symbol];
                    limited_bits += populations[symbol] * limited[symbol];
                }
                cng_huffman_kernel::require(limited_bits >= normal_bits, "unrestricted lower bound");
            }
            rows << ordinal << ',' << size << ',' << count << ',' << maximum << ',' << limited_bits << ',' << normal_bits << '\n';
            ++ordinal;
        }
        rows.close();
        cng_huffman_kernel::require(static_cast<bool>(rows), "depth report write");
        std::cout << "PASS: " << ordinal << " packet depth measurements\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
