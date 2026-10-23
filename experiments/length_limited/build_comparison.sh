#!/bin/sh
set -eu
comparison_compiler=/opt/homebrew/opt/llvm/bin/clang++
comparison_flags='-O3 -DNDEBUG -std=c++20 -arch arm64 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc -Iexperiments/whole_codec -Iexperiments/entropy'
comparison_definitions='-DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Dcng_candidate=cng_prior -Dcng_match_trial=cng_match_prior -Dcng_huffman_kernel=cng_huffman_prior'
# The prior implementation is compiled under distinct names so both candidates
# execute in one comparison process. Original source files remain unchanged.
"$comparison_compiler" $comparison_flags $comparison_definitions -c experiments/whole_codec/candidate_codec.cpp -o .build/length-limited/prior_codec.o
"$comparison_compiler" $comparison_flags $comparison_definitions -c experiments/whole_codec/match_bounded.cpp -o .build/length-limited/prior_match.o
"$comparison_compiler" $comparison_flags $comparison_definitions -c experiments/entropy_reduction/huffman_reduced.cpp -o .build/length-limited/prior_huffman.o
# The released public declarations have the same value types and function
# signatures as the private candidate. Renaming their namespace makes the
# existing probe compare prior CNE files against the new candidate directly.
"$comparison_compiler" $comparison_flags -Dstx_cng=cng_prior -c experiments/whole_codec/probe.cpp -o .build/length-limited/comparison_probe.o
"$comparison_compiler" $comparison_flags .build/length-limited/comparison_probe.o .build/length-limited/prior_codec.o .build/length-limited/prior_match.o .build/length-limited/prior_huffman.o .build/length-limited/libwhole_candidate.a .build/length-limited/libwhole_shared.a -lz -o .build/length-limited/cng_direct_comparison

"$comparison_compiler" $comparison_flags -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 experiments/whole_codec/bounds_checks.cpp experiments/whole_codec/match_bounded.cpp experiments/length_limited/huffman_exact.cpp -o .build/length-limited/bounds
