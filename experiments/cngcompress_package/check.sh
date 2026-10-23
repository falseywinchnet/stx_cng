#!/bin/sh
set -eu
package_compiler=/opt/homebrew/opt/llvm/bin/clang++
package_flags='-O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1 -Istx_cngcompress/include -Iexperiments/entropy'
"$package_compiler" $package_flags experiments/cngcompress_package/verify_frames.cpp experiments/whole_codec/match_bounded.cpp experiments/cngcompress_gate/huffman_short.cpp .build/cngcompress-package/libstx_cngcompress.a -o .build/cngcompress-package/verify-frames
package_record=${1:?fresh record name required}
package_root=$(pwd)
package_original="$package_root/.build/cngcompress-$package_record-original"
package_relocated="$package_root/.build/cngcompress-$package_record-relocated"
test ! -e "$package_original"
test ! -e "$package_relocated"
/opt/homebrew/bin/cmake --install .build/cngcompress-package --prefix "$package_original"
mv "$package_original" "$package_relocated"
/opt/homebrew/bin/cmake -S stx_cngcompress/tests/installed -B .build/cngcompress-$package_record-installed -DCMAKE_PREFIX_PATH="$package_relocated" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER="$package_compiler"
/opt/homebrew/bin/cmake --build .build/cngcompress-$package_record-installed -j4
/opt/homebrew/bin/ctest --test-dir .build/cngcompress-$package_record-installed --output-on-failure -V > .build/cngcompress-package/installed-$package_record.log
cat .build/cngcompress-package/installed-$package_record.log
/opt/homebrew/bin/ctest --test-dir .build/cngcompress-package --output-on-failure -V > .build/cngcompress-package/tests-$package_record.log
cat .build/cngcompress-package/tests-$package_record.log
for package_unit in huffman match; do
    "$package_compiler" -O3 -DNDEBUG -std=c++20 -fPIC -arch arm64 -S stx_cngcompress/src/$package_unit.cpp -o .build/cngcompress-package/$package_unit-arm64.s
    "$package_compiler" -O3 -DNDEBUG -std=c++20 -fPIC -arch x86_64 -march=x86-64 -mtune=generic -msse2 -mno-sse3 -mno-ssse3 -mno-sse4.1 -mno-sse4.2 -mno-avx -S stx_cngcompress/src/$package_unit.cpp -o .build/cngcompress-package/$package_unit-x86_64.s
done
