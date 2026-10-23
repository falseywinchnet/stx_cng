#include "huffman_kernel.hpp"
#define cng_huffman_kernel cng_huffman_reference
#include "../entropy/huffman_tail_split.cpp"
#undef cng_huffman_kernel
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
struct Samples {
    std::array<double, 7> reference{};
    std::array<double, 7> candidate{};
};
template<bool candidate>
double measure(std::span<const std::uint8_t> input, std::span<std::uint8_t> output,
               std::size_t expected_size, std::uint64_t& observed) {
    constexpr std::size_t repetitions = 64;
    std::uint64_t sum = 0;
    Clock::time_point start = Clock::now();
    for (std::size_t repeat = 0; repeat < repetitions; ++repeat) {
        std::size_t size = 0;
        if constexpr (candidate) { size = cng_huffman_kernel::encode(input, output); }
        else { size = cng_huffman_reference::encode(input, output); }
        sum += size + output[size - 1];
    }
    Clock::time_point end = Clock::now();
    observed += sum;
    if (sum < repetitions * expected_size) { throw std::runtime_error("kernel observed output"); }
    double seconds = std::chrono::duration<double>(end - start).count() / repetitions;
    return seconds;
}
void numbers(const std::array<double, 7>& values) {
    std::cout << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) { std::cout << ','; }
        std::cout << values[index];
    }
    std::cout << ']';
}
}
int main() {
    try {
        std::uint64_t observed = 0;
        std::cout << std::setprecision(12) << "{\"cases\":[";
        bool first = true;
        for (std::size_t size : {1024U, 131072U}) {
            for (unsigned pattern = 0; pattern < 4; ++pattern) {
                std::vector<std::uint8_t> input(size);
                std::uint32_t state = 20261023;
                for (std::size_t position = 0; position < size; ++position) {
                    state ^= state << 13;
                    state ^= state >> 17;
                    state ^= state << 5;
                    switch (pattern) {
                        case 0: input[position] = static_cast<std::uint8_t>(state); break;
                        case 1: input[position] = 0; break;
                        case 2: input[position] = static_cast<std::uint8_t>(state % 7); break;
                        case 3: input[position] = position % 64 == 0 ? static_cast<std::uint8_t>(state) : 0; break;
                        default: throw std::runtime_error("kernel pattern");
                    }
                }
                std::vector<std::uint8_t> reference(2 * size + 512);
                std::vector<std::uint8_t> candidate(reference.size());
                std::size_t reference_size = cng_huffman_reference::encode(input, reference);
                std::size_t candidate_size = cng_huffman_kernel::encode(input, candidate);
                if (reference_size != candidate_size || !std::equal(reference.begin(), reference.begin() + reference_size, candidate.begin())) {
                    throw std::runtime_error("kernel identical packet");
                }
                Samples samples;
                for (std::size_t trial = 0; trial < samples.reference.size(); ++trial) {
                    if ((trial & 1U) == 0) {
                        samples.reference[trial] = measure<false>(input, reference, reference_size, observed);
                        samples.candidate[trial] = measure<true>(input, candidate, candidate_size, observed);
                    } else {
                        samples.candidate[trial] = measure<true>(input, candidate, candidate_size, observed);
                        samples.reference[trial] = measure<false>(input, reference, reference_size, observed);
                    }
                }
                if (!first) { std::cout << ','; }
                first = false;
                std::cout << "{\"bytes\":" << size << ",\"pattern\":" << pattern << ",\"coded_bytes\":" << candidate_size
                          << ",\"reference_seconds\":";
                numbers(samples.reference);
                std::cout << ",\"candidate_seconds\":";
                numbers(samples.candidate);
                std::cout << '}';
            }
        }
        std::cout << "],\"observed\":" << observed << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
