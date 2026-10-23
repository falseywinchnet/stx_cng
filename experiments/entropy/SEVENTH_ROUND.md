# Remove stored padding and simplify short packets

The next independent candidate is the compact-table Huffman-token baseline with
unpadded Huffman streams, an implicit final stream length, and one stream for
short packets. The refined tail reader preserves the smaller packets and returns
decoding below Zstandard's measured time on both large corpus groups. This is
useful progress, not completion: small structured images still lose too often,
and complete-image integration and noise-attempt measurements remain outstanding.

## Construction

The previous Huffman representation stored eight zero bytes after each of four
streams and four 32-bit lengths. Its bounded lookup needed readable tail bits,
but those zeros conveyed no source information. Experimental mode 35 removes
the 32 padding bytes and the last length. The final stream occupies the rest of
the enclosing packet. The model and encoded symbols are unchanged.

The reader supplies zeros locally at its final refill. Its position tracks the
virtual bits supplied, including those zeros. After producing the declared
output, it checks that the consumed bits round up to exactly the stored extent,
and that unused bits in the final byte are zero. Full interior refills still
read four bytes directly. No out-of-span load or borrowed padding is required.
No large temporary buffer or allocation is added.

Experimental mode 36 further uses one stream when the Huffman source contains
at most 1,024 bytes, and four streams otherwise. This is one fixed packet-length
rule, not an image classifier or an opt-in model. The single-stream form needs
no stream-length fields and no paired lookup table: it uses the canonical
single-symbol table. Larger packets retain four independent recurrence states
and the paired-symbol decoder. The threshold is an initial fixed design choice,
not a proved optimum; this round did not tune it against the corpus.

`huffman_unpadded.cpp` supplies the first two variants. The third,
`huffman_tail_split.cpp`, retains mode 36 and the same encoder while simplifying
the full-refill test and separating the unlikely tail helper. The decoded-size
bound limits the virtual position to at most `4*maximum_input+4`, so adding four
in the refill test cannot overflow. The helper receives fewer than four real
bytes or an exhausted stream. Its branch hint describes the rare tail; timing,
not that hint alone, determines whether the change helps.

The matcher, token grammar, code-length construction and 128 KiB block size are
unchanged. Caller workspace remains 459,792 encoder bytes and 131,088 decoder
bytes, excluding stack tables, input/output and harness/reference storage. The
published CNG format and Zstandard dependency are unchanged.

## Size property

For an already coded four-stream Huffman packet, removing padding and the last
length saves exactly 36 bytes. Some previously stored packets also become worth
coding. The single-stream form removes another 12 header bytes and cannot add
bit-rounding bytes: concatenating the same codes has no more rounding than four
separate streams. Raw and constant forms retain their existing representation.
The length rule applies to the Huffman input, which can be a parsed token stream,
not necessarily the original entropy frame.

Consequently these changes cannot enlarge a packet produced with the unchanged
parser. The outer stored/literal fallback preserves that property. All 1,410
retained corpus frames confirm it: 1,270 become smaller and 140 remain equal.
Every file's replacement accounting is nonincreasing against the prior token
baseline. The refined tail reader emits byte-identical deterministic vectors
to the first single-stream implementation and equal sizes on all corpus frames.

This does not mean every file is now smaller than current CNG. The new candidate
improves 43/131 general files, 48/80 RGBA files and 3/11 composite bodies over the
Zstandard-backed release. That distribution still misses the owner's requirement
that regressions be rare.

## Measurements

Each cell is **accounted file size / encode time / decode time**, divided by the
Zstandard control. Lower is better.

| Candidate | General | RGBA | Composite bodies |
|---|---:|---:|---:|
| Prior compact-table token baseline | 0.993955 / 0.783 / 0.988 | 0.991570 / 0.763 / 1.001 | 1.005987 / 0.819 / 0.731 |
| Remove padding and final length | 0.993501 / 0.781 / 1.004 | 0.991016 / 0.747 / 1.039 | 1.005042 / 0.785 / 0.752 |
| Also use one stream for short packets | 0.993487 / 0.822 / 0.989 | 0.991010 / 0.789 / 0.987 | 1.004968 / 1.034 / 0.722 |
| Same packets, refined tail reader | 0.993487 / 0.806 / 0.979 | 0.991010 / 0.799 / 0.976 | 1.004968 / 1.023 / 0.758 |

The refined form takes about 80% of Zstandard's encode time and 98% of its decode
time on the general and RGBA groups. Composite encoding is slightly slower in
this run. Encode differences between the two single-stream runs are measurement
variation: their encoder source is identical. The tail change is not evidence
of an encoding improvement. These are individual paired-trial runs, not confidence
intervals between candidates. Small percentages need further complete-operation
measurement before any production performance claim.

| Image | Current CNG | Prior token baseline | New candidate |
|---|---:|---:|---:|
| `smooth` | 11,712 | 14,025 | 13,953 |
| `rectangle_on_gradient` | 383 | 481 | 468 |
| `large_smooth_rgba` | 21,527 | 33,928 | 31,721 |
| `composite_control_states` | 1,083 | 1,476 | 1,391 |

The large smooth case improves by 2,207 bytes, about 6.5% against the prior
candidate, but still trails current CNG substantially. The rectangle saves only
13 bytes because its tiny token streams were largely stored rather than using
the padded Huffman form. Removing overhead does not recover missing matches or
improve the code distribution; it only removes representation cost.

The workload is the unchanged 222-file corpus: 131 general, 80 RGBA and 11
composite bodies, with 1,410 retained entropy frames. The RGBA set includes
derivatives of 64 photographs. Execution uses Apple A18 Pro; Release binaries
are built on M4 with non-Apple LLVM 23.1.2. The control is Zstandard 1.5.7 level 3.
Each frame has one warm-up and seven paired trials with alternating order;
reported ratios sum per-frame medians. Task-owned local timing runs are serialized.
M4 builds can run on their separate CPU during local measurements.

Sizes include headers, model descriptions, bit rounding and stored escapes,
followed by the current outer literal fallback. They are accounted replacement
sizes, not emitted candidate CNG containers. Timings include search, parsing,
entropy models/coding and reconstruction, but exclude allocation and full image
operations. Frozen retained frames omit unsuccessful attempts on stored-literal
bands. Noise-image encoding cost is therefore still unestablished.

## Verification and reproduction

Each of the three candidates passes 432 deterministic native match round trips
and 414 independent scalar inverses, plus exact reconstruction of all retained
corpus frames and byte-identical Zstandard control recompression.

Each Huffman implementation additionally passes 405 native and 405 independent
checks: all lengths 0–65 and boundaries around 256, 1,024, 4,096, 65,536 and
131,072 bytes, with constant, binary, skewed, random-byte and periodic sources.
These cover the stream-count transition and short final refills. Both the
matching vectors and direct Huffman vectors are byte-identical before and after
the final tail refactor. The expanded independent inverse also still reconstructs
the earlier baseline's 414 vectors.

These are valid-input checks, not an automated memory integrity review. No new
review was initiated. Release-source hashes and published evidence remain intact.
The M4 build commands are in root `AGENTS.md`. After copying completed binaries:

```sh
.build/entropy/cng_entropy_checks_short_tail .build/entropy/short_tail_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/entropy/short_tail_vectors.bin
.build/entropy/cng_huffman_tail_short_tail .build/entropy/huffman_tail_short_tail.bin
python3 -B experiments/entropy/huffman_tail_oracle.py .build/entropy/huffman_tail_short_tail.bin
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_short_tail \
  --output .build/entropy/new-short-tail.json
python3 -B experiments/entropy/verify_tail_evidence.py
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

`evidence/results_{unpadded,short_stream,short_tail}.json.gz` retains all samples
and source, binary and input identities. `checks_seventh.json` records native and
independent check counts and vector/binary hashes. Source snapshots preserve
measured CMake, oracle and README versions. The verifier checks all source
identities, complete runs, frame-by-frame size monotonicity, accounted sizes and
identical tail-refactor vector hashes.

Carry the refined unpadded short-stream form forward as the new candidate while
retaining the previous baseline for timing comparisons. The remaining central
problem is still match selection and representation on ordered inputs. Full
image integration, exact pixels, complete timings, failed-attempt costs on noise
and peak memory remain required before replacing Zstandard in the release.
