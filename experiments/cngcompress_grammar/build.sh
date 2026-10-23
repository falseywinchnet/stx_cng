#!/bin/sh
set -eu
grammar_compiler=/opt/homebrew/opt/llvm/bin/clang++
grammar_flags='-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy'
mkdir -p .build/cngcompress-grammar
"$grammar_compiler" $grammar_flags -DCNG_NAMESPACE=cng_prefix -DCNG_PREFIX=1 -DCNG_SPLIT=0 -DCNG_MODE=17 -c experiments/cngcompress_grammar/grammar.cpp -o .build/cngcompress-grammar/prefix.o
"$grammar_compiler" $grammar_flags -DCNG_NAMESPACE=cng_split -DCNG_PREFIX=0 -DCNG_SPLIT=1 -DCNG_MODE=18 -c experiments/cngcompress_grammar/grammar.cpp -o .build/cngcompress-grammar/split.o
"$grammar_compiler" $grammar_flags -DCNG_NAMESPACE=cng_combined -DCNG_PREFIX=1 -DCNG_SPLIT=1 -DCNG_MODE=19 -c experiments/cngcompress_grammar/grammar.cpp -o .build/cngcompress-grammar/combined.o
"$grammar_compiler" $grammar_flags experiments/cngcompress_grammar/probe.cpp experiments/whole_codec/match_bounded.cpp experiments/decode_tables/huffman_batch.cpp .build/cngcompress-grammar/prefix.o .build/cngcompress-grammar/split.o .build/cngcompress-grammar/combined.o -o .build/cngcompress-grammar/probe
"$grammar_compiler" $grammar_flags experiments/cngcompress_grammar/integer_test.cpp experiments/decode_tables/huffman_batch.cpp -o .build/cngcompress-grammar/integer-test
"$grammar_compiler" $grammar_flags experiments/cngcompress_grammar/cost_witness.cpp -o .build/cngcompress-grammar/cost-witness
"$grammar_compiler" $grammar_flags -DCNG_NAMESPACE=cng_prefix -DCNG_PREFIX=1 -DCNG_SPLIT=0 -DCNG_MODE=17 -S experiments/cngcompress_grammar/grammar.cpp -o .build/cngcompress-grammar/prefix-arm64.s
"$grammar_compiler" $grammar_flags -arch x86_64 -march=x86-64 -mtune=generic -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -DCNG_NAMESPACE=cng_prefix -DCNG_PREFIX=1 -DCNG_SPLIT=0 -DCNG_MODE=17 -S experiments/cngcompress_grammar/grammar.cpp -o .build/cngcompress-grammar/prefix-x86_64.s
