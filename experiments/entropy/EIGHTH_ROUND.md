# Bounded candidate retention and prefix records

Retaining two recent candidates per four-byte hash bucket improves several
ordered cases. Storing their exact prefixes beside their positions recovers most
of the added encoder work in the measured large groups. Carry that tagged
construction forward as the next research candidate. The mixed four/eight-byte
index loses overall and is retained as a negative result.

This remains an incomplete replacement: size regressions against current CNG are
still frequent, composite encoding is slower in this run, and whole-image and
noise-attempt measurements remain outstanding. The release is unchanged.

## Search and equivalent refactors

All variants retain the seventh round's unpadded Huffman representation, fixed
short-stream rule, eight-byte minimum match, 128 KiB blocks and sampled miss
schedule. They do not change the decoder's representation or add a runtime model
selector. Each queried position examines at most two index candidates.

- `two_candidates`: each four-byte hash bucket retains its two most recent
  positions. Check the complete eight-byte prefix, extend exact matches, and
  select the longer result, preferring the nearer source on equal lengths.
  Two position arrays occupy 128 KiB rather than the original index's 64 KiB.
- `dual_key`: retain one position in each of a four-byte and an eight-byte index.
  Compare both candidates by the same rule. This changes candidate retention and
  collisions; it is not merely a change in lookup width.
- `two_tail`: preserve the two-recent-position search, but reject a second
  candidate if it disagrees at the final byte of the current best span. Such a
  candidate cannot tie or exceed that span. If the best already reaches the block
  end, an older candidate cannot improve either length or nearest-distance tie.
  This shortcut does not change the selected parse.
- `tagged`: preserve that search and shortcut while storing each exact four-byte
  prefix alongside its position. Two prefix/position records occupy an aligned
  16-byte bucket. A prefix mismatch can be rejected from the index itself, before
  following the candidate back into source bytes. This replaces the two separate
  position arrays with one contiguous bucket array. The source is immutable, so
  the stored prefix remains valid throughout its entry's lifetime.

The tail sample is in bounds because the earlier source precedes the current
position and the best length is at most the remaining block extent. Both full
extension scans are bounded by the selected span, giving at most two such scans
per committed match rather than an unbounded chain. Duplicate candidates are
skipped. Post-match insertion and index initialization follow the same fixed
rules as the baseline, extended to the second record.

The tagged representation increases index traffic and initialization size while
avoiding source-prefix reads and placing both candidates together. Timing tests
measure this tradeoff; locality alone is not a performance guarantee. There are
no platform intrinsics, per-query allocations, or unbounded searches.

The tail shortcut and tagged records produce byte-identical deterministic vectors
to the original two-candidate version. All 1,410 corpus frame sizes and all 222
accounted file sizes agree across those three variants. Corpus packets are not
retained, so corpus-wide byte identity is not inferred solely from equal sizes.
Their match-selection equivalence also follows from the rejection conditions
and the immutable-prefix invariant described above.

## Measured results

Each cell is **accounted file size / encode time / decode time**, divided by the
Zstandard control. Lower is better.

| Candidate | General | RGBA | Composite bodies |
|---|---:|---:|---:|
| Prior unpadded single-candidate baseline | 0.993487 / 0.806 / 0.979 | 0.991010 / 0.799 / 0.976 | 1.004968 / 1.023 / 0.758 |
| Two recent positions | 0.993384 / 0.897 / 0.971 | 0.990809 / 0.848 / 0.965 | 1.004837 / 1.152 / 0.717 |
| Four/eight-byte indexes | 0.994782 / 0.957 / 1.008 | 0.992282 / 0.970 / 1.013 | 1.006405 / 1.177 / 0.747 |
| Two positions, exact tail rejection | 0.993384 / 0.882 / 0.976 | 0.990809 / 0.857 / 0.976 | 1.004837 / 1.113 / 0.724 |
| Exact prefixes stored beside positions | 0.993384 / 0.806 / 0.996 | 0.990809 / 0.783 / 0.987 | 1.004837 / 1.078 / 0.740 |

The prefix records retain the two-position compression benefit while taking
about 81% and 78% of Zstandard's encode time on the general and RGBA groups.
Decoding is near parity on those groups. Composite encoding still regresses.
The three equivalent representations use the same decoder and packet grammar;
differences in their measured decode ratios do not demonstrate a new decoding
algorithm. Runs are individual paired-trial measurements, not confidence intervals
between variants. The tail shortcut alone has no strong timing win in these runs.

| Image | Current CNG | Prior candidate | Tagged two-candidate search |
|---|---:|---:|---:|
| `smooth` | 11,712 | 13,953 | 13,852 |
| `rectangle_on_gradient` | 383 | 468 | 464 |
| `large_smooth_rgba` | 21,527 | 31,721 | 29,178 |
| `composite_control_states` | 1,083 | 1,391 | 1,241 |

The large smooth case saves another 2,543 bytes, about 8.0%, against the prior
candidate. The control-state composite saves 150 bytes, about 10.8%. Neither
has reached the release's size yet. These improvements support retaining more
than the latest colliding prefix; they do not establish that the chosen match
is globally optimal or that a larger candidate budget would be beneficial.

Against the previous candidate, general files split into 61 smaller, 34 equal
and 36 larger; RGBA into 46 smaller and 34 larger; composites into 5 smaller,
5 equal and 1 larger. The largest proportional general regression is
`horizontal_32`, from 115 to 119 bytes. The largest proportional RGBA regression
is about 0.083%. These are not size-monotone changes like padding removal.
Against current CNG, the new search improves 45/131 general files, 48/80 RGBA
files and 3/11 composite bodies. Regressions are still not rare enough.

Compiler-measured caller storage is:

| Construction | Encoder bytes | Decoder bytes |
|---|---:|---:|
| Prior single-candidate baseline | 459,792 | 131,088 |
| Two position arrays, including tail shortcut | 525,328 | 131,088 |
| Tagged 16-byte buckets | 656,400 | 131,088 |

These exclude stack tables, input/output and reference/harness storage. They do
not establish complete CNG peak memory. Workspaces are allocated outside timing;
index reset, candidate lookup and extension, token construction, Huffman work
and reconstruction are timed.

The unchanged workload has 222 frozen files and 1,410 retained entropy frames:
131 general images, 80 RGBA images and 11 composite bodies. RGBA includes
derivatives of 64 photographs. Execution is on Apple A18 Pro, with Release
binaries built on M4 using non-Apple LLVM 23.1.2. The reference is Zstandard 1.5.7
level 3. Each frame has one warm-up and seven alternating paired trials. Ratios
sum per-frame medians. Task-owned local benchmarks are serialized; M4 builds
may run concurrently on their separate CPU.

Sizes include all candidate packet overhead and the existing outer literal
fallback. They are file replacement accounting, not emitted candidate CNG files.
Timings exclude full image operations and failed compression attempts on
stored-literal bands. No conclusion about random-image encode cost follows.

## Checks and provenance

All four variants pass 432 deterministic native round trips and 414 independent
scalar inverses, including overlap, block boundaries and maximum frame size.
Every complete corpus run reconstructs each retained frame and reproduces the
original Zstandard control bytes. The expanded independent inverse checks the
existing token grammar with the new Huffman packets.

Native/vector hashes, check counts, observed benchmark source identities and
build-input snapshots are retained in `evidence/checks_eighth.json`. The explicit
build inputs identify the harness revisions used when compiling, including
versions before later compile-time include branches were added. Benchmark source
maps describe the files observed when each run began; those maps can also include
unrelated experiment sources. Named snapshots preserve every recorded version.
`workspaces_eighth.json` retains compiler-measured storage and binary identities.
`results_{two_candidates,dual_key,two_tail,tagged}.json.gz` retains every timing
sample and input identity. `verify_candidate_evidence.py` checks completeness,
frozen inputs, accounted totals, source identities, equivalent-refactor frame
sizes and vector hashes, and workspace values.

These are valid-input algorithm checks, not an automated memory integrity review.
No new review was initiated. Published release-source hashes remain unchanged.

Use the root M4 configure command before requesting newly added build targets.
An initial targeted build before regeneration returned “No rule to make target”;
configuration followed by the targeted build succeeded. After copying binaries:

```sh
.build/entropy/cng_entropy_checks_tagged .build/entropy/tagged_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/entropy/tagged_vectors.bin
cmp .build/entropy/two_candidates_vectors.bin .build/entropy/tagged_vectors.bin
.build/entropy/cng_workspace_tagged
python3 -B experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_tagged \
  --output .build/entropy/new-tagged.json
python3 -B experiments/entropy/verify_candidate_evidence.py
python3 -B experiments/entropy/summarize.py
python3 -B tools/check_evidence.py
```

Carry the tagged construction forward while retaining the smaller single-candidate
baseline as a comparison. Reject the mixed-width index for this workload. The
remaining objective requires better ordered-image representation, rare size
regressions, actual candidate CNG containers, exact pixels, complete encode/decode
measurements, noise-attempt costs and peak memory. None is waived by these
retained-frame improvements.
