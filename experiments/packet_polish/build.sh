#!/bin/sh
set -eu
mkdir -p .build/packet-polish
/opt/homebrew/opt/llvm/bin/clang++ -O3 -DNDEBUG -std=c++20 -arch arm64 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Iexperiments/entropy experiments/packet_polish/probe.cpp -o .build/packet-polish/probe
