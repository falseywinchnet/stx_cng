# Building stx_cng

The library requires CMake 3.20 or later, a C++20 compiler, Zstandard **1.5.7** and
zlib. It builds a static library. No image toolkit, browser engine, Python runtime
or platform graphics API is required.

## Configure, build, test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The verified macOS configuration uses non-Apple LLVM 23.1.2:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
ctest --test-dir build --output-on-failure
```

On another installation, set `CMAKE_PREFIX_PATH` to the dependency prefix or
supply `ZSTD_INCLUDE_DIR` and `ZSTD_LIBRARY` explicitly. Zlib is located through
CMake's `FindZLIB`. This source release uses portable C++20, but the release
verification described here was performed on macOS arm64; a source portability
claim is not a completed Windows or Linux test result.

## Dependency contract

The encoder uses Zstandard's advanced static-context API to give its entropy
workspace one explicit owner and a capacity ceiling. CMake checks that the header
version is 1.5.7; context construction also checks the runtime library version.
Supply matching headers and library. Upgrading Zstandard requires another
compatibility, capacity and performance check.

Zlib supplies CRC32. The image payload uses Zstandard, not zlib's DEFLATE
compressor. Dependencies are linked externally and are not downloaded by this
project. Their licenses remain separate from this repository's MIT license.

## Embed from source

```cmake
add_subdirectory(external/stx_cng)
target_link_libraries(my_application PRIVATE stx_cng::stx_cng)
```

This exposes the public include directory and the C++20 requirement. The target
carries the libraries needed for linking the static archive. Set `BUILD_TESTING`
to `OFF` when the embedding build does not need the bundled tests.

## Install

```sh
cmake --install build --prefix /your/install/prefix
```

Installation places `libstx_cng.a` (or the platform's static-library equivalent)
and the `stx_cng/` public headers beneath the selected prefix. For manual
integration, add that include directory and link the archive, Zstandard and zlib.
This release does not install a `find_package(stx_cng)` configuration.

## What the tests establish

The bundled test exercises 40 RGB/RGBA image cases, including multi-band images,
ramps, sharp steps, repeated patterns, noise and narrow dimensions. It checks both
stream output and caller-supplied output, then round-trips both collection forms
with mixed channel counts. Temporary fixtures are created in the test working
directory and removed on success. A failed run leaves them available for
inspection; remove that fixture directory before repeating the test.

The larger 222-image verification corpus remains external. The source release
includes its results and source hashes, not third-party image files. These
ordinary equality and resource checks do not constitute an automated memory
integrity review.
