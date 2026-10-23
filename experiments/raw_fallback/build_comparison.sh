#!/bin/sh
set -eu
comparison_compiler=/opt/homebrew/opt/llvm/bin/clang++
comparison_flags='-O3 -DNDEBUG -std=c++20 -arch arm64 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc -Iexperiments/whole_codec -Iexperiments/entropy'
comparison_definitions='-DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Dcng_candidate=cng_prior -Dcng_match_trial=cng_match_prior -Dcng_huffman_kernel=cng_huffman_prior'
# The prior implementation is compiled under distinct names so both candidates
# execute in one comparison process. Original source files remain unchanged.
"$comparison_compiler" $comparison_flags $comparison_definitions -c experiments/whole_codec/candidate_codec.cpp -o .build/raw-fallback/prior_codec.o
"$comparison_compiler" $comparison_flags $comparison_definitions -c experiments/whole_codec/match_bounded.cpp -o .build/raw-fallback/prior_match.o
"$comparison_compiler" $comparison_flags $comparison_definitions -c experiments/entropy_reduction/huffman_reduced.cpp -o .build/raw-fallback/prior_huffman.o
# The released public declarations have the same value types and function
# signatures as the private candidate. Renaming their namespace makes the
# existing probe compare prior CNE files against the new candidate directly.
"$comparison_compiler" $comparison_flags -Dstx_cng=cng_prior -c experiments/whole_codec/probe.cpp -o .build/raw-fallback/comparison_probe.o
"$comparison_compiler" $comparison_flags .build/raw-fallback/comparison_probe.o .build/raw-fallback/prior_codec.o .build/raw-fallback/prior_match.o .build/raw-fallback/prior_huffman.o .build/raw-fallback/libfallback_candidate.a .build/raw-fallback/libfallback_shared.a -lz -o .build/raw-fallback/cng_direct_comparison
