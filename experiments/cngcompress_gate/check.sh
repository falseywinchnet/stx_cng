#!/bin/sh
set -eu
gate_compiler=/opt/homebrew/opt/llvm/bin/clang++
gate_flags='-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy'
"$gate_compiler" $gate_flags experiments/decode_tables/check.cpp experiments/cngcompress_gate/huffman_short.cpp -o .build/cngcompress-gate-short/check
"$gate_compiler" $gate_flags experiments/whole_codec/bounds_checks.cpp experiments/whole_codec/match_bounded.cpp experiments/cngcompress_gate/huffman_short.cpp -o .build/cngcompress-gate-short/bounds
"$gate_compiler" $gate_flags -arch arm64 -S experiments/cngcompress_gate/huffman_short.cpp -o .build/cngcompress-gate-short/huffman-arm64.s
"$gate_compiler" $gate_flags -arch x86_64 -march=x86-64 -mtune=generic -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -S experiments/cngcompress_gate/huffman_short.cpp -o .build/cngcompress-gate-short/huffman-x86_64.s
