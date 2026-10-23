#!/bin/sh
set -eu
decode_compiler=/opt/homebrew/opt/llvm/bin/clang++
decode_flags='-O3 -DNDEBUG -std=c++20 -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy'
"$decode_compiler" $decode_flags -arch arm64 -S experiments/decode_tables/huffman_batch.cpp -o .build/decode-tables/huffman-arm64.s
"$decode_compiler" $decode_flags -arch x86_64 -msse2 -mno-avx -S experiments/decode_tables/huffman_batch.cpp -o .build/decode-tables/huffman-x86_64.s
