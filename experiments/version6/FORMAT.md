# V6 stream (CNGPHI05)

This is an experimental representation, not the published CNG grammar. The
production v3 decoder is unchanged and does not accept these streams. V6 is a
single-pass, raster-order, phase-aware causal codec: every context is
recomputed by the decoder from already-decoded samples, there is no parse, no
transmitted table and no encoder choice between alternatives.

## Container

| Field | Bytes | Meaning |
| --- | --- | --- |
| Signature | 8 | ASCII `CNGPHI05` |
| Width | 4 | big-endian u32, ≥ 1 |
| Height | 4 | big-endian u32, ≥ 1 |
| Channels | 1 | 3 (RGB8) or 4 (RGBA8) |
| Payload | rest | adaptive binary range-coder bytes, five flush bytes at the end |

Width × height × channels must not exceed 2³¹ samples. There is no checksum,
no chunking and no collection framing; those belong to the CSP/CNG container
when the representation is attached to it. Bytes read past the end of the
payload are zero.

## Per-pixel construction

For each pixel in raster order, with K = 6, PB = 8, GATE = 5, CLEAN_T = 2:

1. **Activity.** err = Σ |MED residual| of the six causal neighbours at
   offsets (rows above, columns left) (0,1) (1,0) (1,1) (1,−1) (0,2) (2,0)
   that lie inside the image; act = bin(err) with edges 0,1,2,3,5,9,17,33.
2. **Repetition lookup.** Key = (L, U, UL, UR) samples of every channel,
   −1 outside the image, packed nine bits each (−1 → 0x1FF), little-end
   first, seven components per 64-bit word, three words. Hash: h =
   0x9E3779B97F4A7C15; for each word h ^= word; h *= 0xBF58476D1CE4E5B9;
   h ^= h >> 29 (mod 2⁶⁴). Slot = h mod capacity; fingerprint = h >> 35.
   Capacity is the smallest power of two with 2·capacity ≥ pixels, clamped
   to [2⁶, 2²²]. Hit iff the slot's fingerprint equals the key's; repeat
   mode iff hit and confidence ≥ 1.
3. **Predictions.** MED(L, U, UL) per plane with JPEG-LS boundary rules
   (row above the image is 0; left of the image is U; UL and UR fall back
   to U when unavailable).
4. **Phase context.** On the cleaned plane S (sample where |residual| < 2,
   prediction otherwise) the three window moments m0 = ΣS, m1 = Σdx·S,
   m2 = Σ(−dy)·S over the causal window (rows y−t..y, columns x−l..x+r,
   current row only to the left; t, l, r = min(6, distance to the edge))
   give the Q24 plane extrapolation c0·m0 + c1·m1 + c2·m2 with the fixed
   coefficient table indexed by (t, l, r); the phase is
   ⌊(8·(extrapolation − pred·2²⁴) + step·2²³) / (step·2²⁴)⌋ clipped to
   ±16 (RGB mode, step 1) or ±3 (opponent mode, step 1 + ⌊act/2⌋);
   "no taps" when t = l = 0.
5. **Modes.** Repeat mode codes each channel's residual against the stored
   colour. Otherwise act < GATE codes G, R, B in that order against MED with
   contexts (act, phase, alpha context, G residual sign for R and B); act ≥
   GATE lifts the pixel to the modular opponent planes (A = R−G, T = G+⌊A/2⌋,
   Bc = B−T, Y = T+⌊(Bc+(A&1)+1)/3⌋, all wrapped to 8 bits) and codes Y, A,
   Bc against MED on those planes with contexts (act, offset, magnitude bins
   of the earlier planes). Alpha, when present, is coded first.
6. **Residual binarisation.** Zigzag of the wrapped 8-bit residual; zero
   flag (fine context), sign (sign context), exp-Golomb prefix bins
   (coarse context × min(i, 15)), adaptive leading suffix bit, remaining
   suffix bits bypass.
7. **Coder.** LZMA-style 32-bit range coder; 16-bit probabilities updated by
   p += ((target − p)·⌊65536/(n+2)⌋) >> 16 with n capped at 30 and p
   clamped to [64, 65472].
8. **State update.** Store the sample, the cleaned sample, the error
   energy, the lifted planes and their cleaned copies; store (colour,
   min(confidence+1, 3)) in the table slot when the lookup hit with the same
   colour, else (colour, 0).

The exact reference is `research/phase_codec.py` in the research tree; the
native kernel is byte-identical to it on the retained vectors and corpus.
