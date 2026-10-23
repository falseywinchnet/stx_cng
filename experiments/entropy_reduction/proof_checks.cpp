#include "huffman_kernel.hpp"
// Diagnostic-only inclusion makes the old length construction inspectable.
// The measured candidate links separately and has no diagnostic instrumentation.
#define cng_huffman_kernel cng_huffman_reference
#include "../entropy/huffman_tail_split.cpp"
#undef cng_huffman_kernel
#include <iostream>
#include <vector>
namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::uint32_t next(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
void exact(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> reference(2 * input.size() + 512);
    std::vector<std::uint8_t> candidate(reference.size());
    std::vector<std::uint8_t> decoded(input.size());
    std::size_t first = cng_huffman_reference::encode(input, reference);
    std::size_t second = cng_huffman_kernel::encode(input, candidate);
    require(first == second && std::equal(reference.begin(), reference.begin() + first, candidate.begin()), "identical Huffman packet");
    cng_huffman_kernel::decode(std::span<const std::uint8_t>(candidate).first(second), decoded);
    require(std::equal(input.begin(), input.end(), decoded.begin()), "exact Huffman samples");
}
}
int main() {
    try {
        std::uint32_t state = 20261023;
        std::size_t cases = 0;
        std::size_t proved = 0;
        for (std::uint32_t minimum : {1U, 2U, 17U, 127U}) {
            for (std::uint32_t pattern = 0; pattern < 32; ++pattern) {
                std::array<std::uint32_t, 256> populations{};
                std::vector<std::uint8_t> input;
                for (std::size_t symbol = 0; symbol < populations.size(); ++symbol) {
                    std::uint32_t population = minimum + next(state) % (minimum + 1);
                    if (pattern == 0) { population = minimum; }
                    if (pattern == 1) { population = symbol < 128 ? minimum : 2 * minimum; }
                    if (pattern == 2) { population = symbol == 255 ? 2 * minimum : minimum; }
                    if (pattern == 3) { population = symbol == 0 ? minimum : 2 * minimum; }
                    populations[symbol] = population;
                    input.insert(input.end(), population, static_cast<std::uint8_t>(symbol));
                }
                std::array<std::uint8_t, 256> lengths{};
                cng_huffman_reference::make_lengths(populations, lengths);
                for (std::uint8_t length : lengths) { require(length == 8, "uniform-depth proof boundary"); }
                ++proved;
                exact(input);
                ++cases;
                // Moving beyond the sufficient condition need not imply a
                // compressible tree. It must still preserve the old packet.
                input.insert(input.end(), 2 * minimum + 1, 0);
                exact(input);
                ++cases;
            }
        }
        for (std::size_t size : {0U, 1U, 2U, 3U, 4U, 7U, 15U, 255U, 256U, 1023U, 1024U, 1025U, 131071U, 131072U, 131073U, 2097160U}) {
            for (std::uint32_t alphabet : {1U, 2U, 7U, 16U, 127U, 255U, 256U}) {
                std::vector<std::uint8_t> input(size);
                for (std::uint8_t& value : input) { value = static_cast<std::uint8_t>(next(state) % alphabet); }
                exact(input);
                ++cases;
            }
        }
        std::cout << "PASS " << cases << " byte-identical packets and inverses; " << proved << " full-alphabet depth checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
