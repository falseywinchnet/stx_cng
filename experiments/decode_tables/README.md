# Grouped Huffman reader experiment

The grouped reader preserves every encoded byte of the exact-length candidate.
Its full-image measurements are encouraging, but the isolated packet results
conflict. Keep it experimental until the compiler and memory effects are resolved.
The smaller short-packet table and two tail-reader alternatives are retained
negative results. Published CNG sources and signatures are unchanged.

## Work removed

The existing four-stream decoder refills a 32-bit reservoir around each paired
lookup. The grouped interior loads one little-endian 64-bit word per stream and
performs four interleaved paired lookups before rechecking input and output bounds.
Each lookup consumes at most twelve bits and writes at most two symbols. A word
loaded at the consumed byte offset has at least 57 usable bits after its initial
0..7-bit shift; four lookups need at most 48. Entry requires eight real readable
bytes and eight writable output bytes in each lane. The original bounded reader
handles the remainder and validates exact consumption and padding.

There are no platform intrinsic forks, additional tables or heap allocations.
The existing 4096-entry single and paired lookup tables remain. The change adds
bounded scalar bit cursors; object sizes do not establish compiled stack usage.
Arm64 assembly folds the byte assembly into word loads and keeps four independent
recurrences. The x86-64 SSE2/no-AVX assembly spills several live values; it is
inspection evidence, not x86 execution or speed evidence.

## Measurements and conflicting evidence

Build: non-Apple LLVM 23.1.2, C++20, `-O3 -DNDEBUG`, arm64, on the selected M4
host. Execution: Apple A18 Pro. Compilation uses the installed LLVM driver
configuration; inspecting the driver showed target CPU apple-m1 and tune CPU
apple-m5, both with and without an explicit arm64 flag. The released comparator
uses Zstandard 1.5.7 level 3 and system zlib 1.2.12 for CRC.

Seven alternating paired trials are reduced to per-input medians. Ratios below
are sums of candidate medians divided by sums of baseline medians; lower is better.

| Comparison against exact-length candidate | Inputs | Decode ratio |
|---|---:|---:|
| Short-table variant, one translation unit | 2,953 packets | 1.031549 |
| Grouped reader, one translation unit | 2,953 packets | 0.927960 |
| Grouped reader, separately linked objects | 2,953 packets | 1.035511 |
| Explicit tail reader only, separate objects | 2,953 packets | 1.012500 |
| Grouped reader plus explicit tail, separate objects | 2,953 packets | 1.020357 |
| Grouped reader, complete entropy frames | 1,413 frames | 0.770764 |

Frame medians sum to 0.207443685 s baseline and 0.159890177 s candidate. General,
RGBA and composite frame ratios are respectively 0.751436, 0.784860 and 1.058924.
These are frame decoding with separate preallocated workspaces, not whole-image
cost. The difference between packet and frame measurements is unresolved. It may
involve compiler layout, cache state and call context; this experiment does not
identify a causal share or establish a universal kernel improvement.

The instrumented baseline's approximate stage totals are 0.004361457 s for model
reading, 0.014990294 s for lookup construction and 0.140138976 s for symbol decode.
About 88% is in the symbol body. Instrumentation changes generated code; these
stage medians are not an exact decomposition of the uninstrumented comparison.
The short-table variant reduces the 265 short packets to a 0.671231 ratio but
regresses the aggregate. Neither that variant nor the explicit-tail variants are
used in the complete-image candidate.

## Complete images

All 222 files are byte-identical to the preceding exact-length candidate. Both
complete decoder interfaces reconstruct the original source hashes. Ratios below
compare the grouped-reader candidate with the released Zstandard codec, including
codec allocation but excluding file I/O and caller pixel allocation.

| Corpus | Encode time | Decode time | Entropy decode time |
|---|---:|---:|---:|
| General, 131 | 0.916379 | 0.910670 | 0.705039 |
| RGBA, 80 | 0.911139 | 0.920134 | 0.723420 |
| Composite bodies, 11 | 1.007847 | 0.939782 | 0.797343 |

Size results are unchanged: 81 general, 29 RGBA and six composite cases remain
larger than the release. Smooth and rectangle-on-gradient remain unresolved.
Collection wrapper integration is not covered.

A direct comparison against the preceding exact-length implementation uses five
process runs of seven alternating trials for each selected case. Median complete
decode ratios are 0.943615 smooth, 0.886836 random, 0.967809 mixed order/noise,
0.875005 kodim01, 0.997670 rectangle-on-gradient, 1.012633 large smooth RGBA and
1.008671 composite control states. kodim01 improves in all five process runs;
large smooth RGBA regresses in all five. Random has no measured entropy decode
work yet shows a substantial complete-operation difference. This demonstrates
why the entire timing difference cannot be credited to the grouped reader.

## Verification and reproduction

The ordinary checks cover 1,178 byte-identical Huffman cases, 128 uniform-depth
checks, 432 match boundary/distribution cases and 414 independent scalar inverses.
The match-vector bundle exactly matches the preceding candidate. Packet probes
check every decoded output; all 1,413 complete frame comparisons also pass.
No automated memory integrity review was initiated.

Build sequentially from the repository using the configured M4 selector:

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/decode_tables -B .build/decode-tables -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/decode-tables -j4
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/decode_tables/build_comparison.sh
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/decode_tables/build_diagnostics.sh
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/decode_tables/assembly.sh
```

Copy binaries from the selected mirror before local execution. Use new output
paths; preserve the retained measurements. The extraction scripts validate source
files against the exact-length candidate evidence.

```sh
python3 -B experiments/decode_tables/extract.py --corpus .build/length-limited/corpus --output .build/decode-tables/new-packets
python3 -B experiments/decode_tables/extract_frames.py --corpus .build/length-limited/corpus --output .build/decode-tables/new-frames
.build/decode-tables/profile .build/decode-tables/new-packets/packets.bin .build/decode-tables/new-profile.csv
.build/decode-tables/profile-batch .build/decode-tables/new-packets/packets.bin .build/decode-tables/new-profile-batch.csv
.build/decode-tables/linked .build/decode-tables/new-packets/packets.bin .build/decode-tables/new-linked.csv
.build/decode-tables/frames-probe .build/decode-tables/new-frames/frames.bin .build/decode-tables/new-frames.csv
python3 -B experiments/decode_tables/run.py --corpus /Users/ultimussecundai/cng --binary .build/decode-tables/cng_whole_probe --output .build/decode-tables/new-whole
python3 -B experiments/decode_tables/compare.py --output .build/decode-tables/new-direct
python3 -B experiments/decode_tables/verify.py
```

`linked-tail` and `linked-batch-tail` take the same arguments as `linked`.
`build_diagnostics.sh` was added after measurements to consolidate their compiler
commands. Pinned CSVs, full-image reports, manifests and assembly live in evidence.
