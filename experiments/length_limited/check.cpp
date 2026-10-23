#include "package_merge.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
#define cng_huffman_kernel cng_exact_huffman
#include "huffman_exact.cpp"
#undef cng_huffman_kernel
#define cng_huffman_kernel cng_prior_huffman
#include "../entropy_reduction/huffman_reduced.cpp"
#undef cng_huffman_kernel
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
struct Before {
    bool operator()(const cng_package_merge::Symbol& first, const cng_package_merge::Symbol& second) const {
        if (first.weight != second.weight) { return first.weight < second.weight; }
        return first.symbol < second.symbol;
    }
};
// Independent bounded-knapsack oracle on Kraft units, only for L<=4. It
// enumerates every admitted code length per symbol, not package decisions.
std::uint32_t optimum(std::span<const cng_package_merge::Symbol> symbols, unsigned limit) {
    constexpr std::uint32_t infinity = 100000000;
    std::array<std::uint32_t, 17> previous{};
    std::array<std::uint32_t, 17> next{};
    previous.fill(infinity);
    previous[0] = 0;
    std::size_t units = 1U << limit;
    for (const cng_package_merge::Symbol& symbol : symbols) {
        next.fill(infinity);
        for (std::size_t used = 0; used <= units; ++used) {
            if (previous[used] == infinity) { continue; }
            for (unsigned length = 1; length <= limit; ++length) {
                std::size_t width = 1U << (limit - length);
                if (width > units - used) { continue; }
                std::uint32_t cost = previous[used] + length * symbol.weight;
                next[used + width] = std::min(next[used + width], cost);
            }
        }
        previous = next;
    }
    return previous[units];
}
std::size_t small_checks() {
    std::size_t cases = 0;
    for (std::size_t count = 2; count <= 6; ++count) {
        std::size_t patterns = 1U << (2 * count);
        for (std::size_t pattern = 0; pattern < patterns; ++pattern) {
            std::array<cng_package_merge::Symbol, 6> symbols{};
            for (std::size_t index = 0; index < count; ++index) {
                symbols[index] = cng_package_merge::Symbol{static_cast<std::uint32_t>(1 + ((pattern >> (2 * index)) & 3)), static_cast<std::uint16_t>(index)};
            }
            std::sort(symbols.begin(), symbols.begin() + count, Before{});
            std::span<const cng_package_merge::Symbol> active = std::span<const cng_package_merge::Symbol>(symbols).first(count);
            for (unsigned limit = 1; limit <= 4; ++limit) {
                if (count > (1U << limit)) { continue; }
                std::array<std::uint8_t, 256> lengths{};
                cng_package_merge::lengths(active, limit, lengths);
                std::uint32_t cost = 0;
                std::size_t kraft = 0;
                for (const cng_package_merge::Symbol& symbol : active) {
                    unsigned length = lengths[symbol.symbol];
                    require(length >= 1 && length <= limit, "bounded length");
                    cost += length * symbol.weight;
                    kraft += 1U << (limit - length);
                }
                require(kraft == (1U << limit) && cost == optimum(active, limit), "independent optimum");
                ++cases;
            }
        }
    }
    return cases;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "corpus packets and result CSV required");
        std::size_t cases = small_checks();
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        require(static_cast<bool>(input) && static_cast<bool>(rows), "packet check files");
        rows << "packet,baseline_bytes,candidate_bytes\n";
        std::size_t packets = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            require(static_cast<bool>(input), "packet header");
            std::size_t size = cng_prior_huffman::get32(header, 0);
            require(size >= 5 && size <= cng_entropy_trial::maximum_input + 512, "packet size");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            require(static_cast<bool>(input), "packet read");
            std::size_t count = cng_prior_huffman::get32(packet, 1);
            require(count <= cng_entropy_trial::maximum_input, "sample size");
            std::vector<std::uint8_t> raw(count);
            std::vector<std::uint8_t> decoded(count);
            std::vector<std::uint8_t> output(2 * count + 512);
            cng_prior_huffman::decode(packet, raw);
            std::size_t old_size = cng_prior_huffman::encode(raw, output);
            require(old_size == size && std::equal(packet.begin(), packet.end(), output.begin()), "baseline packet identity");
            std::size_t new_size = cng_exact_huffman::encode(raw, output);
            // Existing decoder reads the changed length table without changes.
            cng_prior_huffman::decode(std::span<const std::uint8_t>(output).first(new_size), decoded);
            require(decoded == raw, "exact code inverse");
            rows << packets << ',' << size << ',' << new_size << '\n';
            ++packets;
        }
        rows.close();
        require(static_cast<bool>(rows), "packet report write");
        std::cout << "PASS: " << cases << " independent optimality checks; " << packets << " packet identities and existing-decoder inverses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
