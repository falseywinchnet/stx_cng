# Comparing zxc with CNG's entropy candidate

zxc supplies a useful decoder design reference. It does not currently give us a
balanced drop-in replacement on the retained CNG data. Level 7 saves 56.4% of
entropy-frame decode time, but takes 17.6 times as long to encode and produces
2.56% more bytes. Lower levels are faster but substantially larger. Its pivoted
Huffman layout is the strongest isolated lead: identical code lengths decode
53.1% faster with NEON, with 0.074% more bytes overall. Without explicit intrinsics,
that layout takes 2.92 times our current Huffman decode time.

These are entropy-frame and Huffman-section measurements, not complete CNG image
results. Keep this distinction when comparing them with the preceding 8–9%
whole-image decode improvement. Published codec sources and dependencies are
unchanged; the reference is an optional, isolated experiment.

## Reference and method

Reference: [zxc v0.15.0](https://github.com/hellobertrand/zxc/tree/2d29ab6fdf0e935781d287dd1ec2bf41d32a3df6),
commit `2d29ab6fdf0e935781d287dd1ec2bf41d32a3df6`, BSD-3-Clause. Its vendored
rapidhash retains its own notice. Sources remain in ignored `build/zxc-study`;
no third-party implementation is copied into the first-party codec. The source
pin covers build inputs, headers, implementation, license and the documents read.
The release differs from the moving main branch, so use the pinned sources for
reproduction and algorithm details.

Build: non-Apple LLVM 23.1.2 on the selected M4 host, C/C++ Release `-O3 -DNDEBUG`.
Execution: Apple A18 Pro. zxc uses its arm64 NEON baseline. Native-host ISA tuning,
LTO and PGO are disabled by CMake options; ordinary installed compiler defaults
still apply. The comparison library installs nothing. Zstandard is pinned to
1.5.7, level 3. Checksums, dictionaries and seek tables are disabled for the entropy
comparison; existing outer CNG CRC work is not timed here. Calls are single-threaded.

The retained 1,413 entropy frames come from 222 exact-length candidate images.
We reconstruct their original bytes, then feed identical bytes to each codec.
CNG uses the current grouped reader and the existing eight-byte matcher; every
CNG encoding must reproduce its retained frame exactly. Reusable contexts and
buffers are warmed outside timing. Three measured rounds rotate codec order;
results sum each frame's median. Allocation, file I/O and verification are outside
these measurements. Encode and decode are complete frame calls, including their
own headers, raw escapes and internal block processing.

CNG has 128 KiB internal blocks and references across the current frame. zxc uses
its default 512 KiB blocks and 64 KiB match window. This is a comparison of complete
existing algorithms, not an equal-window or equal-memory contest. zxc's seven
levels are reference configurations, not proposed CNG user options.

## Retained entropy-frame results

There are 280,540,670 input bytes. The current CNG candidate stores 180,012,867
bytes; its summed median encode/decode times are 0.607788237 / 0.185830655 seconds.
Ratios are comparator divided by CNG; lower is better.

| Comparator | Bytes | Encode time | Decode time | Frames smaller/equal/larger than CNG |
|---|---:|---:|---:|---:|
| Zstandard 1.5.7 level 3 | 1.009301 | 1.483157 | 1.441017 | 863 / 1 / 549 |
| zxc level 1 | 1.452958 | 0.045146 | 0.053601 | 17 / 0 / 1396 |
| zxc level 3 | 1.407090 | 0.675800 | 0.081762 | 18 / 0 / 1395 |
| zxc level 5 | 1.303185 | 4.705718 | 0.178070 | 24 / 0 / 1389 |
| zxc level 6 | 1.254181 | 13.989525 | 0.223364 | 48 / 0 / 1365 |
| zxc level 7 | 1.025639 | 17.645483 | 0.436216 | 227 / 0 / 1186 |

Level 7 by source group:

| Source group | Bytes | Encode time | Decode time |
|---|---:|---:|---:|
| General, 661 frames | 1.02089 | 18.192 | 0.440 |
| RGBA, 720 frames | 1.02963 | 17.274 | 0.433 |
| Composite bodies, 32 frames | 1.00637 | 15.805 | 0.526 |

These are retained frames. Outer literal raw fallback means some rejected
compression attempts are absent, so this dataset alone does not establish whole
encoder cost, noise behavior or final file size. No confidence interval or
cross-machine performance claim is inferred from three rounds.

## Noise and repetition controls

To check selection bias, a second comparison uses 20 existing deterministic
match vectors: sizes 128 KiB and 256 KiB plus one byte; periods zero (noise), one,
seven, 65,536 and 131,072; with and without a sparse disturbance every 1,031 bytes.
The original vector source, raw bytes and frame hashes are pinned. All seven
codecs reconstruct every sample and CNG reproduces its original encoded bytes.

| Pattern, four controls each | CNG bytes | zxc level 7 bytes | Level 7 encode ratio | Level 7 decode ratio |
|---|---:|---:|---:|---:|
| Deterministic byte noise | 786,454 | 786,590 | 46.03 | 2.12 |
| Period 1 | 168 | 348 | 35.56 | 2.47 |
| Period 7 | 372 | 679 | 31.86 | 1.51 |
| Period 65,536 | 268,316 | 786,590 | 74.67 | 0.80 |
| Period 131,072 | 528,084 | 786,590 | 61.17 | 1.23 |

Levels 1 and 3 encode the noise controls faster than CNG (0.280 and 0.450 ratios),
while still taking about 2.2 times its raw-frame decode time. These operations are
small in absolute time; ratios are not a claim about user-visible latency.
The long-period cases expose losses that the retained image total would hide.
In particular, a 128 KiB period exceeds zxc's window. The 64 KiB boundary result
is recorded without attributing it to an unverified internal cause.

## Isolating the Huffman layout

The source of the idea is Marcin Żukowski's
[PivCo-Huffman paper](https://marcinzukowski.github.io/pivco-huffman/paper-1.0/ph.html).
Instead of writing complete codewords consecutively, PivCo groups each tree
node's branch decisions into a bitmap. Reconstruction merges the children's
symbol lists using that bitmap. Flat subtrees admit direct unpacking, and leaf
pairs admit simple bit selection. This exposes bulk work rather than four serial
bit cursors, but introduces intermediate storage and passes through the symbols.
The wavelet-tree connection is about indexing symbols, not CNG's spatial image
pyramid.

The layout-only probe reconstructs all 2,953 retained Huffman packet inputs.
It builds one common complete canonical code for both layouts. zxc supports at
most eleven bits, so 2,322 packets are rebuilt with our exact eleven-bit package
merge; the remaining codes already fit. No zxc tree nudging, match search or
probability fitting is involved. The standard layout is serialized into our
existing grammar and decoded with the grouped reader. PivCo uses zxc's internal
section API through a small declared C boundary. Both reconstruct exact bytes.

Encoding and model construction are outside timing. After warmup, seven alternating
paired decode trials include model reading and table/tree setup. This establishes
a layout-specific decode result, not its encode overhead or whole-codec benefit.

| Layout comparison | Packets | PivCo / standard bytes | PivCo / standard decode time |
|---|---:|---:|---:|
| NEON enabled, all | 2,953 | 1.000738 | 0.469261 |
| NEON enabled, <=1,024 symbols | 265 | 1.221640 | 0.344686 |
| NEON enabled, >1,024 symbols | 2,688 | 1.000642 | 0.469949 |
| Explicit SIMD disabled, all | 2,953 | 1.000738 | 2.915373 |

Native decode totals are 0.140308029 s standard and 0.065841141 s PivCo. Without
explicit SIMD they are 0.140719174 s standard and 0.410248896 s PivCo. The second
build disables zxc's explicit intrinsics; compiler autovectorization remains
allowed. It is not a strict scalar-code experiment. All encoded byte counts and
common payload-bit counts agree between these builds.

The formats have different metadata: zxc uses a fixed 128-byte lengths header,
while ours uses run coding and one or four stream extents. Per-node byte padding
also differs. Hence equal code lengths do not imply equal serialized bytes. A
future in-house layout should retain our compact table representation and measure
small inputs rather than copying the foreign header.

## Memory and useful adaptations

The public static-workspace size functions report the following at zxc's default
512 KiB block size. These are API-sized static workspaces, not measured heap peaks
of the reusable-context benchmark. CNG figures are its declared heap workspace
objects; its stack lookup tables and temporaries are additional.

| Implementation | Encoder bytes | Decoder bytes |
|---|---:|---:|
| Current CNG | 656,400 | 131,088 |
| zxc levels 1–5, static context | 1,876,672 | 1,680,512 |
| zxc levels 6–7, static context | 6,136,768 | 1,680,512 |

The isolated PivCo decoder requires an extra `n+32` scratch bytes and 32 bytes of
output slack. A portable CNG implementation would need its own explicit storage
contract. Its present 128 KiB token workspace cannot silently supply both buffers.

Recommended research order:

1. Keep the current matcher and compact length table. Investigate bottom-up
   bitmap merging in fixed common-width batches, using portable expressions,
   explicit disjoint buffers and compiler inspection. Native NEON speed is a
   target, not proof that a no-intrinsics rewrite will attain it. Test equal
   code lengths first so parsing and distribution changes cannot hide the result.
2. Separately test prefix-width integers or a compact control stream. Our current
   parsed stream intermixes lengths, literals and references, then Huffman-codes
   the whole stream. zxc separates control fields and literals; its prefix varint
   reveals the width in the first byte. These may remove dependency and parsing
   work without the PivCo scratch cost. They change experimental wire grammar;
   they are not format-neutral decoder substitutions. Do not silently copy zxc's
   three-byte limit: CNG admits 2 MiB plus eight bytes per frame.
3. Preserve useful parser reasoning without importing its large DP workspace.
   The inspected high-level parser groups equal-price match lengths and reuses
   coverage from adjacent queries, updating cost-class boundaries when its proof
   applies. Its prices and searched edges differ from exact final packet cost.
   Calling this globally optimal for our histogram-dependent packet objective
   would be unjustified. Any transfer needs a proof under our actual costs.

The recommendation is selective algorithm research, not a portfolio of user-facing
modes and not wholesale substitution. There is enough measured promise to justify
the bulk-Huffman question, and enough measured cost to reject assuming it is an
automatic portable win.

## Reproduction and evidence

Clone the pinned tag into `build/zxc-study/source`, verify its resolved commit
against `pin.json`, and export it with `git archive`. Transfer that archive to the
same ignored `build/zxc-study/source` directory in the selected M4 mirror. The
root `m4build` helper deliberately excludes `build/`; preserve source and generated
output there and do not install dependencies. `verify_source.py` checks all pinned
build inputs before the native build.

```sh
python3 -B experiments/zxc_comparison/verify_source.py
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/zxc_comparison/build.sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/zxc_comparison -B .build/zxc-comparison-portable -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ -DZXC_DISABLE_SIMD=ON
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/zxc-comparison-portable -j4
```

Copy completed binaries back before local execution. Run comparisons sequentially,
using fresh outputs (the probes refuse overwrites):

```sh
.build/zxc-comparison/compare .build/decode-tables/frames/frames.bin .build/zxc-comparison/new-frames.csv
.build/zxc-comparison/huffman .build/decode-tables/corpus/packets.bin .build/zxc-comparison/new-huffman.csv
.build/zxc-comparison/huffman-no-intrinsics .build/decode-tables/corpus/packets.bin .build/zxc-comparison/new-huffman-portable.csv
python3 -B experiments/zxc_comparison/extract_controls.py
.build/zxc-comparison/compare .build/zxc-comparison/controls/frames.bin .build/zxc-comparison/new-controls.csv
python3 -B experiments/zxc_comparison/verify.py
```

`extract_controls.py` refuses to replace its output directory. Existing frame and
packet extractors are in `experiments/decode_tables` and verify source images.
Recorded CSVs, completion logs, source/binary hashes and input manifests are in
`evidence`. Source pin checking was added after the first build and then run on
the unchanged downloaded sources. Ordinary inverse checks pass throughout. No
automated memory integrity review was initiated or claimed.
