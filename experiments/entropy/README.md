# An in-house entropy backend for CNG

This page records the first round. Continue with [direct events and paired
Huffman coding](SECOND_ROUND.md) for the current evidence and remaining work.

The first experiment does **not** justify replacing Zstandard in the released
codec. It establishes an independent coder, identifies the work it is missing,
and preserves three measured formulations. None changes the published format.

## What must be replaced

CNG already extracts exact prediction runs and pixel copies. Its remaining
sequence records and transformed literal residuals still contain repetition.
Zstandard compresses that repetition as well as coding symbol frequencies;
removing it therefore removes more than an entropy coder. See the sequence,
literal and entropy descriptions in [RFC 8878](https://www.rfc-editor.org/rfc/rfc8878.html).

The independent baseline uses range asymmetric numeral systems (rANS), following
the mathematical construction in [Duda's ANS paper](https://arxiv.org/html/1311.2540v2).
Four interleaved states expose independent arithmetic without architecture-specific
intrinsics, as described in [Giesen's interleaving paper](https://arxiv.org/html/1402.3392v1).
No external implementation source was imported. These are implementations of
known algorithms, not a claim to a newly invented entropy coding principle.

One deterministic byte histogram is transmitted per block. Frequencies sum to
4096. Each observed symbol receives one slot; cumulative integer apportionment
distributes the remainder. There is no trained model, image classifier, online
adaptation, or image-specific selection among these experiments.

## Three formulations

1. **Byte rANS:** four 32-bit states, byte refill, division during encoding.
2. **Word rANS:** at most one 16-bit refill per symbol, a packed 16 KiB decode
   table, and exact reciprocal division with a single overshoot correction.
3. **Word rANS with sequence runs:** apply CNG's existing repeated-byte
   separation to sequence byte planes before word rANS. Literal handling is
   unchanged from the second formulation. Restoration emits whole repeated
   spans instead of decoding every repeated byte through rANS.

These are separate experiment executables, not codec options. The repeated-byte
transform is copied from our own MIT codec and retains its four-byte run threshold.
It removes equal-byte runs only; it does not replace Zstandard's general matching.

## Results

Measured on an Apple A18 Pro, with release binaries built on the M4 host using
non-Apple LLVM 23.1.2. The control uses Zstandard 1.5.7, level 3. Each frame has
one warm-up and seven timed paired trials with alternating execution order.
Context and buffer allocation are outside timing; histogram construction,
frequency serialization, decode-table construction and run transformation are
inside their respective operation timings. Each timed decode is checked exactly.

The corpus contains 131 general files, 80 RGBA files and 11 composite image
bodies: 222 files and 1,410 retained entropy frames. The RGBA set includes
derivatives of 64 photographs. Composite rows do not measure collection opening.

The following size changes account for replacing retained frames in the frozen
files, including our model, state and framing bytes and the existing outer
stored-literal fallback:

| Formulation | General | RGBA | Composite bodies |
|---|---:|---:|---:|
| Byte rANS | +2.92% | +3.18% | +8.32% |
| Word rANS | +2.92% | +3.17% | +8.31% |
| Word rANS + sequence runs | **+1.68%** | **+2.53%** | **+2.13%** |

For the third formulation:

| Corpus | Current bytes | Accounted replacement bytes | Encode time / Zstd | Decode time / Zstd |
|---|---:|---:|---:|---:|
| General | 98,399,047 | 100,048,338 | 1.26 | 2.17 |
| RGBA | 99,791,531 | 102,318,966 | 1.23 | 2.17 |
| Composite bodies | 741,327 | 757,136 | 1.57 | 1.18 |

Time ratios are sums of per-frame medians; **lower is better**. They measure
complete entropy operations, not complete image encode/decode. Across separate
runs, paired ratios against Zstandard are more useful than comparing absolute
times. No confidence interval or cross-platform performance claim is made.

The sequence-run change gets sequence decoding close to the control: 1.13 times
its time on general files, 0.97 on RGBA, and 0.89 on composites. However, sequence
payloads remain 45.6%, 13.7%, and 4.0% larger respectively. General and RGBA literal
decoding still costs about 2.2 times the control's time. Their literal payloads
are 1.11% and 2.33% larger after the outer fallback.

Simple images deserve particular attention. In the third formulation, `smooth`
grows from 11,712 to 15,001 accounted bytes; `edges_alpha` grows from 996 to 1,809.
`nyquist_checker` shrinks from 133 to 128. `random` changes from 295,006 to 294,996,
but its stored literal data is untouched: this is not a demonstration of fast
noise encoding by the new backend. Aggregate percentages conceal these cases.

## What the experiment establishes

Removing redundant symbol events helps more than only tuning their arithmetic.
Word refill improves the decode-time ratio, but sequence run separation reduces
both the number of symbols and their cost. Plain byte-frequency coding still
misses structure that the current backend uses.

The next experiment should encode **literal counts, prediction/copy lengths,
and distances directly**. Separate small event alphabets from integer suffix
bits, preserve the special meaning of zero distance, and avoid recreating empty
high byte planes. This should be one fixed construction, not an image-dependent
menu. It must demonstrate a benefit with all tables and framing counted.

Literal residuals need their own investigation: compare a compact deterministic
context model against the independent-symbol model, with its table storage and
decode dependencies included. The measured previous-byte entropy estimate is
only an optimistic diagnostic. It excludes model transmission and does not
prove that a practical context coder wins. Neither a larger model nor generic
match search should be added solely to chase that estimate.

Both improvements remain hypotheses. The new implementation has not earned a
place in the shipping codec.

## Exactness and scope

Both native rANS formulations pass 102 deterministic cases, including empty and
short inputs, full alphabets, sparse runs, random symbols and maximum input size.
An independent Python scalar inverse checks 96 emitted cases per formulation.
All three probes reconstruct every tested entropy frame exactly, and recreate
the original Zstandard frames byte for byte. The released source hashes and
222 published evidence records still pass `tools/check_evidence.py`.

These are ordinary valid-input exactness checks. They do not establish an
automated memory integrity review. No new-format CNG image has been emitted:
replacement file sizes are accounting estimates, not complete alternative
container round trips. Frozen files omit failed compression attempts on
stored-literal bands, so those attempts are absent from these timings.

Coder input is bounded by 2,097,160 bytes. Coding uses caller-supplied disjoint
input/output storage, no heap allocation inside the coder, and a 16 KiB table
for the word decoder. The experiment harness owns additional raw, encoded and
restoration buffers; its memory use is not a new shipping-codec workspace bound.
Sequence run separation currently admits at most 1 MiB of raw sequence bytes.

## Reproduce and inspect

Build this directory separately; the public library does not include it:

```sh
cmake -S experiments/entropy -B .build/entropy -DCMAKE_BUILD_TYPE=Release
cmake --build .build/entropy
.build/entropy/cng_entropy_checks .build/entropy/vectors.bin
python3 experiments/entropy/oracle.py .build/entropy/vectors.bin
.build/entropy/cng_entropy_checks_words .build/entropy/vectors_words.bin
python3 experiments/entropy/oracle.py .build/entropy/vectors_words.bin
python3 experiments/entropy/summarize.py
```

Use the configured M4 build helper and non-Apple toolchain on the owner's
machine. The corpus runner accepts its private research root explicitly:

```sh
python3 experiments/entropy/run.py --corpus /path/to/cng-research \
  --binary .build/entropy/cng_entropy_probe_runs \
  --output .build/entropy/new-results.json
```

The runner refuses to overwrite reports. `evidence/*.json.gz` preserves all
samples, frame statistics, file identities, binary hashes and measured source
hashes, without distributing external images. The private corpus is needed to
rerun its measurements; the deterministic coder checks need no external images.
`byte_reference/` and `word_reference/` retain the changed harness files needed
to recover earlier source identities. They are provenance snapshots, not extra
production implementations. `summarize.py` verifies every recorded source hash
against the current files or those snapshots before presenting totals.
