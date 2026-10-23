#!/bin/sh
set -eu
integration_root=$(pwd)
integration_record=${1:?fresh record name required}
integration_original="$integration_root/.build/cng-$integration_record-original"
integration_relocated="$integration_root/.build/cng-$integration_record-relocated"
test ! -e "$integration_original"
test ! -e "$integration_relocated"
/opt/homebrew/bin/cmake --build .build/cng-v3 -j4
/opt/homebrew/bin/ctest --test-dir .build/cng-v3 -V --output-on-failure > .build/cng-v3/tests-$integration_record.log
/opt/homebrew/bin/cmake --install .build/cng-v3 --prefix "$integration_original"
mv "$integration_original" "$integration_relocated"
/opt/homebrew/bin/cmake -S tests/installed -B .build/cng-$integration_record-consumer -DCMAKE_PREFIX_PATH="$integration_relocated" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/opt/homebrew/bin/cmake --build .build/cng-$integration_record-consumer -j4
/opt/homebrew/bin/ctest --test-dir .build/cng-$integration_record-consumer -V --output-on-failure > .build/cng-v3/installed-$integration_record.log
test ! -e "$integration_original"
otool -L .build/cng-$integration_record-consumer/installed > .build/cng-v3/libraries-$integration_record.log
cat .build/cng-v3/tests-$integration_record.log
cat .build/cng-v3/installed-$integration_record.log
cat .build/cng-v3/libraries-$integration_record.log
