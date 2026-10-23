# Prefix-width integers and separated controls

This experiment retains the current bounded matcher, eight-byte minimum match,
128 KiB block boundaries, frame history, copy kernels, Huffman construction and
compact length table. It changes only the byte-token grammar. All variants are
private experiments, not new public format options.

## Independent changes

Four builds compare the baseline, prefix integers alone, separated controls
alone, and both together. Baseline uses the original source and must reproduce
every retained corpus frame byte for byte. The other builds use a derived source
whose match-extension, candidate-search, index-update and copy-kernel fragments
are mechanically compared with the original by `verify.py`.

Prefix integers have the same widths as LEB128:

| Value range | First byte | Remaining bytes | Total payload bits |
|---|---|---:|---:|
| 0..127 | `0xxxxxxx` | 0 | 7 |
| 128..16383 | `10xxxxxx` | 1 | 14 |
| 16384..2097151 | `110xxxxx` | 2 | 21 |
| 2097152..268435455 | `1110xxxx` | 3 | 28 |

Low payload bits occupy the first byte; remaining payload bytes follow in little
endian order. Overlong forms are rejected. The final four-byte form covers the
few allowed frame values above 2 MiB; it is not borrowed as an implicit zxc format
extension. The implementation is first-party code applying the prefix-width idea.
`grammar_first.cpp` retains the first reader with a variable-width loop;
`grammar.cpp` selects explicit bounded reads for each width. The encoder bytes
are identical between these revisions.

The split layout begins with a four-byte offset to the literal section. Literal
counts, distances and match-length-minus-eight values occupy the control section
in their original event order. Literal bytes are gathered after the controls.
A final literal-only event remains permitted; exact output extent terminates the
parse. The entire arranged byte sequence still receives **one Huffman table**.
This isolates separation from separate entropy models and from a new parser.
Reordering can affect the four Huffman stream padding terms; the new offset and
prefix byte distribution can also change the fitted table.

The split encoder uses 787,488 bytes of declared workspace versus 656,400 for
baseline/prefix. Its temporary literal array adds one extra copy of the literal
bytes (a logical read and write per literal), and its parse adds four header
bytes per referenced block. All decoders retain the same 131,088-byte workspace.
These are declared storage and source-level copy counts, not measured peak
process memory or DRAM traffic.

## Measurement

Non-Apple LLVM 23.1.2 builds on the selected M4 host; Apple A18 Pro executes.
Each complete encode and decode is measured five times after warmup, in rotating
variant order; tables, parsing, raw rejection and reconstruction are included.
Allocation, verification and file I/O are outside timing. Timed output inverses
must be exact. The 1,413 retained frames contain 280,540,670 raw bytes; these are
retained CNG entropy frames, not complete images or discarded outer-encoder
attempts. Twenty additional controls exercise random data and periods 1, 7,
65,536 and 131,072, with/without sparse disturbances at 128 KiB and 256 KiB+1.

Initial-reader results, ratios to the baseline in the same run:

| Variant | Compressed bytes | Encode time | Decode time | Repeat decode time |
|---|---:|---:|---:|---:|
| Prefix only | 1.00003288 | 1.00614 | 0.997262 | 0.998237 |
| Split only | 1.00002660 | 1.00344 | 0.998126 | 0.999791 |
| Combined | 1.00005933 | 1.01203 | 0.994380 | 0.997243 |

Baseline emits 180,012,867 bytes; prefix emits 180,018,786; split 180,017,656;
combined 180,023,547. In the first run, respectively 662, 654 and 505 frames
have slower decoding. Initial control-set aggregate decoding regresses by
1.07%, 3.69% and 2.46%. Tiny aggregate differences are not sufficient evidence
to change the format. All individual rows and the repeat run are retained.

The initial arm64 assembly retains a loop and variable shift in multi-byte
prefix reads. The refined reader explicitly expresses all four bounded widths,
removing that loop while retaining the same wire grammar. Both arm64 and strict
baseline x86-64/SSE2 assembly are retained. x86-64 was inspected, not executed.

`integer-test` checks all 2,097,161 values from zero through the admitted maximum
input size, plus the maximum 28-bit value: 2,097,162 exact round trips and
LEB128-equivalent width checks. Complete-frame trials test both overlapping and
non-overlapping reconstruction through the retained workloads. No automated
memory integrity review was initiated.

Final explicit-width reader, again relative to its paired baseline:

| Variant | Compressed bytes | Encode time | Decode time | Slower frames |
|---|---:|---:|---:|---:|
| Prefix only | 1.00003288 | 1.01065 | 0.997014 | 579 |
| Split only | 1.00002660 | 1.00745 | 1.002310 | 608 |
| Combined | 1.00005933 | 1.01419 | 0.997532 | 418 |

All packet sizes are unchanged from the earlier reader. On the 20 controls,
final decode ratios are 0.999934, 1.037094 and 1.026587. No variant provides a
material enough complete-frame gain to promote here. Retain the current integer
and interleaving grammar. A tiny aggregate improvement is not a claim of a
reliable whole-image benefit, especially with added encode work and individual
regressions. Prefix-only uses no extra workspace; split variants do.

## Adjacent-query work sharing under our costs

The inspected zxc 0.15.0 source is pinned at
`2d29ab6fdf0e935781d287dd1ec2bf41d32a3df6`. Its
[level-six parser](https://github.com/hellobertrand/zxc/blob/2d29ab6fdf0e935781d287dd1ec2bf41d32a3df6/src/lib/zxc_compress.c)
uses static match-cost classes that ignore distance. At a covered adjacent
position, a preceding match already reaches the same endpoints. With
`dp[p] >= dp[p-1]`, most later transitions cannot win if their costs are equal;
only length-cost boundaries need special handling under those assumptions.

Our current grammar violates those cost assumptions even with a frozen Huffman
table. `cost_witness.cpp` constructs a complete valid table from populations
2,000 for byte zero and one for each other byte. It obtains one bit for zero and
nine bits for one. For zero literals and distance one, match length eight emits
`0,1,0` (11 bits); length nine emits `0,1,1` (19 bits). Both lengths use a one-byte
length field. At a common endpoint, equal predecessor costs would favor the
later length-eight transition by eight bits, despite crossing no integer-width
boundary. This is a concrete failure of the width-only cost premise, not a
fully instantiated source-input counterexample to zxc's own differently priced
parser. Its shortcut remains scoped to its stated costs.

A suitable fixed-table reuse test would need to check actual integer-byte code
costs, including distance, and every relevant cost discontinuity, rather than
just integer widths. Fitting the Huffman model to a changed parse additionally
changes global populations, table serialization, contiguous-stream boundaries
and padding. Whole-packet dominance requires those effects too. The existing
exact packet scorer remains the appropriate authority for final size.

The retained greedy matcher already skips the interior of each committed match;
it does not perform the adjacent DP queries that this zxc shortcut accelerates.
It also retains bounded recent candidates rather than an exhaustive match graph.
Adding a DP or changing candidate maintenance solely to adopt the shortcut would
change the matcher and the optimization problem. No such change is made here.
The reusable principle is to carry proven equality work where the same queries
already occur; no new globally bit-optimal parser is claimed.

## Reproduction

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/cngcompress_grammar/build.sh
```

After copying the completed arm64 binaries from the selected mirror, run
sequentially on the recorded execution CPU:

```sh
.build/cngcompress-grammar/integer-test
.build/cngcompress-grammar/cost-witness
.build/cngcompress-grammar/probe .build/decode-tables/frames/frames.bin NEW-frames.csv
.build/cngcompress-grammar/probe .build/zxc-comparison/controls/frames.bin NEW-controls.csv
python3 -B experiments/cngcompress_grammar/verify.py
```

The runner refuses existing output CSVs. Evidence includes source/binary hashes,
all rows and completion logs, both assembly revisions, corpus identities and the
control manifest. No zxc code is linked and no release source is changed.
