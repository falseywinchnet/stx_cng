#!/bin/sh
set -eu
# Run after CMake and build_comparison.sh have produced the candidate library
# and separately named baseline objects. No timings run during this build.
decode_compiler=/opt/homebrew/opt/llvm/bin/clang++
decode_flags='-O3 -DNDEBUG -std=c++20 -arch arm64 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy'
"$decode_compiler" $decode_flags experiments/decode_tables/profile.cpp -o .build/decode-tables/profile
"$decode_compiler" $decode_flags experiments/decode_tables/profile_batch.cpp -o .build/decode-tables/profile-batch
"$decode_compiler" $decode_flags experiments/decode_tables/linked.cpp .build/decode-tables/prior_huffman.o .build/decode-tables/libwhole_candidate.a -o .build/decode-tables/linked
"$decode_compiler" $decode_flags experiments/decode_tables/linked.cpp .build/decode-tables/prior_huffman.o experiments/decode_tables/huffman_tail.cpp -o .build/decode-tables/linked-tail
"$decode_compiler" $decode_flags experiments/decode_tables/linked.cpp .build/decode-tables/prior_huffman.o experiments/decode_tables/huffman_batch_tail.cpp -o .build/decode-tables/linked-batch-tail
"$decode_compiler" $decode_flags experiments/decode_tables/frames.cpp .build/decode-tables/prior_match.o .build/decode-tables/prior_huffman.o .build/decode-tables/libwhole_candidate.a -o .build/decode-tables/frames-probe
