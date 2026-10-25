# Tiled transform + zstd: V6's lessons as data for a fast back end (2026-10-25)

The owner's target after the CNGPHI08 release: a PNG/WebP-class format, sized
between lossless WebP and PNG and decoding at their speed, that applies what
the phase-coding study learned to the *data* fed to a zstd-class back end, in
independent 128×128 tiles, keeping the index, mosaic and salvage features.
This records the first exact reference of that construction and what it shows.

Reference: `research/tiled_zstd_codec.py` (stream CNGZ0001; design in its
docstring). Benchmark: `research/benchmark_tiled_zstd.py` (`main` with
`--config` overrides, `ablations`). Records: `results/tiled_zstd_benchmark.json`
(default construction), `results/tiled_zstd_benchmark_med.json` (plane and
split off), `results/tiled_zstd_ablations.json`. All 236 images decode exactly
in every configuration run; lossless WebP (libwebp via Pillow, method 6,
quality 100, exact) was measured on the same images and decodes exactly too.

## 1. Construction

Per tile, per pixel: MED prediction; activity from the six causal MED-residual
energies, five classes; for quiet pixels a phase-rounded prediction (a
least-squares plane through twelve causal samples, Q8, rounded) with the
residual sign flipped by the plane's sub-integer phase; a colour gate (quiet
pixels code R, G, B residuals directly, busy pixels code G and R−G, B−G);
zigzag bytes; one stream per activity class (planes separated inside it) and
one for alpha. Each stream is a magicless zstd level-19 frame. Tile 0's raw
streams are a raw-content dictionary that later tiles use when it makes them
smaller (flag in the tile payload). Mosaic and duplicated index as in CNGPHI08;
tile 0's payload is stored a second time only when some tile shares it.

## 2. Results (complete files over OptiPNG `-o7`; lossless WebP on the same images is 0.747 overall)

| set | n | WebP | default | plane and split off | v3 = 1 (plane and split off) |
|---|---|---|---|---|---|
| photographs | 24 | 0.735 | 0.915 | 0.906 | 1.069 |
| screen captures | 14 | 0.657 | 1.155 | 0.888 | — |
| smooth graphics | 10 | 0.825 | 1.441 | 0.642 | 0.371 |
| mixed | 13 | 0.823 | 0.998 | 0.932 | 0.898 |
| noise | 23 | 0.964 | 1.139 | 1.130 | 0.844 |
| 128 px emoji | 96 | 0.795 | 1.986 | 1.488 | — |
| periodic tiles | 56 | 0.879 | 1.601 | 1.448 | 1.154 |
| all | 236 | 0.747 | 1.010 | 0.932 | 1.008 (126) |

The best configuration measured is 0.932 of OptiPNG and 1.248 of WebP: about
PNG's size and v3's size, not between WebP and PNG. It beats WebP only on
smooth graphics (0.642 against 0.825), where removing the rounding dither is
exactly what matters, and it loses heavily on emoji (1.9× WebP) and periodic
tiles (1.6× WebP).

**Component ablations** (default = 1; each column switches one component off):

| set | plane off | flip off | split off | colour gate off | dictionary off |
|---|---|---|---|---|---|
| photographs | 0.978 | 0.996 | 1.010 | 1.170 | 1.006 |
| screen captures | 0.731 | 0.968 | 0.989 | 1.066 | 1.001 |
| smooth graphics | 0.930 | 1.006 | 0.614 | 0.988 | 1.001 |
| emoji | 0.737 | 0.994 | 0.948 | 1.003 | 1.000 |
| periodic tiles | 0.955 | 0.995 | 0.946 | 0.926 | 1.124 |
| all | 0.914 | 0.991 | 0.991 | 1.105 | 1.004 |

(The ablation run predates the tile-0-copy fix; its ratios compare
configurations under the same container.)

## 3. Reading

- **The phase-rounded plane, used as the prediction, hurts** (+9% overall,
  +37% on captures, +36% on emoji). Its twelve-tap window reaches past edges
  that MED stops at, so it spreads error into flat and rendered regions. This
  repeats the Bernstein result: a surface is worth having as a *context*, and
  costs when it is subtracted.
- **Context knowledge does not reach zstd.** zstd's literal coder is order-0
  and its strength is matching inside long, contiguous, repetitive streams.
  The only way to hand it V6's conditioning is to split streams by context, and
  splitting breaks the repetition it relies on (+63% on smooth graphics). The
  sign flip by phase is worth nothing for the same reason (+0.9%).
- **What does transfer:** the colour gate (−10% overall, −15% on photographs),
  and the shared dictionary for repeated content (−11% on periodic tiles).
- **The gap to WebP is in the back end, not the transform.** WebP codes pixels
  with pixel-aware entropy codes (separate green/length, red, blue, alpha and
  distance alphabets; different codes per region; a colour cache). A
  byte-oriented general compressor cannot reproduce that, which is also why v3
  stalled at about PNG's size.

## 4. What the evidence points to

Keep the tiled container, mosaic, index and salvage, and keep MED with the
colour gate and a context derived from activity and a cheap phase sign. Replace
zstd with pixel-aware static entropy codes: one canonical code (Huffman or
rANS) per context, tables computed per image, stored at the front and again at
the end, so every tile decodes independently given them. That is the "small
shared store in the upper left, repeated" of the owner's design, applied to the
entropy tables, where it saves every tile its own table header. The earlier
static-table oracle bounds this: V6's own symbol stream under static tables cost
about 4% over adaptive coding (the phase-coding study in the research tree), so
the coding side can stay close to V6; the open question is how much of V6's
modelling the cheap context keeps. Speed is not measured here: the Python
decoder's time says nothing about the construction.

## 5. zstd's entropy stage made context-aware (stream CNGC0001)

zstd decodes all literals of a block before any output exists, so a context
taken from reconstructed pixels cannot reach its literal coder. The second
reference, `research/tiled_context_codec.py` with
`research/benchmark_tiled_context.py` (record `results/tiled_context_benchmark.json`),
keeps zstd's kind of entropy coder (static table-driven ANS: one lookup and one
multiply per symbol) but gives it one table per context and decodes each symbol
in step with reconstruction. Prediction is MED with the colour gate; contexts
are the activity class for green, class × bucket of the green residual for red
and blue (37 contexts with alpha). Tables are per image, quantized to half
powers of two, zstd-compressed and stored at the front and the back; each tile
is one rANS stream and decodes independently given them. zstd's LZ stage is
not used. All 236 images decode exactly.

| set | n | WebP | zstd construction (§2) | context-coded | tables % of file |
|---|---|---|---|---|---|
| photographs | 24 | 0.735 | 0.906 | **0.811** | 0.2 |
| screen captures | 14 | 0.657 | 0.888 | **0.815** | 0.4 |
| smooth graphics | 10 | 0.825 | 0.642 | 1.275 | 1.7 |
| mixed | 13 | 0.823 | 0.932 | 0.936 | 0.4 |
| noise | 23 | 0.964 | 1.130 | 1.064 | 0.9 |
| 128 px emoji | 96 | 0.795 | 1.488 | 1.460 | 12.3 |
| periodic tiles | 56 | 0.879 | 1.448 | 23.6 | 0.3 |
| all | 236 | 0.747 | 0.932 | 0.998 | 0.7 |
| all but the tile fixtures | 180 | 0.746 | 0.929 | **0.857** | |

(Bytes over OptiPNG `-o7`.) Contexts do what zstd's order-0 coder cannot: on
photographs and captures the context-coded files are 9–10% smaller than the
zstd construction and sit between WebP and PNG. Without a match stage, exact
repetition is paid pixel by pixel: an 8-pixel tile of noise repeated over the
image costs 213,722 bytes against 883 for zstd and 396 for WebP, and smooth
synthetic ramps lose the residual-pattern matches zstd found. Emoji are near
the zstd construction and pay 12% of their bytes for the 37 per-image tables.

The two halves are complementary: context-coded literals for what does not
repeat, matching for what does. That is zstd's own split between literals and
sequences, with the literal coder given contexts, and it is the next
construction. WebP signals a match inside its green alphabet, so the decoder's
per-pixel loop keeps a single branch between "decode a pixel" and "copy a run".

## 6. Match stage added (stream CNGM0001)

`research/tiled_match_codec.py` with `research/benchmark_tiled_match.py`
(record `results/tiled_match_benchmark.json`): the context-coded tiles of §5
with zstd's other half restored. A match copies the MED residuals of every
channel of L pixels from d pixels back inside the tile, so it covers exact
repeats and repeated rounding patterns; tiles stay independent. It is signalled
as WebP does: each green context's alphabet is 256 literals plus 30 length
codes, followed by a distance symbol from its own table (six symbols for the
offsets left, up, up-left, up-right, two left, two up; prefix codes otherwise).
The encoder parse is greedy over the six offsets and a 16-deep hash chain. All
236 images decode exactly.

| set | n | WebP | zstd construction | context only | with matches | matched pixels |
|---|---|---|---|---|---|---|
| photographs | 24 | 0.735 | 0.906 | 0.811 | **0.811** | 4% |
| screen captures | 14 | 0.657 | 0.888 | 0.815 | **0.771** | 72% |
| smooth graphics | 10 | 0.825 | 0.642 | 1.275 | 1.091 | 94% |
| mixed | 13 | 0.823 | 0.932 | 0.936 | **0.902** | 40% |
| noise | 23 | 0.964 | 1.130 | 1.064 | 1.061 | 7% |
| 128 px emoji | 96 | 0.795 | 1.488 | 1.460 | 1.468 | 85% |
| periodic tiles | 56 | 0.879 | 1.448 | 23.6 | **1.374** | 98% |
| all | 236 | 0.747 | 0.932 | 0.998 | **0.848** | 52% |

(Bytes over OptiPNG `-o7`.) Overall the construction now sits between WebP
and PNG: 0.848 of OptiPNG, 1.135 of WebP, 0.922 of v3 and 1.07 of CNGPHI08.
Repetition is back (the 8-pixel noise tile from 213,722 bytes to under 1 KB)
and captures gain 5% over context coding alone.

Two losses remain. **Smooth graphics** lose to the zstd construction (1.09
against 0.64) because their matches are many and short (26,000 matches of
mean length 9 on a 512×512 half-plane): each pays a full distance symbol,
where zstd repeats its last offsets almost for free. A repeat-last-distance
symbol is the missing piece. **Emoji** lose to WebP by 1.85×: their 38
per-image tables take 13% of each file, and their antialiased palette is
better served by WebP's colour cache than by residual matches.

## 7. Repeat offsets (stream CNGM0002)

The distance alphabet gained three repeat-distance symbols in front of the six
offsets (move-to-front, tiles start with 1, w, 2), and repeat matches are
accepted from two pixels. The first match version is frozen as
`research/tiled_match_codec_m1.py` with its record
`results/tiled_match_benchmark_m1.json`. All 236 images decode exactly. The
corpus does not move: 1.001 of the first version overall, 0.994 on smooth
graphics (0.848 of OptiPNG, 1.136 of WebP), although a synthetic ramp shrinks
by 27%.

`research.diagnose_match_costs` shows why. On `retained_smooth_half` the code
is 15% literals, 24% lengths and 61% distances, and of 26,800 matches only
2,071 use a repeat distance: 24,272 use a new, far distance with up to twelve
raw bits. The greedy parse takes the longest candidate at each pixel whatever it
costs, so a match one pixel longer at a new distance beats a cheap repeat. zstd
at level 19 parses by cost. The fix is in the encoder only: a cost-aware parse
that prices literals from the context tables and matches from their length and
distance codes. On photographs the code is 98% literals, so this is a loss
remover for smooth and synthetic content, not a general gain.
`research.diagnose_match_planes` also shows that matching each plane on its own
does not reduce the number of matches on smooth content (24,709 against
26,467), so plane-wise matching is not the cause.

## 8. Cost-aware parse (encoder only; stream CNGM0002 unchanged)

`tile_ops_costed` in `research/tiled_match_codec.py` replaces the greedy parse
as the encoder default: code lengths come from the tables of a greedy first
pass, every pixel is priced as a literal from its contexts, every candidate
match at its full length and a few shorter ones from its length and distance
codes under the repeat distances of the cheapest path reaching it, and the
cheapest path through each tile is kept (a match of 64 pixels or more is taken
at once). The greedy record is kept as `results/tiled_match_benchmark_m2_greedy.json`.
All 236 images decode exactly.

| set | n | WebP | greedy parse | cost-aware parse | cost-aware / greedy |
|---|---|---|---|---|---|
| photographs | 24 | 0.735 | 0.812 | 0.810 | 0.998 |
| screen captures | 14 | 0.657 | 0.773 | 0.758 | 0.981 |
| smooth graphics | 10 | 0.825 | 1.084 | 0.987 | 0.911 |
| mixed | 13 | 0.823 | 0.901 | 0.892 | 0.990 |
| noise | 23 | 0.964 | 1.061 | 1.060 | 0.999 |
| 128 px emoji | 96 | 0.795 | 1.472 | 1.421 | 0.965 |
| periodic tiles | 56 | 0.879 | 1.370 | 1.368 | 0.999 |
| all | 236 | 0.747 | 0.848 | **0.842** | 0.992 |

(Bytes over OptiPNG `-o7`.) The construction is now 0.842 of OptiPNG, 1.128 of
WebP, 0.919 of v3 and 1.067 of CNGPHI08. The parse removes 9% of the smooth
loss and 3.5% of the emoji loss; photographs, being 98% literals, do not move.
Smooth graphics are still 1.54× the zstd construction: on `retained_smooth_half`
57,928 bytes against 30,933. `research.diagnose_channel_agreement` finds that
the three channels' residuals agree on 56% of smooth-graphics pixels but that
agreement is not predictable from the left neighbour (equal 57% of the time,
against 87–99% on captures, emoji and tiles), so a causal agreement context does
not explain the remaining gap; it is not yet identified. Emoji still pay 13% of
their bytes for tables.

## 9. Colour cache and escaped table entries (stream CNGM0003)

`research/tiled_cache_codec.py` with `research/benchmark_tiled_cache.py`
(record `results/tiled_cache_benchmark.json`; the CNGM0002 record it is
compared with is `results/tiled_match_benchmark_m2.json`). Two additions:

- **Colour cache**, as WebP's: 256 recent colours per tile, indexed by a
  multiplicative hash of the packed samples, every reconstructed pixel written
  to its slot. A pixel found in its slot can be coded by the slot index, a third
  part of the green alphabet (256 literals, 30 lengths, 256 indices). The
  decoder recovers a cached pixel's residuals from its MED predictions, so
  matches and energies are unaffected. The cache starts empty in every tile.
- **Escaped table entries**: a symbol used at most twice in a context is not
  listed; an escape entry ends each table, and an escaped symbol follows it in
  raw bits (10 for green, 6 for distances, 8 otherwise).

All 236 images decode exactly.

| set | n | WebP | CNGM0002 | CNGM0003 | CNGM0003 / CNGM0002 |
|---|---|---|---|---|---|
| photographs | 24 | 0.735 | 0.810 | 0.810 | 1.000 |
| screen captures | 14 | 0.657 | 0.758 | 0.755 | 0.995 |
| smooth graphics | 10 | 0.825 | 0.987 | 0.954 | 0.966 |
| mixed | 13 | 0.823 | 0.892 | 0.890 | 0.997 |
| noise | 23 | 0.964 | 1.060 | 1.054 | 0.994 |
| 128 px emoji | 96 | 0.795 | 1.421 | **1.134** | 0.798 |
| periodic tiles | 56 | 0.879 | 1.368 | 1.328 | 0.970 |
| all | 236 | 0.747 | 0.842 | **0.834** | 0.991 |

(Bytes over OptiPNG `-o7`.) Emoji files fall 20% (mean 7,430 to 5,928 bytes;
WebP 4,158): the two table copies from 1,943 to 1,121 bytes and the payload
with mosaic from 5,396 to 4,715. Emoji are now 0.966 of CNGPHI08 and 11 of 96
beat OptiPNG. Overall the construction is 0.834 of OptiPNG, 1.117 of WebP and
0.917 of v3. Every screen capture now beats OptiPNG.

The tables are still 19% of an emoji file because they are stored twice
(the benchmark's "tables % of file" column counts one copy). On a one-tile
image the second copy protects against damage to the table region only, so
whether small images keep it is a containment decision, worth about 9% on
emoji.

## 10. Tables stored once on single-tile images (stream CNGM0004)

On the owner's decision, an image of a single tile stores its tables once;
multi-tile images keep both copies. `encode(..., tables_once=False)` still
writes CNGM0003, and its record is kept as `results/tiled_cache_benchmark_m3.json`.
All 236 images decode exactly in the new run.

| set | n | WebP | CNGM0003 | CNGM0004 | CNGM0004 / CNGM0003 |
|---|---|---|---|---|---|
| 128 px emoji | 96 | 0.795 | 1.134 | **1.026** | 0.905 |
| smooth graphics | 10 | 0.825 | 0.954 | 0.949 | 0.994 |
| all other sets | | | | | 0.996–1.000 |
| all | 236 | 0.747 | 0.834 | **0.831** | 0.997 |

(Bytes over OptiPNG `-o7`.) Emoji are now 1.29× WebP, 0.874 of CNGPHI08, and
30 of 96 beat OptiPNG. The price is containment on one-tile images: damage to
their table copy now makes the file undecodable (the mosaic needs the tables
too), where before the second copy repaired it. Damage to the payload still
fails only that tile, which on a one-tile image is the whole picture either way.

## 11. The smooth-graphics gap: per-plane matches (stream CNGM0005)

**Cause.** `research.diagnose_smooth_layouts` compresses the same residuals in
different layouts with zstd. On `retained_smooth_half`, planes concatenated
take 27,103 bytes and each plane compressed alone 28,232, but pixels
interleaved take 56,020, which is what the whole-pixel coder paid (55,343).
Each colour plane's residual pattern repeats with its own period; whole-pixel
matches see only the coincidences of all three, so they are short and their
distances vary. Cross-plane copies are not the cause.

**Construction** (`research/tiled_plane_codec.py`, `research/benchmark_tiled_plane.py`,
record `results/tiled_plane_benchmark.json`, compared with the CNGM0004 record
`results/tiled_cache_benchmark_m4.json`). Each plane, alpha included, can start
a match of that plane alone, signalled by 30 length codes in its context's
alphabet (for green, after the whole-pixel lengths and the cache indices),
with three repeat distances kept per plane. Whole-pixel matches and the colour
cache remain, usable when no plane is inside a match. A first version with
per-plane matches only cut the smooth half-plane by 31% but made emoji 35%,
a photograph 7% and a palette image five times larger: a repeated pixel costs
one match per plane and the cache is crowded out. The encoder therefore parses
each plane by cost over the whole tile first, shares that plane-wise cost over
the pixels, lets a whole-pixel match or a cached pixel win a pixel only where
it is cheaper, and parses each plane again over the pixels left to it. All 236
images decode exactly.

| set | n | WebP | zstd construction | CNGM0004 | CNGM0005 | CNGM0005 / CNGM0004 |
|---|---|---|---|---|---|---|
| photographs | 24 | 0.735 | 0.906 | 0.810 | 0.801 | 0.989 |
| screen captures | 14 | 0.657 | 0.888 | 0.755 | 0.744 | 0.986 |
| smooth graphics | 10 | 0.825 | 0.642 | 0.949 | **0.839** | 0.884 |
| mixed | 13 | 0.823 | 0.932 | 0.889 | 0.875 | 0.984 |
| noise | 23 | 0.964 | 1.130 | 1.050 | 1.050 | 1.000 |
| 128 px emoji | 96 | 0.795 | 1.488 | 1.026 | 1.023 | 0.997 |
| periodic tiles | 56 | 0.879 | 1.448 | 1.327 | 1.329 | 1.001 |
| all | 236 | 0.747 | 0.932 | 0.831 | **0.822** | 0.988 |

(Bytes over OptiPNG `-o7`.) Smooth graphics fall 12% and now sit level with
WebP (1.016 of it); every other set gains or holds. Overall the construction
is 0.822 of OptiPNG, 1.101 of WebP, 0.906 of v3 and 1.04 of CNGPHI08. On the
half-plane it is 43,107 bytes against 30,933 for the zstd construction, so
about half the smooth gap remains; one fixture, `smooth`, grows 10% (12,109 to
13,362), where the plane-wise route was priced cheaper than it codes. The
encoder now parses every tile several times, which a native encoder has to
account for.
