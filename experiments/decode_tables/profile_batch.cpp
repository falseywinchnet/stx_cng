#define cng_huffman_kernel cng_instrumented
#include "huffman_profile.cpp"
#undef cng_huffman_kernel
#define cng_huffman_kernel cng_baseline
#include "../length_limited/huffman_exact.cpp"
#undef cng_huffman_kernel
#define cng_huffman_kernel cng_width
#include "huffman_batch.cpp"
#undef cng_huffman_kernel
#include <fstream>
#include <iostream>
#include <vector>
namespace {
using Clock = std::chrono::steady_clock;
double median(std::array<double, 7> values) {
    std::sort(values.begin(), values.end());
    return values[3];
}
}
int main(int argc, char** argv) {
    try {
        cng_baseline::require(argc == 3, "packets and profile output required");
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream rows(argv[2]);
        cng_baseline::require(static_cast<bool>(input) && static_cast<bool>(rows), "profile files open");
        rows.precision(12);
        rows << "packet,bytes,symbols,max_bits,model,table,body,baseline,batch\n";
        std::size_t ordinal = 0;
        while (input.peek() != std::char_traits<char>::eof()) {
            std::array<std::uint8_t, 4> header{};
            input.read(reinterpret_cast<char*>(header.data()), 4);
            cng_baseline::require(static_cast<bool>(input), "profile header");
            std::size_t size = cng_baseline::get32(header, 0);
            cng_baseline::require(size >= 5 && size <= cng_entropy_trial::maximum_input + 512, "profile packet extent");
            std::vector<std::uint8_t> packet(size);
            input.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(size));
            cng_baseline::require(static_cast<bool>(input), "profile packet read");
            std::size_t count = cng_baseline::get32(packet, 1);
            cng_baseline::require(count <= cng_entropy_trial::maximum_input, "profile output extent");
            std::vector<std::uint8_t> expected(count);
            std::vector<std::uint8_t> decoded(count);
            cng_baseline::decode(packet, expected);
            unsigned max_bits = 0;
            if (packet[0] == 36) {
                std::array<std::uint8_t, 256> lengths{};
                cng_baseline::read_lengths(std::span<const std::uint8_t>(packet).subspan(5), lengths);
                for (std::uint8_t length : lengths) { max_bits = std::max(max_bits, static_cast<unsigned>(length)); }
            }
            std::array<double, 7> model{};
            std::array<double, 7> table{};
            std::array<double, 7> body{};
            std::array<double, 7> baseline{};
            std::array<double, 7> width{};
            for (std::size_t trial = 0; trial < 7; ++trial) {
                cng_instrumented::decode(packet, decoded);
                cng_baseline::require(decoded == expected, "profile exactness");
                model[trial] = cng_instrumented::profile.model;
                table[trial] = cng_instrumented::profile.table;
                body[trial] = cng_instrumented::profile.body;
                for (unsigned order = 0; order < 2; ++order) {
                    bool proposed = ((trial + order) & 1U) != 0;
                    Clock::time_point start = Clock::now();
                    if (proposed) { cng_width::decode(packet, decoded); }
                    else { cng_baseline::decode(packet, decoded); }
                    double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                    if (proposed) { width[trial] = elapsed; }
                    else { baseline[trial] = elapsed; }
                    cng_baseline::require(decoded == expected, "paired packet inverse");
                }
            }
            rows << ordinal << ',' << size << ',' << count << ',' << max_bits << ',' << median(model) << ','
                 << median(table) << ',' << median(body) << ',' << median(baseline) << ',' << median(width) << '\n';
            ++ordinal;
        }
        rows.close();
        cng_baseline::require(static_cast<bool>(rows), "profile output complete");
        std::cout << "PASS: " << ordinal << " staged and paired decoder checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
