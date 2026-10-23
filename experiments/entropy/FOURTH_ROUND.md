# Short matches and compact statistical descriptions

The replacement goal remains open. This round improves the independent backend's
model representation and separates three causes of loss: the minimum admitted
match length, integer-event representation, and the cost of transmitting models.
The new event coding is still a diagnostic replay using reference-generated
matches; it is not an independently measured match-finding encoder.

The full in-house corpus trial changes only the Huffman code-length description
in the previous `history` baseline. Every candidate entropy packet is no larger
than before. The larger goal still fails: many individual images remain larger
than the current CNG files, and no alternative whole-image codec has been
integrated or measured.

## Controlled replay: short matches alone are insufficient

The generated Zstandard level-3 parse is clipped to the same 128 KiB raw-block
boundaries. One replay admits pieces of at least eight bytes, another admits
pieces of at least three. The latter retains the reference's short matches,
except one- or two-byte pieces created at a block edge. Literal counts, distances
and length-minus-minimum remain variable-length byte integers in the same
Huffman-coded stream. Both packet variants are reconstructed independently with
a scalar copy loop. The eight-byte result exactly reproduces the prior replay's
sizes for every frame.

| Stream | Zstandard | Eight-byte replay | Three-byte replay |
|---|---:|---:|---:|
| `smooth` literals | 7,219 | 8,755 | 9,099 |
| `kodim01` literals | 560,732 | 556,503 | 576,647 |
| `large_smooth_rgba` sequences | 17,581 | 28,918 | 28,602 |
| `large_smooth_rgba` literals | 3,473 | 4,986 | 4,472 |

Simply keeping those matches does not close the gap. This does not establish
that short matches are intrinsically undesirable: they were selected for a
different representation, with different costs. It establishes that our byte
integer grammar does not exploit the supplied short matches effectively.

## Direct integer events

`match_events.cpp` adapts our existing event coder to named records containing
literal count, match length and distance. Positive match lengths subtract two,
leaving zero for a terminal literal-only record. Each field has its own 1,024-slot
rANS model. Small values below sixteen have exact symbols; larger values retain
the exponent and two leading fraction bits, with remaining bits emitted directly.
The initial distance representation has a special symbol for the previous
nonzero distance. No sequence-byte-plane conversion is required by this API.

Literals use the existing four-stream Huffman kernel. The replay packages events
and literals separately, includes all tables, states and framing, and retains
plain-literal and raw escapes. It uses the same reference parse for each event
formulation; no new match search or image classifier is involved.

This improves the original smooth literal replay from 9,099 to 7,599 bytes,
versus Zstandard's 7,219. Small blocks still lose significantly. The subsequent
variants keep that representation and compact its descriptions:

1. **Compact Huffman table.** Lengths zero through twelve are literal nibbles.
   Token fourteen repeats the previous length 3–18 times; token fifteen repeats
   it 19–274 times. The previous length starts at zero. Every run costs no more
   nibbles than listing its lengths, so the 256-entry description occupies at
   most the old 128 bytes. The reconstructed lengths and Huffman codes are
   unchanged. No alternate model or selector is added.
2. **Packed event model.** Active counts and symbols fit in seven bits.
   Non-final normalized frequencies fit in ten bits; the final frequency is the
   known total of 1,024 minus the frequencies already read. The probabilities,
   suffixes and rANS recurrence remain unchanged. Model bits are padded to the
   next byte before the suffix-size and state fields.
3. **Three recent distances.** Maintain three nonzero distances in move-to-front
   order. Codes one through three select a retained distance; a new distance is
   offset by three. Zero retains its terminal/prediction meaning. This changes
   the distance alphabet but not the generated match choices. State resets for
   each event packet. This cache does not implement all of Zstandard's repeat
   offset semantics.

## Actual byte accounting

`replay_account.json` verifies that the emitted event packet's component sizes
sum exactly to its total. With compact Huffman tables but the original event
model description, the large smooth sequence stream occupies 27,608 bytes:

| Component | Bytes |
|---|---:|
| Event probability-model descriptions | 6,993 |
| Integer suffix bits, rounded to bytes | 7,682 |
| rANS payload | 8,040 |
| Literal packets | 2,995 |
| rANS states | 784 |
| Event framing | 833 |
| Block headers | 196 |
| Frame headers | 85 |
| **Total** | **27,608** |

This corrected the assumption that Huffman tables were the main source of this
particular loss. Compact Huffman tables help, but event models and integer
representation account for much more of the remaining cost.

The following comparisons use compact Huffman tables throughout. All values
are entropy packet bytes, before outer CNG file accounting:

| Stream | Zstandard | Direct events | Packed models | Packed models + three distances |
|---|---:|---:|---:|---:|
| `smooth` sequences | 4,420 | 4,734 | 4,645 | 4,633 |
| `smooth` literals | 7,219 | 7,599 | 7,503 | 7,477 |
| `mixed_order_noise` literals | 148,318 | 148,637 | 148,470 | 148,430 |
| `kodim01` literals | 560,732 | 559,082 | 558,582 | 558,591 |
| `rectangle_on_gradient` sequences | 61 | 107 | 95 | 95 |
| `rectangle_on_gradient` literals | 249 | 452 | 397 | 392 |
| `large_smooth_rgba` sequences | 17,581 | 27,608 | 25,432 | 23,482 |
| `large_smooth_rgba` literals | 3,473 | 5,671 | 5,517 | 5,558 |
| `composite_control_states` sequences | 715 | 974 | 905 | 891 |
| `composite_control_states` literals | 295 | 522 | 472 | 474 |

The recent-distance formulation preserves its regressions in this table. Packing
the model alone cannot enlarge its description; changing the distance alphabet
can change normalized probabilities and suffix costs either way.

These are **diagnostic sizes**, not encoder benchmarks. The generated parse is
informational, not extracted from the frozen bitstream, and is not optimized
for our grammar. Every reference is verified against source bytes, every replay
is decoded exactly, and ordinary Zstandard recompression separately matches the
frozen frame bytes. The diagnostic does not provide an independent replacement
for Zstandard's match finder or establish its speed.

## Full independent backend trial

`cng_entropy_probe_table` combines our own `history` matcher with the compact
Huffman table. It does not use reference-generated matches or the new event
coder. Its fixed 128 KiB entropy blocking and all other coding decisions remain
the same as the previous `history` baseline.

| Group | Accounted size change vs current CNG | Encode time / Zstandard | Decode time / Zstandard | Smaller files |
|---|---:|---:|---:|---:|
| General, 131 files | −0.605% | 0.783 | 0.988 | 39 |
| RGBA, 80 files | −0.843% | 0.763 | 1.001 | 47 |
| Composite bodies, 11 files | +0.599% | 0.819 | 0.731 | 3 |

The new model descriptions do not increase any of the 1,410 candidate packet
sizes compared with `history`. The general group improves from 38 to 39 smaller
files, RGBA from 44 to 47, and composites remain at three.

| Image | Current CNG | Previous `history` accounting | Compact-table accounting |
|---|---:|---:|---:|
| `smooth` | 11,712 | 14,045 | 14,025 |
| `rectangle_on_gradient` | 383 | 481 | 481 |
| `large_smooth_rgba` | 21,527 | 34,876 | 33,928 |
| `composite_control_states` | 1,083 | 1,573 | 1,476 |

The execution CPU is Apple A18 Pro, with Release binaries built on the M4 using
non-Apple LLVM 23.1.2. Zstandard is 1.5.7, level 3. Each frame receives one warm-up
and seven paired timed trials with alternating order; ratios sum per-frame
medians. The previous history run is a separate observation, so its timing
comparison is not a controlled claim that table compaction itself speeds up
encoding. Model serialization adds work while removing bytes.

These are retained-entropy timings and file-size accounting. They exclude
complete image reconstruction, discarded attempts on stored-literal bands and
collection opening. No candidate CNG files were emitted. The RGBA corpus includes
derivatives of 64 photographs. The ordinary matcher's caller workspace remains
459,792 encoder bytes and 131,088 decoder bytes, excluding entropy stack tables,
input/output and harness buffers. The diagnostic event replay has additional
record and scratch storage; it has no production workspace claim.

## Checks and reproducibility

- Compact-table matcher: 432 native round trips and 414 independent bit-by-bit
  Huffman / byte-copy inverses, plus all 1,410 corpus frames exactly reconstructed.
- Code-length grammar: 43,434 native and independent inverses. These cover every
  pair of length values zero through twelve at every split of a 256-entry table,
  plus an alternating-length case. The grammar checks deliberately include
  arrays that are not valid Huffman trees; they establish serialization and its
  128-byte bound, not prefix-tree validity. Corpus and coder tests exercise valid
  constructed trees separately.
- Each of the three direct-event coders: 60 native and 60 independent scalar
  inverses. Cases include empty and maximal event counts, all-zero fields,
  repeated distances, alternating cache positions, bucket boundaries and values
  through 2,097,160. Earlier 48-case observations are retained locally.
- Six selected images replay exactly in each diagnostic formulation. Replay
  account sums and every source identity are checked by
  `verify_replay_evidence.py`.

These are valid-input algorithm checks, not an automated memory integrity review.
No additional review was initiated. The published source hashes and all 222
release evidence records remain unchanged.

After building through the root `AGENTS.md` M4 commands and copying completed
arm64 executables for local measurements:

```sh
.build/entropy/cng_table_model_checks .build/entropy/table_model_vectors.bin
python3 -B experiments/entropy/table_model_oracle.py .build/entropy/table_model_vectors.bin
.build/entropy/cng_match_event_recent_checks .build/entropy/match_event_recent_vectors.bin
python3 -B experiments/entropy/match_event_oracle.py .build/entropy/match_event_recent_vectors.bin
.build/entropy/cng_entropy_checks_table .build/entropy/table_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/entropy/table_vectors.bin
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_table --output .build/entropy/new-table.json
python3 -B experiments/entropy/replay_recent.py --corpus /path/to/cng-research \
  --output .build/entropy/new-recent-replay.json
python3 -B experiments/entropy/verify_replay_evidence.py
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

The `replay_*.json` reports retain source/binary/input identities. The full
backend report is `results_table.json.gz`. Source snapshots retain earlier
measured harness versions. The historical `rans_*` timing fields still denote
the candidate operation, including Huffman matching trials.

## Next acceptance-relevant step

Connect the compact integer-event representation to our own cheap matcher and
measure its complete entropy operations. Keep the existing independent matcher
as the control; do not report replay results as achievable independent encoder
performance. Compare per-image distributions and include short-match search
work, event-model construction, literal coding and restoration in the timings.

The event representation should also be examined for model cost across a whole
CNG band: the large smooth case carries 49 event blocks in 17 entropy frames.
This is a measurable partitioning question, not authorization for a menu of
image-dependent models. Any consolidation must retain bounded workspaces and
account for changed probability distributions, buffering and latency. Longer
history alone, a lower minimum match length alone and byte-saving descriptor
criteria have already failed to solve the problem.

Only after an independent backend meets the per-image size and speed objective
should it enter an experimental whole-image path. Exact pixels, end-to-end
encode/decode speed, noise work and peak workspace remain required before any
release-format or dependency decision.
