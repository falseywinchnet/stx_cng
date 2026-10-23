# Packet-cost Huffman polishing

The sibling's bounded two-pass construction is mathematically useful, but this
corpus does not justify adding its search to the encoder. The house-style rewrite
saves 1,827 bytes from 167,696,124 compressed packet bytes (0.001089%). Its search
alone takes 0.038229 s against 0.373223 s of baseline packet encoding, before
collecting the actual lane counts. This is a retained negative implementation
experiment, not a shipped codec change.

The exact byte objective and fixed-support lower bound remain useful for evaluating
future parser changes. A small encode cost is acceptable for a meaningful decode
or size improvement; this particular search has not demonstrated either at a
useful scale. The independent grouped-reader candidate is recorded separately in
`../decode_tables/README.md`.

## Algorithm reviewed and rewritten

The sibling report is `REPORT.md` in the owner's
`Documents/Codex/2026-10-18/lo/outputs/huffman-packet-study` directory. Its report
and prototype hashes are recorded in `evidence/checks.json`. The implementation
here rewrites the prototype with explicit types, named procedures and visible
lifetimes. It does not run a learning model, change the grammar, or add a coding
option. The following reasoning is independently checked against our serializer:

- The objective is `ceil(table_nibbles/2) + sum(ceil(lane_bits/8))`, plus fixed
  headers. Actual contiguous coding lanes determine counts.
- When weighted payload bits decrease, four separate lane roundings can increase
  payload bytes by at most three. A larger packet regression necessarily involves
  increased table bytes.
- An edit visits the changed run and its neighbors. At most three edited symbols
  therefore need at most nine old runs. Merging overlapping windows avoids double
  counting; unchanged neighboring runs prevent merges beyond the window.
- Consecutive active-symbol swaps preserve Kraft equality. A triple with lengths
  `(h,k,k)` may become `(h+1,h+1,k-1)` in each distinct placement. These preserve
  Kraft and the admitted 1..12 bound. The best strict improvement is accepted once
  per pass; ties keep the incumbent.
- At most `4*S-7` proposals occur per pass. Two passes at 256 symbols admit at
  most 2,034 proposals. Exhausting two passes is not proof of local optimality.
- Replacing each positive length by one gives a relaxed support-mask table lower
  bound. Adding the rounded payload optimum gives a lower bound on packet bytes.
  Equality certifies optimality under fixed support; the general gap bound does
  not imply that the optimum is attainable by this neighborhood.

Only the current exact payload-optimal lengths seed the search. Regenerating the
older rounded constructor is timed outside the search and used for comparison;
it is not silently retained as a second incumbent. Thus improvements cannot make
the current packets larger but need not remove every older-constructor regression.

The map occupies 2,056 bytes, four uint32 lane histograms 4,096 bytes, a patch
16 bytes, and the active-symbol array 512 bytes. Additional bounded bit totals and
patch temporaries exist. These are object sizes, not a measured stack high-water
mark. No search allocation occurs. The 256-symbol and four-lane dimensions are
format bounds; counts sum to at most the admitted 2 MiB input.

## Measured corpus result

The extractor supplies 2,953 packets from the hash-verified 222-image exact-length
candidate. Of these, 2,847 use Huffman coding; 106 raw or singleton packets are
reproduced and left outside length optimization. All candidate lengths are
serialized into the real grammar and decoded with the existing decoder.

| Passes | Bytes saved | Improved packets | Remaining regressions vs old constructor | Proposals | Accepted edits | Search time |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | 92 | 0 | 0 | 0.001539 s |
| 1 | 1,411 | 1,033 | 68 | 1,212,594 | 1,033 | 0.027804 s |
| 2 | 1,827 | 1,033 | 61 | 1,715,516 | 1,409 | 0.038229 s |

The zero-pass time includes computing the initial score; it is diagnostic work
not present in the baseline encoder. One and two pass timings include that score,
run-map construction, proposals and commits, but exclude histogram collection,
initial length construction, output emission and inverse checks. Each search and
baseline encoding time is the median of seven calls, summed over the 2,847 coded
packets. The baseline encoder reproduces every retained packet byte for byte.
The baseline encode total is 0.373223 s. The separately measured, single-trial
lane-counting pass totals 0.133176 s. These are not paired whole-image timings;
adding their ratios is not a production slowdown estimate. Counts might be
collected in an existing pass, but that benefit has not been implemented or timed.

Two passes leave 2,471 packets at a demonstrated local optimum in this neighborhood;
376 exhaust the budget after an improvement without such a certificate. Only 17
packets attain the global lower bound, unchanged from zero passes. The maximum
saving is six bytes per packet. Smooth saves five packet bytes, rectangle-on-gradient
two, kodim01 five and large smooth RGBA 37. Those are packet savings, not new whole
image measurements; they do not resolve the structured-image compression gap.

## Checks and reproduction

The rewritten sparse scorer matches the native serializer on 30,000 deterministic
patch cases. Generated runs include initial zero runs and run-length thresholds;
edited positions are sorted and distinct. The corpus checks exact native score,
proposal bounds, fixed support, canonical-code completeness, nonincrease, lower
bounds and exact reconstruction for zero, one and two passes. The sibling's own
exhaustive-oracle tests are separate evidence; their counts are not claimed here.

Build on the selected M4 using non-Apple LLVM 23.1.2, C++20, `-O3 -DNDEBUG`.
Copy the completed binary back for Apple A18 Pro execution, then run sequentially:

```sh
/Users/ultimussecundai/.local/bin/m4build -- sh experiments/packet_polish/build.sh
.build/packet-polish/probe .build/decode-tables/corpus/packets.bin .build/packet-polish/new-packets.csv
python3 -B experiments/packet_polish/verify.py
```

The source bundle is extracted by `../decode_tables/extract.py`; its source image
hashes and packet bundle hash are pinned in the retained evidence. Use a fresh CSV
path to preserve results. No automated memory integrity review was initiated.
