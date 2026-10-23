# Independent event parsing and statistical block size

The event coder now runs with our own matcher. It does not satisfy the replacement
goal. The first three-byte greedy formulation substantially worsens both size
and execution time. Returning to the eight-byte matcher restores useful speed;
a fixed separation between event and literal block sizes improves the largest
smooth case, but the existing Huffman-token baseline still has a better overall
size distribution and a substantially smaller workspace.

The production codec and release remain unchanged. These are separate research
executables, not selectable production models.

## Independent constructions

All new candidates use the compact Huffman tables and the packed, three-distance
integer-event coder from the fourth round. Input remains the frozen raw entropy
streams, including CNG sequence byte planes. Event records describe this inner
byte matching; they are not the original CNG prediction/copy records.

- `event_backend`: a sampled single-candidate index hashes three bytes and
  accepts exact matches of at least three bytes. It extends through scalar
  64-bit comparisons, copies unmatched literals into reusable storage, and
  sends literal counts, lengths and distances directly to the event coder.
  A final unmatched three-byte tail remains literal in this first formulation.
- `event_eight`: the earlier four-byte-hash/eight-byte-minimum matcher, with the
  same event/literal representation. This changes both the lookup key and
  admission rule; the comparison is not a one-factor proof about minimum match
  length alone.
- `event_small`: the same eight-byte construction, with its record arrays sized
  for that minimum. Its packets have a distinct experimental mode identifier.
  Native vectors differ only in that mode byte; all corpus frame sizes agree.
- `event_large`: the same construction with fixed 256 KiB blocks instead of
  128 KiB, reducing the number of event probability tables. Literal probability
  tables initially cover those larger blocks too.
- `event_partitioned`: retain 256 KiB event blocks, but Huffman-code literals in
  fixed 128 KiB pieces. A literal sequence no larger than 128 KiB uses one ordinary
  Huffman packet; a longer sequence uses the existing blocked packet grammar.
  This boundary is determined by length, without trying several models.

Every candidate is independent of Zstandard's match finder. Zstandard is linked
only by the corpus probe as a reference encoder/decoder. There is no trained
model or image classification.

## Measured results

Each cell is **accounted size / encode time / decode time**, divided by the
Zstandard control. Lower is better.

| Candidate | General | RGBA | Composite bodies |
|---|---:|---:|---:|
| Prior compact-table Huffman baseline | 0.993955 / 0.783 / 0.988 | 0.991570 / 0.763 / 1.001 | 1.005987 / 0.819 / 0.731 |
| Three-byte event matcher | 1.053827 / 2.300 / 5.153 | 1.065005 / 2.308 / 5.105 | 1.041021 / 2.534 / 4.090 |
| Eight-byte event matcher, original workspace | 0.994305 / 0.789 / 1.002 | 0.991957 / 0.753 / 1.026 | 1.008417 / 0.855 / 0.780 |
| Eight-byte event matcher, smaller workspace | 0.994305 / 0.779 / 0.995 | 0.991957 / 0.762 / 1.027 | 1.008417 / 0.922 / 0.848 |
| 256 KiB events and literals | 1.001768 / 0.740 / 1.025 | 1.010294 / 0.698 / 1.064 | 1.009283 / 0.830 / 0.770 |
| 256 KiB events, 128 KiB literal pieces | 0.994412 / 0.768 / 0.996 | 0.992053 / 0.743 / 1.004 | 1.008067 / 0.905 / 0.919 |

These are individual paired-trial runs on Apple A18 Pro, using M4-built Release
binaries from non-Apple LLVM 23.1.2. The reference is Zstandard 1.5.7, level 3.
Each frame has a warm-up and seven paired trials with alternating order. The
ratios sum per-frame medians; differences between candidate runs are not
confidence intervals. Task-owned timing runs were serialized on the execution
host. M4 builds may proceed while the different local CPU runs a benchmark.

The 222-file corpus comprises 131 general images, 80 RGBA images and 11 composite
bodies, with 1,410 retained entropy frames. The RGBA group includes derivatives
of 64 photographs. Sizes include candidate headers, models, states, padding and
stored escapes, followed by the existing outer literal fallback. They are file
replacement accounting, not emitted candidate CNG files. Timings include search,
parsing, entropy coding, table construction and reconstruction, but exclude
allocation and full image operations. Frozen files omit failed compression
attempts on stored-literal bands; random-image encoder work remains unmeasured.

The three-byte greedy formulation improves only 5/131 general files and no RGBA
files. The smaller eight-byte formulation improves 33/131, 44/80 and 3/11. The
partitioned formulation improves 33/131, 45/80 and 3/11. Those distributions do
not meet the requirement that regressions be rare.

## Ordered images and the block-size experiment

| Image | Current CNG | Prior Huffman baseline | Eight-byte events | Partitioned events/literals |
|---|---:|---:|---:|---:|
| `smooth` | 11,712 | 14,025 | 13,948 | 13,948 |
| `rectangle_on_gradient` | 383 | 481 | 592 | 592 |
| `large_smooth_rgba` | 21,527 | 33,928 | 32,017 | 29,853 |
| `composite_control_states` | 1,083 | 1,476 | 1,597 | 1,597 |

Larger blocks help the large smooth case, but using the same enlarged extent
for literal frequency tables hurts aggregate compression. Returning literals
to 128 KiB pieces recovers most of that loss while retaining the smooth-case
improvement. This supports treating event-model and literal-model extents as
separate fixed design decisions. It does not justify selecting different
algorithms for individual images or increasing block size indefinitely.

The event representation is not inherently tied to the first trial's fivefold
decode-time regression: the eight-byte path uses the same coder and returns
near parity. The particular three-byte search/admission policy produces a poor
representation. Detailed event counts and cost-aware admission, rather than
another unconditional lowering of the minimum, are the next parser questions.

## Buffer bounds and measured caller storage

The event coder's normalized alphabet has at most 88 symbols per field. Each
integer contributes at most 19 suffix bits. For N events, suffix storage is at
most `ceil(57*N/8)` bytes, and the symbol array requires `3*N` bytes. Therefore
`11*N + 64` scratch bytes suffice.

The rANS loop emits at most one 16-bit word per symbol: at most `6*N` payload
bytes for three fields. Packed models occupy at most 560 bytes, with 25 bytes
for the mode/count, suffix length and four states. Including suffix rounding,
`14*N + 1024` output bytes suffice. These replace the earlier conservative
`24*N + 2048` output and `24*N + 64` scratch requirements. Tight-capacity tests
emit byte-identical vectors to the previous three-distance coder.

Compiler `sizeof` measurements on the arm64 build are:

| Workspace | Encoder bytes | Decoder bytes |
|---|---:|---:|
| Prior Huffman-token baseline | 459,792 | 131,088 |
| Three-byte event prototype | 2,076,920 | 655,364 |
| Eight-byte events, 128 KiB blocks | 1,066,600 | 327,692 |
| Eight-byte events, 256 KiB blocks | 2,066,024 | 655,372 |
| Partitioned events/literals | 2,066,536 | 655,372 |

These are caller workspaces only, excluding coder stack tables, input/output,
reference contexts and harness buffers. The reduced workspace variant halves
the prototype's encoder storage while preserving its coded sizes. Nevertheless,
the prior baseline remains substantially smaller. No total codec peak-memory
claim follows from these figures. Allocation and initialization remain outside
the reported operation timings and must be included in later full-codec work.

## Verification and provenance

Each of the five independent matching variants passes 432 deterministic native
round trips and 414 independent inverses. The inverse uses independently decoded
integer events, bit-by-bit Huffman reconstruction and byte-at-a-time references.
All five complete corpus runs reconstruct every retained entropy frame exactly
and reproduce the original Zstandard frame bytes with the reference encoder.

The bounded event coder passes 60 native and 60 independent cases using its
smaller output/scratch capacities. Its emitted vectors equal the previous recent-
distance coder's vectors exactly. The compact-workspace matcher vectors equal
the original eight-byte matcher's vectors after normalizing the outer mode byte.
All 222 corpus file sizes and 1,410 frame sizes agree between those two variants.

`evidence/results_event_*.json.gz` retains all timing samples and source, binary
and input identities. `checks_fifth.json` records the checks and their source
snapshots; `workspaces_fifth.json` records compiler-measured storage. The verifier
checks complete runs, source identities and compact-workspace size equivalence.
Source snapshots preserve previous measured versions as harnesses evolve.
These are valid-input checks, not an automated memory integrity review. No new
review was initiated. Published source hashes and release evidence stay intact.

Build using the root `AGENTS.md` M4 commands. After copying completed binaries:

```sh
.build/entropy/cng_match_event_bounded_checks .build/entropy/event_bounded_vectors.bin
python3 -B experiments/entropy/match_event_oracle.py .build/entropy/event_bounded_vectors.bin
.build/entropy/cng_entropy_checks_event_partitioned .build/entropy/event_partitioned_vectors.bin
python3 -B experiments/entropy/event_backend_oracle.py .build/entropy/event_partitioned_vectors.bin
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_event_partitioned \
  --output .build/entropy/new-event-partitioned.json
python3 -B experiments/entropy/verify_backend_evidence.py
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

## Algorithmic question and next work

Ferragina, Nitto and Venturini formulate bit-optimal LZ parsing as a weighted
shortest-path problem and prune it under an increasing integer-code cost
assumption. That result is useful context, but it does not directly establish
optimality for our coder: event probabilities depend on the chosen parse and
recent-distance costs depend on the path. See
[Bit-Optimal Lempel-Ziv compression](https://arxiv.org/html/0802.0835v1).

The owner has been asked to take a formal bounded-work parsing question to the
separate investigation: derive cheap dominance rules or a bounded approximation
for actual coded bits and decoder work, including literal runs, overlapping
matches and three recent distances, without an unbounded search graph.
Implementation work does not depend on that answer arriving immediately.

Retain the compact-table Huffman-token construction as the independent baseline.
Before adding parser machinery, record the event counts and length distributions
that explain the three-byte trial's excess work, then measure a bounded,
cost-aware admission or lookahead rule. Preserve the partitioned event variant
as evidence that model overhead can be reduced without adopting larger literal
models. It is not the current winner over the full corpus.

The original objective still requires better per-image results, actual candidate
CNG files, exact reconstructed pixels, complete encode/decode measurements,
random-input attempt costs and peak memory. None of those requirements is waived
by an aggregate entropy-speed improvement.
