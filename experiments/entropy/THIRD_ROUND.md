# Repetition, match cost and the remaining representation gap

The in-house backend is still **not ready to replace Zstandard**. A cheap
matcher now recovers most of the severe rectangle regression and retains faster
entropy encoding with decoding near the control on the larger groups. But too
many individual images grow, including smooth images. Aggregate improvements do
not satisfy the owner's requirement.

The most useful new result is a diagnostic replay. Feeding the reference's
long-match choices into our existing packet grammar barely changes the largest
smooth-image sequence payload: 29,020 bytes becomes 28,918, versus Zstandard's
17,581. For this case, better selection of long matches alone is not enough.
Short matches and the representation of lengths, distances and literals remain
the next targets. This is conditional evidence from an informational generated
parse, not a proof that match search cannot improve.

The public codec, format, release and dependency remain unchanged. All ten
formulations below are independent experiment executables, not user-facing
codec options. No reference match finder is used by any candidate benchmark.

## Construction and measured alternatives

The common construction uses a 64 KiB index holding one candidate per short-word
hash, exact comparison, scalar 64-bit extension, greedy copies, 128 KiB entropy
blocks and the prior four-stream paired Huffman kernel. It verifies equality
before accepting references. An overlapping copy is reconstructed by seeding
one period and doubling already reconstructed material with disjoint copies.
Constant-distance-one copies use a fill. There are no platform intrinsics.

- `match`: six-byte minimum, four-byte hash, every position queried, index reset
  per block, maximum distance 65,535. Variable literal counts and match lengths
  surround literal bytes and a two-byte distance in one Huffman-coded stream.
- `sampled`: after consecutive failures, query spacing increases as
  `1 + (misses >> 6)`; a match resets the counter. This is the same rule on all
  inputs, not an image classification. It reduces unsuccessful search work.
- `eight`: require eight matching bytes before extension. This removes many
  expensive short descriptors and restores encoding speed.
- `plain`: if parsing finds no references, encode the original block directly
  with Huffman. Avoid assembling and reconstructing a redundant literal stream.
- `split_match`: separate literals and controls into two Huffman packets. The
  extra model/framing cost outweighs some of the statistical separation.
- `history`: retain the index across the raw frame, admit full-frame distances
  with variable-length integers, and retain the `plain` escape.
- `word`: hash eight bytes instead of four, otherwise retain `history`.
- `repeat`: retain two recent distances, check them directly near a successful
  match boundary, and code their reuse as zero or one. It takes the first exact
  recent-distance candidate, not the longest among all candidates. Roll back
  distance state when a parsed block is ultimately stored raw.
- `compact`: return to a four-byte hash and four-byte minimum. Pack small
  literal counts and match lengths into the two nibbles of one byte, with
  variable-length suffixes. Accept only matches longer than their complete
  descriptor. This criterion saves source bytes but does not price entropy.
- `priced`: store compact controls verbatim and Huffman-code literals
  separately. Estimate a literal symbol's cost as `max(1, floor(log2(N/count)))`
  for nonconstant blocks; a constant block takes the ordinary constant escape.
  Reject a reference unless its estimated literal cost exceeds its exact control
  byte cost. Histogram construction is included in encoding time. The final
  literal distribution changes after parsing, so this is an estimate, not an
  optimal parse or a guarantee that a selected reference saves encoded bits.

## Complete retained-entropy measurements

Each cell below is **file-size ratio / encode-time ratio / decode-time ratio**,
candidate divided by Zstandard; lower is better. All framing, model and stored
escape bytes are included. Sizes apply the existing outer stored-literal escape.
The file sizes are replacement accounting, not emitted alternative CNG files.

| Variant | General size / encode / decode | RGBA size / encode / decode | Composite size / encode / decode |
|---|---:|---:|---:|
| match | 1.001248 / 1.886 / 1.094 | 1.000160 / 1.848 / 1.123 | 1.003765 / 1.861 / 0.904 |
| sampled | 0.998524 / 1.167 / 1.047 | 0.996931 / 1.158 / 1.076 | 1.004625 / 1.102 / 0.803 |
| eight | 0.994335 / 0.752 / 0.995 | 0.992017 / 0.741 / 1.013 | 1.007298 / 0.887 / 0.792 |
| plain | 0.994291 / 0.757 / 0.989 | 0.991962 / 0.725 / 0.996 | 1.007257 / 0.859 / 0.733 |
| split_match | 0.994527 / 0.754 / 0.984 | 0.992177 / 0.741 / 0.995 | 1.008907 / 0.868 / 0.811 |
| history | 0.994208 / 0.758 / 0.983 | 0.991905 / 0.757 / 1.003 | 1.006823 / 0.829 / 0.770 |
| word | 0.995490 / 0.779 / 1.017 | 0.993172 / 0.785 / 1.046 | 1.008688 / 0.784 / 0.892 |
| repeat | 0.995445 / 0.806 / 1.009 | 0.993133 / 0.792 / 1.032 | 1.008814 / 0.755 / 0.860 |
| compact | 1.038138 / 1.098 / 1.648 | 1.042746 / 1.082 / 1.623 | 1.019140 / 0.888 / 0.962 |
| priced | 0.994452 / 1.029 / 0.964 | 0.991856 / 0.949 / 0.998 | 1.008941 / 1.560 / 0.854 |

Execution was on Apple A18 Pro; builds used the M4 host with non-Apple LLVM
23.1.2 in Release mode. The reference is Zstandard 1.5.7, level 3. Each frame
has a warm-up and seven paired trials with alternating order. Ratios sum
per-frame medians; these are individual experiment runs, not confidence bounds.
Task-owned performance runs were serialized on the execution host.

The corpus contains 131 general files, 80 RGBA files and 11 composite image
bodies, totaling 222 files and 1,410 retained entropy frames. The RGBA set
contains derivatives of 64 photographs. Composite rows do not measure opening
collections. Frozen files omit failed compression attempts on stored-literal
bands; these timings therefore do not establish complete image operation times
or the cost of encoding random images. Allocation is outside the timed coder;
parsing, table construction, entropy coding and restoration are inside.

`history` is a useful baseline for the next experiment: it takes about 76% of
the control's encoding time, with decoding near parity on the general and RGBA
groups. However, only 38/131 general files, 44/80 RGBA files and 3/11 composite
bodies become smaller. The compact-token experiment demonstrates why merely
lowering the match-length threshold is insufficient. Pricing and separating
those controls recovers the aggregate size loss, but loses the encoding-time
advantage and does not solve smooth images. Because `priced` changes both the
cost test and the representation, its result does not isolate those changes.

## Ordered-image failures remain decisive

| Image | Current CNG bytes | `history` accounted bytes | `priced` accounted bytes |
|---|---:|---:|---:|
| `rectangle_on_gradient` | 383 | 481 | 558 |
| `smooth` | 11,712 | 14,045 | 14,066 |
| `large_smooth_rgba` | 21,527 | 34,876 | 38,744 |
| `composite_control_states` | 1,083 | 1,573 | 1,521 |

The prior entropy-only rectangle result was 8,465 bytes. General repetition
matching closes much of that gap, but a remaining 26% increase still matters.
The largest smooth case also exposes an expensive sequence representation:
5,105,736 raw sequence bytes become 29,020 with `history`, versus 17,581 with
Zstandard. Its small literal payload grows from 3,473 to 5,383 bytes as well.

## Reference diagnostics and replay

`match_diagnostic.cpp` uses Zstandard's explicitly informational
`ZSTD_generateSequences` API at level 3. These are generated parses, **not**
traces extracted from the frozen compressed packets. Every generated reference
is checked byte for byte and covers the source exactly. Ordinary level-3
recompression separately reproduces the original frozen packets byte for byte.
The deprecated API is used only in the diagnostic executable.

For `smooth`, 7,647 of 15,253 bytes copied by its generated literal parse belong
to matches shorter than eight. For `large_smooth_rgba`, generated sequence
parses copy 1,271,988 bytes from distances beyond 65,535 and mark 2,568,360 bytes
as using a repeat distance. These categories can overlap; they are not additive.
Longer history and recent-distance trials were motivated by those observations.
They did not close the gap.

`match_replay.cpp` then clips the same generated references to our 128 KiB block
boundaries, turns pieces shorter than eight into literals, and emits our exact
`history` mode-16 grammar. It uses the same Huffman kernel, plain/raw escapes
and ordinary mode-16 decoder. All replay packets reconstruct exactly.

The following are **entropy packet bytes**, without the outer file accounting:

| Image / stream | Zstandard | Our `history` parse | Generated-reference replay |
|---|---:|---:|---:|
| `smooth` sequences | 4,420 | 5,026 | 4,937 |
| `smooth` literals | 7,219 | 8,946 | 8,755 |
| `rectangle_on_gradient` sequences | 61 | 87 | 76 |
| `rectangle_on_gradient` literals | 249 | 321 | 329 |
| `large_smooth_rgba` sequences | 17,581 | 29,020 | 28,918 |
| `large_smooth_rgba` literals | 3,473 | 5,383 | 4,986 |
| `composite_control_states` sequences | 715 | 1,077 | 986 |
| `composite_control_states` literals | 295 | 423 | 408 |
| `kodim01` literals | 560,732 | 556,128 | 556,503 |

This keeps the coding scheme fixed while changing the admitted long-match
parse. It does not provide the best parse for our coding costs, and dropping
short matches does not recreate the parse Zstandard would have chosen if its
own minimum were eight. It nevertheless narrows the next experiment: retain
cheap search as a baseline and investigate efficient event coding, including
short-match descriptors, rather than assume a larger search index is the cure.
A replay retaining the reference's short matches can further separate the
minimum-length restriction from descriptor costs before a new search algorithm
is implemented. No replay timing is an in-house encoder-speed result.

## Storage, exactness and reproduction

The ordinary matcher owns 459,792 bytes of caller-supplied encoder workspace
and 131,088 bytes of decoder workspace. Split/priced variants use 327,696 and
262,160 bytes respectively. These figures exclude entropy-kernel stack tables,
input, output and harness buffers; they are not total codec peak-memory claims.
The admitted raw frame is at most 2,097,160 bytes, and encoder output capacity
is at least twice input size plus 1,024. Workspaces are allocated before timing,
reused, and disjoint from input/output. No matcher allocates per element.

The first seven variants pass 432 deterministic native round trips each and
414 independent scalar inverse checks each. Repeat, compact and priced pass
433 and 415 respectively, including an additional stored-first-block followed
by repeated-data case. The fixture's first block is stored in the repeat trial;
this is not an exhaustive state-transition proof. Sizes include zero, tiny,
65,535/65,536, 128 KiB, 256 KiB and maximum-input boundaries, with random,
periodic and disturbed-periodic populations. Every full corpus trial decodes
all retained entropy frames exactly. The independent inverse uses bit-by-bit
Huffman decoding and byte-at-a-time references.

These are valid-input algorithm checks, not an automated memory integrity
review. No additional review was initiated. Public-source evidence remains
unchanged and still verifies all 222 records.

Build through the root `AGENTS.md` commands, then copy completed executables
from the M4 mirror for local arm64 measurements. Example commands:

```sh
.build/entropy/cng_entropy_checks_history .build/entropy/history_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/entropy/history_vectors.bin
.build/entropy/cng_entropy_checks_priced .build/entropy/priced_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/entropy/priced_vectors.bin 415
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_history \
  --output .build/entropy/new-history-results.json
python3 -B experiments/entropy/replay_matches.py --corpus /path/to/cng-research \
  --output .build/entropy/new-replay.json
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

`evidence/results_*.json.gz` retains timings, sizes, inputs and source/binary
identities for every alternative, including regressions. Legacy `rans_*` timing
fields name the candidate operation, even for Huffman/matching experiments.
The two uncompressed diagnostic JSON reports retain their own source identities.
Source snapshots preserve prior measured harnesses. No external images are
redistributed. Nothing here establishes a successful whole-image replacement;
that still requires emitted images, exact pixels, end-to-end timing, noise
encoding work and peak workspace measurements.
