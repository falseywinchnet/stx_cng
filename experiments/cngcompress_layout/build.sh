#!/bin/sh
set -eu
layout_compiler=/opt/homebrew/opt/llvm/bin/clang++
layout_flags='-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy'
mkdir -p .build/cngcompress-layout
"$layout_compiler" $layout_flags -arch arm64 experiments/cngcompress_layout/probe.cpp experiments/cngcompress_layout/layout.cpp experiments/cngcompress_layout/constant.cpp experiments/cngcompress_bulk/merge.cpp experiments/decode_tables/huffman_batch.cpp -o .build/cngcompress-layout/probe
"$layout_compiler" $layout_flags -arch arm64 -S experiments/cngcompress_layout/layout.cpp -o .build/cngcompress-layout/layout-arm64.s
"$layout_compiler" $layout_flags -arch x86_64 -march=x86-64 -mtune=generic -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -S experiments/cngcompress_layout/layout.cpp -o .build/cngcompress-layout/layout-x86_64.s
"$layout_compiler" $layout_flags -arch arm64 experiments/cngcompress_layout/constant_test.cpp experiments/cngcompress_layout/constant.cpp -o .build/cngcompress-layout/constant-test
"$layout_compiler" $layout_flags -arch arm64 -S experiments/cngcompress_layout/constant.cpp -o .build/cngcompress-layout/constant-arm64.s
"$layout_compiler" $layout_flags -arch x86_64 -march=x86-64 -mtune=generic -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -S experiments/cngcompress_layout/constant.cpp -o .build/cngcompress-layout/constant-x86_64.s
