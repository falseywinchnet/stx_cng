# Standalone cngcompress package verification

The staged `stx_cngcompress/` directory builds independently of CNG and external
compression libraries. It exposes a C99 header backed by a C++20 static archive,
reusable opaque handles, explicit status/size results, and independently bounded
frames. Its selected implementation preserves the measured matcher, compact
Huffman table and short-stream decoder. Packaging does not introduce another
coding mode or change valid frame bytes.

The public API reproduces **414 known-raw vectors and 1,413 retained frames byte
for byte**, and decodes them exactly. The 414-vector reference was independently
inverted in the preceding gate; the retained frame comparison also uses the
preceding private decoder. This is compatibility evidence, not an independent
proof for every possible frame.

The package's ordinary native tests pass **432 generated frame cases**, including
empty and maximum input, block boundaries, repeated and disturbed patterns,
handle reuse, capacity/argument status contracts and two independent concurrent
contexts. A C99 consumer passes both in the package build and after installing,
moving the installation prefix, and building a separate consuming project.
The original prefix is absent during that check. The consumer's dynamic library
list contains only the system and C++ runtimes; it does not link Zstandard or
zlib. Public handle storage is 656,400 encoder bytes and 131,088 decoder bytes on
this ABI, excluding allocator metadata and bounded stack temporaries.

One invalid-input rule is tightened relative to the private experiment: the
reserved block flag pair `11` is rejected rather than interpreted as a raw block.
Valid encoder bytes are unchanged. These are ordinary deterministic checks;
no automated memory integrity review was initiated or claimed.

## Environment and reproduction

Non-Apple LLVM 23.1.2 builds Release on the selected M4 host. Package and relocated
consumer tests execute there. Retained-frame identity executes on the local
Apple A18 Pro. The final installation record is `final-v2`.

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S stx_cngcompress -B .build/cngcompress-package -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DSTX_CNGCOMPRESS_BUILD_TESTS=ON
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/cngcompress-package -j4
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/cngcompress_package/check.sh NEW-RECORD
```

The helper refuses to replace installation prefixes. Copy its completed arm64
`verify-frames` executable back before the local identity check:

```sh
.build/cngcompress-package/verify-frames .build/cngcompress-gate-short/match-vectors.bin .build/decode-tables/frames/frames.bin
python3 -B experiments/cngcompress_package/verify.py
```

The helper also emits arm64 and strict baseline x86-64/SSE2 assembly, disabling
SSE3, SSSE3, SSE4 and AVX in the latter. Histogram reduction retains common-width
vector operations. No x86 execution or claim that the whole decoder vectorizes
is implied. No fresh timing claim comes from packaging: the complete-image gate
remains the performance authority until the public integration is measured.

Evidence pins package sources, the comparison driver, binaries, assembly and
completion logs. CNG integration, version compatibility, standalone repository
export and publication remain separate outstanding deliverables.
