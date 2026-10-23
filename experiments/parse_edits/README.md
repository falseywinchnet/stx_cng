# Bounded short-match removal: measured negative result

Replacing a short match with literal bytes can reduce the exact Huffman packet
size, but this particular bounded search offers too little improvement to pursue
as the next production parser. It saves 9,073 bytes over 180,122,478 retained
entropy-frame bytes (0.00504%). These are entropy frames, not complete images.

The experiment reconstructs every retained entropy frame from the existing
222-image CNE corpus, reproduces its baseline bytes, and runs the original match
discovery unchanged. In each 128 KiB block it considers the first 64 matches with
lengths 8 through 32. Each alternative removes exactly one match, merges its
literal run with the following literal run, and inserts the actual source bytes.
No alternative changes the baseline index or discovers new references. Exact
Huffman packet counting includes the four stream boundaries, serialized model,
block prefix and raw fallback. One strictly smaller alternative may be selected
per block; a final whole-frame raw gate remains in place.

Every proposed token stream reconstructs the original block. The selected whole
frames are emitted and decoded through the existing decoder. This is an actual
packet experiment, not a sum of estimated symbol costs.

| Measurement | Result |
|---|---:|
| Retained entropy frames reproduced | 1,413 |
| Blocks inspected | 3,135 |
| Alternatives reconstructed and counted | 54,192 |
| Alternatives smaller than their baseline block | 11,148 |
| Blocks with a selected improvement | 1,366 |
| Frames with a size improvement | 848 |
| Baseline frame bytes | 180,122,478 |
| Selected frame bytes | 180,113,405 |
| Search plus per-alternative checks | 3.032 s |
| Baseline encoding measured once per frame | 0.516 s |

Times are one diagnostic execution on Apple A18 Pro with M4-built LLVM 23.1.2
Release code. The search includes candidate reconstruction and equality checks;
the baseline interval contains encoding only. The ratio is not a calibrated
production slowdown. It does show that the current search implementation cannot
be promoted on these measurements. No decoder-speed improvement is claimed.
Rejected image-level literal-compression attempts are absent from the retained
frame bundle. The experiment does not evaluate every possible match removal,
joint edits, added matches, or a globally optimal parse.

## A more useful follow-up measurement

`depth.cpp` compares the current length-limited Huffman payload against an ordinary
unrestricted Huffman lower bound on all 2,953 retained Huffman packets. It finds
423,692.5 bytes of aggregate payload penalty. This is an upper bound on recoverable
payload savings: unrestricted codes can exceed our 12-bit decoder limit, and it
does not include table cost or stream padding. It motivated the separate
[`length_limited`](../length_limited/README.md) experiment, which measures actual
packets and whole images using an exact bounded-depth construction.

## Reproduction and evidence

Compile `probe.cpp` and `depth.cpp` separately on the selected M4 host with
`/opt/homebrew/opt/llvm/bin/clang++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic
-Werror -DCNG_SHORT_STREAM=1 -DCNG_UNIFORM_PROOF=1 -DCNG_SPLIT_HISTOGRAM=1
-Iexperiments/entropy`. Copy the resulting executables to `.build/parse-edits`.

```sh
python3 -B experiments/parse_edits/extract.py --corpus .build/whole-codec/first-corpus --output .build/parse-edits/new-corpus
.build/parse-edits/probe .build/parse-edits/new-corpus/frames.bin .build/parse-edits/new-rows.csv .build/parse-edits/new-selected.bin
.build/parse-edits/depth .build/parser-review/corpus/packets.bin .build/parse-edits/new-depth.csv
python3 -B experiments/parse_edits/verify.py
```

The compressed evidence retains every per-block measurement, per-frame source
identity and depth measurement. Checksums pin the source, executables and retained
bundles. Native exactness checks are ordinary deterministic checks; no automated
memory integrity review was initiated. This negative result changes no published
codec or format and does not replace the faster baseline.
