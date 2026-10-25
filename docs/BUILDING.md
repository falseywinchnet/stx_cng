# Building stx_cng

The library requires CMake 3.21 or later, a C++20 compiler and zlib. It builds
as one static library. No image toolkit, compression library, Python runtime or
platform graphics API is required.

## Configure, build, test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Zlib is located through CMake's `FindZLIB`; on another installation, set
`CMAKE_PREFIX_PATH` to the dependency prefix. The source is portable C++20
without platform intrinsics; it is built and verified with LLVM clang on
macOS arm64 and with GCC on Linux x86-64, where the encoder produces identical
bytes.

## Dependency contract

Zlib supplies the CRC32 of collection chunks. It is linked externally and not
downloaded. The standalone codec computes its own CRC32 and has no dependency
beyond the C++ standard library. The `stx_cngcompress` package in this
repository was the entropy coder of releases 0.1 and 0.2; it remains as an
independent package and is not built or linked by this release.

## Embed from source

```cmake
add_subdirectory(external/stx_cng)
target_link_libraries(my_application PRIVATE stx_cng::stx_cng)
```

This exposes the public include directory and the C++20 requirement. Set
`BUILD_TESTING` to `OFF` when the embedding build does not need the bundled tests.

## Install

```sh
cmake --install build --prefix /your/install/prefix
```

Installation places the static library, public headers and a relocatable CMake
package configuration beneath the prefix. A consuming C++ project uses:

```cmake
find_package(stx_cng 0.3 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE stx_cng::stx_cng)
```

For manual linking, use `libstx_cng`, zlib and the C++ runtime.

## What the tests establish

`stx_cng_roundtrip` exercises 40 RGB/RGBA image cases, including multi-frame
images, ramps, sharp steps, repeated patterns, noise and narrow dimensions. It
checks both stream output and caller-supplied output, round-trips both
collection forms with mixed channel counts, checks that a `CSP` v3 header of
the previous release is rejected before pixel output, and that a flipped
payload byte is reported as a frame checksum failure.

`stx_cng_malformed` decodes about 70,000 damaged inputs through the public
interfaces: every truncation of six images and two collections, every
single-bit flip of their headers, directories and payload edges, forged
payloads of arbitrary bytes with recomputed checksums, header fields claiming
impossible or oversized dimensions, and whole index copies zeroed or filled.
Each input must be rejected or decode exactly (a forged payload may decode to
anything), must never write outside the destination, and, when the tests are
configured with `-DSTX_CNG_SANITIZE=ON`, must raise no address or
undefined-behaviour sanitizer report. The release runs this configuration.

Temporary fixtures are created in the test working directory and removed on
success. The measurement corpus remains external; the repository includes its
results and source hashes, not third-party image files.
