# Match selection: bit prices, event counts and wider keys

The compact-table Huffman-token baseline remains the strongest overall independent
candidate. None of this round's four changes satisfies the replacement goal.
A bounded pruning pass removes some short references but costs too much and leaves
far too many events. Eight-byte hash keys improve the event coder's aggregate
size, but lose decoding speed. Moving that key change to the token coder worsens
its aggregate size and speed. Keep these as research evidence, not release modes.

## Bounded pruning

`match_event_pruned.cpp` retains the three-byte greedy search from the fifth
round, then compacts its event array before coding. It does not search again.
It prices literals using the bounded Huffman code lengths for the raw block and
prices each integer event field from the first parse's normalized 1,024-slot
histogram. The three recent distances follow the accepted events, not rejected
ones. A new literal-count bucket receives a conservative one-slot price.

For each match, the estimate adds the pending literal-count, match-length and
distance prices, then subtracts the price of the literal count that would result
from rejecting that match. It compares this marginal control estimate with the
Huffman price of the matched bytes. A minimum code-length bound avoids scanning
long profitable matches; otherwise the matched bytes are inspected. Initial
matches are disjoint, so this extra inspection is bounded by the block length.
Accepted literals are copied once into the existing workspace. The final event
and literal models are rebuilt from the resulting parse.

This is a deterministic heuristic, not an optimality proof. It omits future
literal-run changes, probability changes, model-description costs and decoding
work. In particular, references that look inexpensive in bits can still create
many short copies. Post-processing cannot recover longer candidates skipped by
the original greedy parse.

The Q8 logarithm uses integer normalization and eight squarings. The diagnostic
checks every normalized population from 1 through 1,024 against
`floor(256*log2(population))`; all agree. Empty and constant raw blocks use the
existing literal coder. There are no new bulk buffers or runtime allocations.
Caller storage remains 2,076,920 encoder bytes and 655,364 decoder bytes, excluding
small stack tables and input/output storage.

## The event-count result

`pruning_diagnostic.cpp` checks every generated reference against raw input and
accounts for every byte. Its implementation inclusion is diagnostic-only; it
exposes private parser functions without adding instrumentation to timed kernels.
Each frame begins with a fresh index, and history persists across its blocks.

| Image | Three-byte greedy events | Events after pruning | Eight-byte matcher events |
|---|---:|---:|---:|
| `smooth` | 6,450 | 6,330 | 419 |
| `mixed_order_noise` | 3,830 | 3,817 | 243 |
| `kodim01` | 114,718 | 91,696 | 123 |
| `rectangle_on_gradient` | 98 | 94 | 60 |
| `large_smooth_rgba` | 10,672 | 10,529 | 8,239 |
| `composite_control_states` | 498 | 421 | 139 |

Counts include terminal literal events. On `kodim01`, the initial parse has
104,860 matches of exactly three bytes; pruning retains 83,034 of those. The
eight-byte parse has 122 matches in total. The difference explains a substantial
source of repeated event and copy work; it is not a proof that short matches are
always undesirable. The two parsers also differ in hash key and query schedule.
Detailed counts for lengths 3, 4–7, 8–15, 16–31, 32–127 and 128+ are retained.

## Wider-key experiments

`match_event_wide.cpp` hashes all eight bytes required by its match rule. A fixed
64-bit multiplicative hash replaces the four-byte hash; the 16,384-entry index,
sampling rule and minimum match length remain. Post-match insertion now requires
an eight-byte key to remain in the frame. This changes both collision behavior
and the eligible final insertions. It is a concrete algorithm comparison, not
an isolated measurement of key width alone.

The separate `event_lookahead` executable examines one candidate starting one
byte later after finding a match. It accepts the alternative only when its
length exceeds the current length by more than one, then commits immediately.
It never recursively defers a decision. The two extension scans have bounded
work relative to the committed span. Querying/inserting the extra position also
changes the subsequent index. There is no bit-cost or distance-cost guarantee.

`match_wide.cpp` applies the same wider key to the earlier combined token/literal
Huffman representation. It retains that representation's 459,792-byte encoder
workspace and 131,088-byte decoder workspace. The event variants retain the
larger original event workspaces to keep the comparison focused on parsing.
All packet grammars are existing private experiment grammars; no production
format change or runtime model menu is introduced.

## Complete retained-frame measurements

Each cell is **accounted file size / encode time / decode time**, divided by the
Zstandard control. Lower is better.

| Candidate | General | RGBA | Composite bodies |
|---|---:|---:|---:|
| Prior compact-table token baseline | 0.993955 / 0.783 / 0.988 | 0.991570 / 0.763 / 1.001 | 1.005987 / 0.819 / 0.731 |
| Prior eight-byte event matcher | 0.994305 / 0.789 / 1.002 | 0.991957 / 0.753 / 1.026 | 1.008417 / 0.855 / 0.780 |
| Pruned three-byte events | 1.037473 / 3.339 / 4.363 | 1.044279 / 3.327 / 4.324 | 1.023210 / 4.255 / 3.509 |
| Eight-byte keys, event coder | 0.993355 / 0.816 / 1.045 | 0.990835 / 0.803 / 1.059 | 1.008065 / 0.839 / 0.829 |
| Eight-byte keys plus lookahead | 0.993236 / 0.823 / 1.086 | 0.990671 / 0.800 / 1.079 | 1.007578 / 0.819 / 0.768 |
| Eight-byte keys, token coder | 0.995231 / 0.803 / 1.038 | 0.992822 / 0.800 / 1.013 | 1.008029 / 0.835 / 0.788 |

The pruning pass improves sizes over the earlier three-byte greedy trial, but
adds enough work that encoding becomes slower. It improves only 5/131 general,
0/80 RGBA and 2/11 composite files over current CNG. The wider-key event variants
each improve 35/131, 49/80 and 3/11; the wider-key token variant improves 39/131,
44/80 and 3/11. Regressions are not rare.

| Image | Current CNG | Pruned | Wider-key events | With lookahead | Wider-key tokens |
|---|---:|---:|---:|---:|---:|
| `smooth` | 11,712 | 13,627 | 13,203 | 13,163 | 13,810 |
| `rectangle_on_gradient` | 383 | 641 | 593 | 946 | 484 |
| `large_smooth_rgba` | 21,527 | 34,445 | 31,879 | 30,411 | 33,991 |
| `composite_control_states` | 1,083 | 1,635 | 1,459 | 1,424 | 1,340 |

The lookahead regression on the rectangle is a useful counterexample: choosing
longer matches with additional index updates does not guarantee a cheaper final
packet. Wider-key token coding also fails to inherit the aggregate size benefit
seen with event coding. Match selection and its representation must be evaluated
together.

These runs use the unchanged 222-file frozen corpus and its 1,410 retained entropy
frames: 131 general images, 80 RGBA images and 11 composite bodies. RGBA includes
derivatives of 64 photographs. Each frame has one warm-up and seven paired trials
with alternating order. Timings sum per-frame medians on Apple A18 Pro, with
M4-built Release binaries from non-Apple LLVM 23.1.2 and Zstandard 1.5.7 level 3.
Task-owned local benchmarks were serialized. M4 builds can run concurrently on
the separate CPU. Results are single paired-trial runs, not confidence intervals
between different candidate runs.

Sizes include packet headers, tables, states, padding and stored escapes, then
apply the existing outer literal fallback. They are replacement accounting,
not newly emitted CNG containers. Timings include search, parsing, pricing where
present, coding, table construction and reconstruction; they exclude allocations
and complete image operations. Stored-literal bands omit failed compression
attempts, so random-image encoder cost remains unestablished.

## Checks and reproduction

Each of the four candidates passes 432 deterministic native round trips and
414 independent scalar inverses, including block boundaries, overlap and maximum
frame size. All four complete corpus runs reconstruct every frame and reproduce
the original Zstandard bytes. Six diagnostic inputs have exact reference and
byte accounting; the pricing domain's 1,024 values pass the logarithm check.
These valid-input checks are not an automated memory integrity review. No new
review was initiated.

The root `AGENTS.md` gives the M4 build commands. After copying completed binaries,
for example:

```sh
.build/entropy/cng_entropy_checks_event_pruned .build/entropy/event_pruned_vectors.bin
python3 -B experiments/entropy/event_backend_oracle.py .build/entropy/event_pruned_vectors.bin
.build/entropy/cng_entropy_checks_table_wide .build/entropy/table_wide_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/entropy/table_wide_vectors.bin
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_table_wide \
  --output .build/entropy/new-table-wide.json
.build/entropy/cng_pruning_diagnostic_pruned /path/to/frozen.csp
.build/entropy/cng_pruning_diagnostic_eight /path/to/frozen.csp
python3 -B experiments/entropy/verify_pruning_evidence.py
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

`evidence/results_{event_pruned,event_wide,event_lookahead,table_wide}.json.gz`
retains samples and source, binary and input identities. `checks_sixth.json`
records native/vector identities and the measured source versions.
`pruning_diagnostic.json` retains all six comparisons and their identities.
Named source snapshots preserve earlier harness versions as CMake evolves.
The verifier checks all source identities, frozen inputs, complete group counts,
replacement arithmetic and diagnostic conservation.

## Decision and remaining algorithm question

Retain the prior compact-table token baseline; reject this pruning pass and
these wider-key variants as its replacement. The next parser improvement must
control both coded cost and event work while choosing candidates, rather than
trying to repair an already fragmented parse. It must also consider literal-run
and distance costs: selecting the longest available match is insufficient.

The separate bounded-work parsing question recorded in `FIFTH_ROUND.md` remains
open. Its answer is not a prerequisite for further measured implementation work.
No corpus result here waives the original acceptance requirements: rare size
regressions, strong ordered-image results, actual CNG files and exact pixels,
complete encode/decode measurements, failed-attempt costs on noise, and peak
memory. The release and its Zstandard dependency remain unchanged.
