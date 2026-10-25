"""Where the bits go in the match-stage reference (research/tiled_match_codec.py) for a few images: code length of
literal symbols, length symbols, length extra bits, distance symbols and distance extra bits, from the image's own
quantized tables (-log2 of the table probability; extra bits as stored). Sizes only.
    python -B -m research.diagnose_match_costs"""
import math
import numpy as np
from research import phase_corpus
from research import tiled_match_codec as M
from research.tiled_context_codec import normalize, quantize
from research.tiled_zstd_codec import block_means, tile_grid

NAMES = ("retained_smooth_half", "surface_256_noise_0", "kodim23", "checked_list_box_screen")
imgs = {c["name"]: c["rgb"] for c in phase_corpus.corpus() if c["name"] in NAMES}
for name in NAMES:
    img = imgs[name]
    parts = [M.tile_ops(img[y0:y1, x0:x1])[0] for (y0, y1, x0, x1) in tile_grid(img.shape[1], img.shape[0])] + [M.tile_ops(block_means(img))[0]]
    counts = np.zeros((M.CONTEXTS, M.GREEN_SYMBOLS), np.int64)
    for ops in parts:
        for kind, a, b in ops:
            if kind == "s": counts[a, b] += 1
    freqs = [normalize(quantize(counts[k])) for k in range(M.CONTEXTS)]
    cost = dict(literals=0.0, length_symbols=0.0, length_extra=0.0, distance_symbols=0.0, distance_extra=0.0)
    distance_kinds = dict(repeat=0, special=0, coded=0)
    for ops in parts:
        after = None
        for kind, a, b in ops:
            if kind == "s":
                bits = -math.log2(freqs[a][b] / M.TOTAL)
                if a == M.DISTANCE:
                    cost["distance_symbols"] += bits; after = "distance"
                    distance_kinds["repeat" if b < M.NREPEAT else "special" if b < M.NREPEAT + M.NSPECIAL else "coded"] += 1
                elif a < M.RED0 and b >= 256: cost["length_symbols"] += bits; after = "length"
                else: cost["literals"] += bits; after = None
            else:
                cost["length_extra" if after == "length" else "distance_extra"] += a
    total = sum(cost.values())
    print(f"{name} ({img.shape[1]}x{img.shape[0]}): {total / 8:,.0f} bytes of code; " + ", ".join(f"{k} {100 * v / total:.0f}%" for k, v in cost.items()) + f"; distances {distance_kinds}")
