#!/bin/sh
set -eu
python3 -B experiments/zxc_comparison/verify_source.py
/opt/homebrew/bin/cmake -S experiments/zxc_comparison -B .build/zxc-comparison -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/opt/homebrew/bin/cmake --build .build/zxc-comparison -j4
