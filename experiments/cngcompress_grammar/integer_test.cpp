#define CNG_NAMESPACE cng_prefix
#define CNG_PREFIX 1
#define CNG_SPLIT 0
#define CNG_MODE 17
#include "grammar.cpp"
#include <iostream>
namespace {
void fixture(std::size_t value) {
    std::array<std::uint8_t, 4> bytes{};
    std::size_t written = 0;
    cng_prefix::integer(std::span<std::uint8_t>(bytes), written, value);
    std::size_t read = 0;
    std::size_t decoded = cng_prefix::integer(std::span<const std::uint8_t>(bytes).first(written), read);
    unsigned expected_width = value < 128 ? 1 : value < 16384 ? 2 : value < 2097152 ? 3 : 4;
    if (decoded != value || read != written || written != expected_width) { throw std::runtime_error("prefix exact integer"); }
}
}
int main() {
    for (std::size_t value = 0; value <= cng_entropy_trial::maximum_input; ++value) { fixture(value); }
    fixture((std::size_t{1} << 28) - 1);
    std::cout << "PASS: 2097162 prefix integer fixtures, exact bounds and LEB128-equivalent widths\n";
    return 0;
}
