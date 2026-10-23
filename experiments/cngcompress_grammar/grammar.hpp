#pragma once
#include "../whole_codec/match_bounded.hpp"
namespace cng_grammar {
struct SplitWorkspace {
    std::array<cng_match_trial::Bucket, 16384> index;
    std::array<std::uint8_t, cng_match_trial::block_size + 16> parsed;
    std::array<std::uint8_t, 2 * cng_match_trial::block_size + 1024> coded;
    std::array<std::uint8_t, cng_match_trial::block_size + 16> literals;
};
}
// Experimental complete frame encoders. Same input/output bounds, disjointness,
// ownership and partial-output failure contract as match_bounded.hpp.
// Split variants need count+7 parsed bytes versus count+3 in the original.
namespace cng_prefix {
using EncodeWorkspace = cng_match_trial::EncodeWorkspace;
using DecodeWorkspace = cng_match_trial::DecodeWorkspace;
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, DecodeWorkspace& workspace);
}
namespace cng_split {
using EncodeWorkspace = cng_grammar::SplitWorkspace;
using DecodeWorkspace = cng_match_trial::DecodeWorkspace;
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, DecodeWorkspace& workspace);
}
namespace cng_combined {
using EncodeWorkspace = cng_grammar::SplitWorkspace;
using DecodeWorkspace = cng_match_trial::DecodeWorkspace;
std::size_t encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, EncodeWorkspace& workspace);
void decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output, DecodeWorkspace& workspace);
}
