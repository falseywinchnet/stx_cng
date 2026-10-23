#include "rans.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void write_size(std::ostream& output, std::size_t size) {
    for (unsigned byte = 0; byte < 4; ++byte) { output.put(static_cast<char>(size >> (8 * byte))); }
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) { throw std::runtime_error("depth vector output required"); }
        std::ofstream vectors(argv[1], std::ios::binary);
        if (!vectors) { throw std::runtime_error("depth vector open"); }
        // Fibonacci weights force a deep unconstrained tree. These cases exercise
        // the bounded-length rebuild, unlike uniform or two-symbol populations.
        constexpr std::array<unsigned, 3> sizes{14, 22, 29};
        for (unsigned symbols : sizes) {
            std::vector<std::uint8_t> input;
            std::size_t previous = 0;
            std::size_t current = 1;
            for (unsigned symbol = 0; symbol < symbols; ++symbol) {
                input.insert(input.end(), current, static_cast<std::uint8_t>(symbol));
                std::size_t next = previous + current;
                previous = current;
                current = next;
            }
            std::vector<std::uint8_t> encoded(2 * input.size() + 1024);
            std::vector<std::uint8_t> decoded(input.size());
            std::size_t size = cng_entropy_trial::encode(input, encoded);
            cng_entropy_trial::decode(std::span<const std::uint8_t>(encoded).first(size), decoded);
            if (decoded != input || encoded[0] != 7) { throw std::runtime_error("depth inverse or coding"); }
            write_size(vectors, input.size());
            write_size(vectors, size);
            vectors.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
            vectors.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(size));
        }
        vectors.close();
        if (!vectors) { throw std::runtime_error("depth vector write"); }
        std::cout << "PASS: 3 deep-tree Huffman cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
