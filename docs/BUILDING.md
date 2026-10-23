# Building stx_cng

The library requires CMake 3.21 or later, C and C++20 compilers, and zlib.
The bundled standalone stx_cngcompress package supplies entropy coding. It builds a static library. No image toolkit, browser engine, Python runtime
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

On another installation, set `CMAKE_PREFIX_PATH` to the dependency prefix. Zlib is located through
CMake's `FindZLIB`. This source release uses portable C++20, but the release
verification described here was performed on macOS arm64; a source portability
claim is not a completed Windows or Linux test result.

## Dependency contract

The source includes the independent MIT `stx_cngcompress` package. CNG uses its
public C interface and reusable encoder/decoder handles; no private compressor
header is consumed. The package also builds and installs by itself.

Zlib supplies CRC32. It is linked externally and not downloaded. Zstandard is
required only for the frozen v2 comparison/migration sources, which are excluded
from the production build. It is not an installed dependency of CNG v3.

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

Installation places both static libraries, public headers and relocatable CMake
package configurations beneath the prefix. A consuming C++ project uses:

```cmake
find_package(stx_cng 0.2 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE stx_cng::stx_cng)
```

For manual linking, use `libstx_cng`, `libstx_cngcompress`, zlib and the C++
runtime. The standalone compressor itself needs only the C++ and system runtimes.

## What the tests establish

The bundled test exercises 40 RGB/RGBA image cases, including multi-band images,
ramps, sharp steps, repeated patterns, noise and narrow dimensions. It checks both
stream output and caller-supplied output, then round-trips both collection forms
with mixed channel counts. It also checks explicit v2 rejection before
pixel mutation. Version migration is described in [the format](FORMAT.md). Temporary fixtures are created in the test working
directory and removed on success. A failed run leaves them available for
inspection; remove that fixture directory before repeating the test.

The larger 222-image verification corpus remains external. The source release
includes its results and source hashes, not third-party image files. These
ordinary equality and resource checks do not constitute an automated memory
integrity review.
