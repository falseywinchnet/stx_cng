# Building stx_cng

stx_cng needs CMake 3.21 or later and a C++20 compiler with its C compiler. Nothing else: no system library, package
manager, download or extra toolchain. It builds as one static library, `libstx_cng`.

## Configure, build, test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Dependencies

The only third-party code is Zstandard 1.5.7, which compresses the per-image tables. Its official release tarball
is checked in at `third_party/zstd-1.5.7.tar.gz` and pinned by SHA-256. Configuration verifies the hash, extracts
the library sources into the build tree, checks the extracted licence against `LICENSE-zstd.txt`, and compiles
those sources (single threaded, without assembly, legacy formats or the dictionary builder) into `libstx_cng`.
No zstd header or type reaches the public interface, and nothing outside `libstx_cng` needs to be linked. Every
build uses the same Zstandard release, so the encoder writes the same bytes on every platform.

CRC-32 is implemented in the library. Threads come from the C++ standard library; on platforms where that needs a
thread library, CMake's `Threads` package supplies it.

## Embed from source

```cmake
add_subdirectory(external/stx_cng)
target_link_libraries(my_application PRIVATE stx_cng::stx_cng)
```

This exposes the public include directory and the C++20 requirement. Set `BUILD_TESTING` to `OFF` when the
embedding build does not need the tests.

## Install

```sh
cmake --install build --prefix /your/install/prefix
```

Installation places `libstx_cng`, the public headers, the licences and a relocatable CMake package beneath the
prefix. A consuming project uses:

```cmake
find_package(stx_cng 1.0 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE stx_cng::stx_cng)
```

For manual linking, link `libstx_cng` and the C++ runtime (and the platform thread library where one is separate).

## Tests

| Test | What it establishes |
| --- | --- |
| `stx_cng_roundtrip` | 132 images of twelve size classes from 1 × 1 to 300 × 200 and across segment edges, BGR and BGRA, six patterns: every encode and decode call, one, four and all threads, identical files from one and four encoder threads, the mosaic, salvage of an intact and of a damaged file, every error reason, and both collection forms |
| `stx_cng_conformance` | Every file in `tests/vectors` decodes exactly at one and four threads, its mosaic is the block means of its samples, and encoding its samples writes it byte for byte |
| `stx_cng_malformed` | Every truncation and every flipped byte of small files, sampled ones of larger files, random bursts, and crafted files whose checksums all match (random segment payloads, random table bodies in valid Zstandard frames, random mosaics, random dimensions, every quantized frequency above 25), plus damaged collections. Each input must be rejected or, where the original was kept, decode to it exactly |

`-DSTX_CNG_SANITIZE=ON` builds the library, Zstandard and the tests with the address and undefined-behaviour
sanitizers; any report fails the test. The release runs the whole suite in this configuration.

## Verified platforms

| Platform | Compiler | Release tests | Sanitizer tests | Encoder bytes |
| --- | --- | --- | --- | --- |
| macOS 26 arm64 (Apple M4) | Apple clang | pass | pass | identical to the vectors |
| Linux x86-64 (Ubuntu 24.04, AMD Ryzen Threadripper 7960X) | GCC 13.3 | pass | pass | identical to the vectors |

The vectors were written on macOS; both platforms re-encode every vector byte for byte. MSVC is not yet verified.
The Linux record is [`evidence/cng-1.0/linux-x86_64-gcc13.txt`](../evidence/cng-1.0/linux-x86_64-gcc13.txt).

