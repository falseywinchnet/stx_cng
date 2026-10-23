# Using the sibling's bounded-parser result

The sibling report supplies a useful continuation lemma and a real limitation
on histogram-only rANS pricing. It does not establish a fast parser or useful
production compression gains. Its suggested capped portfolio remains a research
proposal. This review preserves that distinction and verifies an exact size
calculation for the current Huffman-token candidate.

## What transfers

For a move-to-front recent-distance list of capacity three, two local parses
that rejoin the same semantic match sequence can change at most three later
distance codes. Those are first appearances among the next three distinct
positive distances, not necessarily the next three events. After those distinct
distances, the lists agree. Distances already encountered in the continuation
have identical front ranks in both lists. This proves the bound even with zero
placeholders in initially unfilled lists.

The correction to a distance histogram is therefore sparse. Fitted frequencies,
event counts, rANS lane assignments and coder evolution are not made local by
that result. It also does not remove the need to preserve future discovery state:
our sampled match index depends on which matches the baseline commits.

The report's exact-cost witness has 18 length-three, distance-one matches and
the same 2,035 zero literal bytes in both parses. Different literal-count orders
produce the same 2,089 reconstructed zero bytes, equal field histograms, fitted
tables, suffix-bit totals and final recent list, but 42 versus 44 control bytes.
`check.cpp` reproduces both packets with the current `match_events_bounded.cpp`;
the resulting binary file matches the sibling's native output byte for byte.
The inspected current event source/header also match the sibling's pinned copies.
This is a control-packet witness, not a complete-frame compression improvement.

The report's finite-state envelope is conservative. At 1,024 events its stated
absolute error bound is about 42.64 bytes; at the eight-byte baseline's maximum
16,385 events per 128 KiB block it is about 562.21 bytes. These values follow
`(64 + 3*N*log2(33/31))/8`. Small candidate differences still require exact
counting. No theorem here says that four exact alternatives plus a 64-proposal
search will outperform the current encoder.

The report preserves its own adverse toy results: its gated portfolio found zero
static-model size wins and two adaptive-model size wins on the tested tiny inputs.
Those checks establish tested correctness, not useful gains on CNG images. We
have not rerun the entire sibling toy audit or implemented its proposed portfolio.

## A simpler exact cost for the current candidate

The current engineering candidate uses Huffman-coded byte tokens, not the rANS
event representation. Its size depends on code lengths and stream padding, not
on a running arithmetic coder state. Let `n[j,s]` be the number of byte symbol
`s` in contiguous Huffman stream `j`, and fit the existing lengths `l[s]` from
the combined populations using the exact existing tree rules. If `T(l)` is the
serialized length-table size and `q` is the number of streams, then:

    coded_size = 5 + T(l) + 4*(q - 1)
                 + sum_j ceil(sum_s n[j,s]*l[s] / 8)

For nondegenerate input the returned packet size is the smaller of this value
and `input_size + 5`. Empty input is five bytes; single-symbol input is six.
The fixed rule is one stream for at most 1,024 input bytes, four otherwise.

The count uses the actual fitted lengths, including the existing bounded-depth
smoothing and symbol tie rules. It serializes the small length table to obtain
its exact size. It does not construct canonical codes, emit a coded body, or
simulate rANS states. Per-stream populations are necessary: one global histogram
does not by itself resolve the four separate byte-rounding terms.

`check.cpp` compares this formula against actual encoding on 432 deterministic
inputs, including empty/constant data, nine alphabet sizes, three distributions,
the short-stream boundary, block boundaries and maximum admitted input size.
It also checks every retained Huffman packet extracted from the existing private
CNE corpus: **2,953 packets from all 222 hash-verified images**. All counts match.
The extractor verifies image chunk CRCs and expected file hashes before retaining
packets. The native check decodes each packet before counting its original symbols.

This is exact-size evidence, not a speed benchmark or a new parser. The diagnostic
still scans its input and fits a tree for each count. It establishes a foundation
for evaluating bounded token edits from updated populations, with stream-boundary
changes included. Outer token headers, block raw fallback, frame raw fallback and
image literal fallback must also be included before selecting an actual parse.

## Next experiment

Keep the report's MTF lemma for the separate event representation. For the current
Huffman-token baseline, first use this exact count to price a small number of
already-discovered alternative tokenizations. Preserve baseline discovery state;
do not assume a chosen edit leaves later sampled-index contents unchanged.
Account for every changed byte and any movement across the four stream boundaries.
Refit lengths and count the complete packet before interpreting a size advantage.

Measure candidate-discovery, model-fitting and count work before adding a wider
search. A count-only evaluator can still be too expensive. Decoder work remains
a separate measured objective; neither this formula nor the sibling's lemma
supplies calibrated per-event cycle costs. The earlier 1 MiB auxiliary-workspace
proposal remains a proposed target, not a newly approved total-codec limit.

## Evidence and reproduction

The original sibling report is at the owner-provided local path
`/Users/ultimussecundai/Documents/Codex/2026-10-18/lo/outputs/cng-parser-study/REPORT.md`.
`checks.json` pins its hash, the native witness, current sources and diagnostic
binary. `counterexample.json` preserves the sibling's witness record;
`corpus_manifest.json` records each admitted image and extracted packet count.
The bundle and native output remain under `.build/parser-review`.

From the repository root:

```sh
python3 -B experiments/parser_review/extract.py --corpus .build/whole-codec/first-corpus --output .build/parser-review/new-corpus
/Users/ultimussecundai/.local/bin/m4build -- sh -c 'mkdir -p .build/parser-review && /opt/homebrew/opt/llvm/bin/clang++ -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic -Werror -DCNG_SHORT_STREAM=1 -Iexperiments/entropy experiments/parser_review/check.cpp experiments/entropy/match_events_bounded.cpp -o .build/parser-review/check'
```

Copy the built executable from the selected M4 mirror for local execution, then:

```sh
.build/parser-review/check .build/parser-review/order-native.bin .build/parser-review/new-corpus/packets.bin
python3 -B experiments/parser_review/verify.py
python3 -B tools/check_evidence.py
```

These are deterministic algorithm checks. No automated memory integrity review
was initiated, and no production codec or published format changed.
