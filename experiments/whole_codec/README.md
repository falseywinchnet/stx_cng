# Whole-image entropy replacement

The tagged two-candidate matcher now runs inside an experimental image codec,
replacing every Zstandard encode and decode call. All 222 corpus images produce
actual files and reconstruct exact pixels. This closes the earlier gap between
retained entropy-frame measurements and complete image operations, including
discarded compression attempts on stored bands.

It is **not yet a suitable release replacement**. Complete encoding improves
modestly on the two large groups, but decoding regresses slightly, size losses
remain frequent, and some ordered images are substantially larger. The public
codec, signatures, dependency declarations and published evidence are unchanged.

## Construction

`candidate_codec.cpp` and its header are experimental copies of the released
image codec. They retain the same prediction, colour transform, run separation,
literal fallback, reconstruction and chunk checks. The original private helper
sources are compiled once and shared by candidate and reference. The differences
are the entropy contexts, capacity calculation, entropy calls, namespace and
experimental signature. The candidate archive has no undefined Zstandard
symbols; zlib remains for the unchanged CRC calls. The comparison executable
links Zstandard for the reference.

The private signature is `89 43 4e 45 0d 0a 1a 0a` (`CNE`). It distinguishes these
experimental files from released CNG. CHDR, BAND and CEND retain their existing
layout, while BAND entropy payloads use the candidate grammar. The header's
version value is meaningful only under this private signature. This is not a
published format revision, a new product name, or a compatibility commitment.

`match_bounded.cpp` copies the eighth round's `match_tagged.cpp`. Its parse and
wire bytes are unchanged; only its include and caller output-capacity requirement
change. For an input of N bytes, the required output capacity is now:

    N + 4 * ceil(N / 131072) + 5

Each block is first encoded in private scratch. The selected block body cannot
exceed the original block length, and the block header takes four bytes. The
frame prefix takes five. The final whole-frame raw escape needs only N + 5.
Thus the implementation never needs its former 2N + 1024 caller bound. This
reduces complete-codec allocations without changing compression decisions.

The context owns 656,400 encoder bytes or 131,088 decoder bytes. These are not
complete-codec memory figures: buffers for prediction, transforms, packets and
output remain necessary, and the entropy kernels also use stack tables.

## First complete-image measurements

Ratios divide candidate by released CNG using Zstandard 1.5.7 level 3. Lower is
better. Times are sums of per-image medians, with seven alternating paired trials
after warm-up. Sizes are actual emitted files, including every chunk and packet.

| Corpus | Images | Size ratio | Encode time ratio | Decode time ratio | Smaller / equal / larger |
|---|---:|---:|---:|---:|---:|
| General | 131 | 0.993384 | 0.955245 | 1.029642 | 48 / 0 / 83 |
| RGBA | 80 | 0.990809 | 0.937221 | 1.028001 | 48 / 0 / 32 |
| Composite image bodies | 11 | 1.004837 | 1.021832 | 1.032429 | 3 / 2 / 6 |

The actual byte totals are 97,748,027 versus 98,399,047; 98,874,338 versus
99,791,531; and 744,913 versus 741,327 respectively. The earlier retained-frame
accounting differed by four bytes on each of three general images. Re-running
all attempts changes three literal bands from stored to compressed. That is why
this report uses emitted files rather than assuming accounting equivalence.

| Case | Reference bytes | Candidate bytes | Reference encode / decode, ms | Candidate encode / decode, ms |
|---|---:|---:|---:|---:|
| `smooth` | 11,712 | 13,852 | 0.757 / 0.231 | 0.813 / 0.226 |
| `large_smooth_rgba` | 21,527 | 29,178 | 11.309 / 5.672 | 12.258 / 5.163 |
| `random` | 295,006 | 295,002 | 1.344 / 0.027 | 1.476 / 0.024 |
| `mixed_order_noise` | 150,930 | 154,182 | 1.100 / 0.283 | 1.039 / 0.397 |

Random literals finish stored in both codecs. Nevertheless, the candidate spends
0.133 ms in entropy encoding versus 0.048 ms for the reference before that
decision. The full encoder is about 9.8% slower on this case. The earlier test
which omitted discarded attempts could not reveal this cost.

For mixed structure and noise, entropy decoding takes 0.149 ms versus 0.025 ms;
the complete decode regresses about 40%. The candidate's block-level operation
includes Huffman expansion followed by reconstruction of match tokens. These
timings locate the regression in entropy decoding but do not isolate table
construction, bit reading, literal traffic and match copying from one another.
Those costs need separate measurement before attributing the result to any one
of them. Similarly, random encoding needs attribution among index maintenance,
frequency counting, code construction and copies.

The large smooth case is still 35.5% larger, despite its decode win. A smaller
corpus total does not meet the requirement for rare regressions or strong ordered
image behavior. No claim of a globally optimal parser follows from these results.

## Memory and timing scope

| Corpus | Maximum owned encode bytes, reference / candidate | Maximum owned decode bytes, reference / candidate |
|---|---:|---:|
| General | 11,015,234 / 10,355,879 | 7,440,684 / 7,471,722 |
| RGBA | 10,484,282 / 9,825,951 | 6,911,262 / 6,943,318 |
| Composite bodies | 10,486,079 / 9,827,743 | 6,912,031 / 6,944,086 |

These are the codec's existing owned-capacity measurements, including the new
contexts. They exclude stack tables, allocator overhead, stream buffers and
caller pixel storage. Maximum values need not belong to the same image in the
two implementations. Candidate decode capacity is slightly larger, while encode
capacity is lower. This is not a claim of lower process peak memory.

The runner also records Darwin peak RSS for each entire comparison process.
That process retains original pixels, both encoded files, output pixels, warm-up
state and trial strings. Its RSS cannot be assigned to either codec and must not
be presented as candidate-only memory. A separate isolated peak-memory comparison
remains necessary for that claim.

Execution was on Apple A18 Pro, with Release binaries built on M4 using non-Apple
LLVM 23.1.2, C++20, `-O3 -DNDEBUG`, ordinary arm64 and no platform intrinsics.
The reference uses Zstandard 1.5.7 and the unchanged system zlib CRC implementation.
RGBA includes derivatives of 64 photographs, not 80 independent photographs.

Complete encode timings include codec allocation and destruction, transforms,
all entropy attempts, CRC and writes to a growing in-memory output stream.
Complete decode timings use `decode_into`: the caller's output allocation is
outside timing, while codec allocations, entropy decoding, reconstruction, CRC
and input consumption are inside. File I/O, corpus loading, output comparisons
and source hashing are outside timing. The stream-output decoder is checked for
exactness once per image but is not the timed decode interface. The composite
cases exercise composite image bodies, not either collection wrapper.

These are individual paired-trial measurements. Every sample is retained; the
reported few-percent differences are not confidence intervals or cross-machine
performance guarantees. Differences from entropy-only runs do not by themselves
prove an allocation or cache explanation.

## Verification and reproduction

The native entropy check uses exactly the tightened output capacity for 432
deterministic cases. Its vectors match the original tagged matcher byte for byte;
414 cases also pass the independent scalar inverse. All 222 complete-image runs
check both decoder interfaces, every pixel, dimensions and deterministic packet
bytes. Reference encoding must reproduce each frozen release file byte for byte.
The Python runner checks reconstructed raw hashes against the admitted source
hashes before deleting its own temporary raw output. Actual `.cne` files remain
under the selected output directory.

`evidence/first_corpus.json.gz` retains every timing sample, image identity, output
hash, measured capacity, source hash and comparison-process RSS. `checks.json`
records compiler settings and binary/vector identities. The verifier checks these
records against the admitted corpus and current sources; with `--artifacts`, it
also checks every emitted file's size, hash and private signature. Preserve these
source versions if extending the experiment.

From the repository root, serialize these M4 mirror/build operations:

```sh
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake -S experiments/whole_codec -B .build/whole-codec -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
/Users/ultimussecundai/.local/bin/m4build -- /opt/homebrew/bin/cmake --build .build/whole-codec -j 4
```

Copy the resulting `cng_whole_probe` and `cng_whole_bounds` binaries from the
selected host's mirror into the corresponding local build directory for A18 Pro
execution. Then use a new corpus output directory:

```sh
.build/whole-codec/cng_whole_bounds .build/whole-codec/bounds_vectors.bin
python3 -B experiments/entropy/match_oracle.py .build/whole-codec/bounds_vectors.bin
cmp .build/whole-codec/bounds_vectors.bin .build/entropy/tagged_vectors.bin
python3 -B experiments/whole_codec/run.py --corpus /path/to/cng-research --output .build/whole-codec/new-corpus
python3 -B experiments/whole_codec/verify.py --artifacts .build/whole-codec/first-corpus
python3 -B tools/check_evidence.py
```

These are valid-input exactness checks. No automated memory integrity review was
initiated. The release remains unchanged while parser research and measured
reductions in noise handling and entropy decoding continue.
