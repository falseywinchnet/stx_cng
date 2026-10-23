#!/bin/sh
set -eu
bulk_compiler=/opt/homebrew/opt/llvm/bin/clang++
bulk_flags='-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy'
mkdir -p .build/cngcompress-bulk
"$bulk_compiler" $bulk_flags -arch arm64 experiments/cngcompress_bulk/probe.cpp experiments/cngcompress_bulk/merge.cpp experiments/decode_tables/huffman_batch.cpp -o .build/cngcompress-bulk/probe
"$bulk_compiler" $bulk_flags -arch arm64 -S experiments/cngcompress_bulk/merge.cpp -o .build/cngcompress-bulk/merge-arm64.s
"$bulk_compiler" $bulk_flags -arch x86_64 -march=x86-64 -mtune=generic -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -S experiments/cngcompress_bulk/merge.cpp -o .build/cngcompress-bulk/merge-x86_64.s
