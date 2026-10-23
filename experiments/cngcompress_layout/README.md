# Complete portable bottom-up Huffman layout

This follows the portable merge study with a complete experimental packet
encoder/decoder. It keeps the exact 12-bit code lengths and compact run-coded
length table used by the current candidate. The matcher is unchanged. A private
packet marker 177 distinguishes this experiment; no released format is changed.

## Wire layout and work

The five-byte prefix contains marker and uncompressed count, followed by the
existing compact Huffman lengths. A preorder tree traversal emits one bitmap
for each irregular node, and fixed-width symbol indices for each maximal flat
subtree. Each emitted node ends on a byte boundary with zero padding. Leaves emit
nothing. The decoder reconstructs the canonical tree, reads an irregular node's
bitmap, counts its ones to determine child sizes, recursively reconstructs the
children, then merges them. No per-node counts or lengths are transmitted.

Flat subtrees use widths 1..8, selected before the decoding loop. Their symbol
maps contain at most 256 entries in total because maximal flat subtrees have
disjoint leaves. Constant children feed directly into a merge without an
intermediate filled array. `layout_first.cpp` uses scalar constant-child merging;
`layout.cpp` uses packed-word expansion from `constant.cpp`. The latter expands
only the variable child and combines it with repeated constant bytes.

Decoder scratch is **n bytes**, in addition to the n-byte output and fixed tree
storage. Encoder scratch is **2n bytes**. No dynamic allocations occur in either
codec call. Recursive decoding alternates output and scratch slices; a child can
use only its own slice, preserving the sibling's already reconstructed values.
Depth is bounded by 12. The declared tree arrays contain 511 nodes, 256 mapping
bytes and 256 16-bit codes; the tree object is 8,952 bytes with this ABI. Length,
canonical-construction and reachability arrays, recursive call frames and compiler
spills are additional stack usage; this is not a measured peak-stack claim.

The source-level lower bound on corpus reconstruction work remains 883,150,653
binary-merge output bytes, or 3.5333 per original symbol. Flat decoding adds its
own writes. Bitmap population counting adds a separate read of the 110,426,285
bitmap bytes already counted in the primitive study. These are logical accesses,
not DRAM or cache-miss measurements. They explain why a good isolated merge need
not produce a fast complete decoder.

## Measurement scope

Build: non-Apple LLVM 23.1.2 on the selected M4 host. Execution: Apple A18 Pro.
Seven measured trials after one warmup, four methods in rotating order per packet
and trial. The same 2,953 retained packets contribute 249,948,888 source symbols.
All allocation, original corpus decoding, population counting, and common code
length selection occur before timing. Output equality is checked after each timed
decode. The encoder measurements include packet/tree construction and emission
but exclude the shared length selection. The classic encoder is the experiment's
fixed-length byte-emitting reference, not a measurement of the production
encoder; do not report its ratio as whole-encoder performance.

Both decode timings include reading the compact table, constructing decode data,
validating extents/padding, and producing all output bytes. There is no prebuilt
plan in the timed decode. No stored/raw fallback or singleton grammar is being
compared: these tests isolate equal-code Huffman payload layouts.

Initial complete result with scalar constant merges:

| Measure | Current Huffman layout | Bottom-up layout |
|---|---:|---:|
| Encoded bytes | 169,520,487 | 169,541,515 |
| Fixed-length encode seconds | 0.568065 | 4.342133 |
| Complete decode seconds | 0.145748 | 0.377548 |

Bottom-up decoding takes 2.5904 times as long overall, with 2,486 packet
regressions. On 265 packets of at most 1,024 symbols it takes 0.7205 times the
current decoder time, but adds 2.5291% bytes. The 2,688 larger packets take 2.6002
times the decode time. This is a negative overall result; the small-input win
does not justify adding another public mode.

The packed constant-child follow-up retains exactly the same packet sizes:

| Measure | Current Huffman layout | Bottom-up with packed constant merges |
|---|---:|---:|
| Encoded bytes | 169,520,487 | 169,541,515 |
| Fixed-length encode seconds | 0.594484 | 4.605699 |
| Complete decode seconds | 0.152488 | 0.261441 |

The decode ratio improves to **1.7145**, but 2,404 packets still regress. Short
packets take 0.7013 times the current decoder time (seven regressions); larger
packets take 1.7198 times as long (2,397 regressions). Separate-run absolute
changes include ambient variation; use the paired baselines. This implementation
will not replace the current Huffman layout. Next experiments examine prefix-width
integers and separated controls independently. This result is not a proof that
all possible portable PivCo implementations lose, but it rejects the measured
construction without hiding its smaller-input wins.

## Exactness and assembly

Each complete run checks 280 flat-tree fixtures: all widths 1..8 at lengths
0..32, 1,025 and 131,073, followed by all 2,953 corpus comparisons. The optimized
constant-child primitive separately passes 147,968 two-sided bitmap/boundary
fixtures. These are deterministic valid-input tests, not an automated memory
integrity review or proof of untrusted-input audit coverage.

Assembly is retained for arm64 and strict baseline x86-64/SSE2 with SSE3, SSSE3,
SSE4 and AVX disabled. The general merge reuses the preceding verified source;
bitmap populations become SIMD byte counts on arm64. Constant-child expansion
uses fixed shifts and masks. x86-64 and big-endian paths have not been executed.

## Reproduction

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/cngcompress_layout/build.sh
```

Copy the completed binaries from the selected mirror, then execute on the stated
local CPU, sequentially with other performance work:

```sh
.build/cngcompress-layout/constant-test
.build/cngcompress-layout/probe .build/decode-tables/corpus/packets.bin NEW.csv
python3 -B experiments/cngcompress_layout/verify.py
```

The packet runner refuses an existing CSV. Evidence pins both source revisions,
all packet rows, completion logs, compiler assembly and executable identities.
No external PivCo implementation is linked. The structural reference is the
[PivCo paper](https://marcinzukowski.github.io/pivco-huffman/paper-1.0/ph.html);
the portable source here is first-party experimental code.
