"""What zstd finds on the smooth-graphics images that the context coder misses: the same MED residuals (colour gate
applied, zigzag bytes) laid out in different ways per 128x128 tile and compressed by zstd level 19, against the
CNGM0004 file. Layouts: planes concatenated (the zstd construction), pixels interleaved, each plane in its own frame,
and green with red and blue always coded as differences from green. Also the share of pixels whose three residuals
are equal, and the residual histogram mass at 0 and +-1. Sizes only.
    python -B -m research.diagnose_smooth_layouts"""
import json
import numpy as np
from research import phase_corpus
from research import tiled_match_codec as M
from research.tiled_zstd_codec import GATE, Zstd, tile_grid, wrap, zigzag

z = Zstd()
now = {r["name"]: r["bytes"] for r in json.load(open("results/tiled_cache_benchmark.json"))["rows"]}
for c in phase_corpus.corpus():
    if c["group"] not in ("research_smooth", "archive_smooth"): continue
    img = c["rgb"]; sizes = dict(planes=0, interleaved=0, separate=0, differenced=0, differenced_planes=0)
    equal = n = zero = one = 0
    for (y0, y1, x0, x1) in tile_grid(img.shape[1], img.shape[0]):
        r, cls, _ = M.analyse(img[y0:y1, x0:x1]); r = r[:, :3]
        busy = cls >= GATE; g = r[:, 1]
        gated = np.stack([np.where(busy, wrap(r[:, 0] - g), r[:, 0]), g, np.where(busy, wrap(r[:, 2] - g), r[:, 2])], 1)
        diff = np.stack([wrap(r[:, 0] - g), g, wrap(r[:, 2] - g)], 1)
        zg = zigzag(gated).astype(np.uint8); zd = zigzag(diff).astype(np.uint8)
        sizes["planes"] += len(z.compress(b"".join(zg[:, p].tobytes() for p in (1, 0, 2))))
        sizes["interleaved"] += len(z.compress(zg.tobytes()))
        sizes["separate"] += sum(len(z.compress(zg[:, p].tobytes())) for p in range(3))
        sizes["differenced"] += len(z.compress(zd.tobytes()))
        sizes["differenced_planes"] += len(z.compress(b"".join(zd[:, p].tobytes() for p in (1, 0, 2))))
        equal += int(((r[:, 0] == g) & (r[:, 2] == g)).sum()); n += len(g)
        zero += int((r == 0).sum()); one += int((np.abs(r) == 1).sum())
    print(f"{c['name']:28s} {img.shape[1]}x{img.shape[0]} CNGM0004 {now[c['name']]:7d} | zstd: " + " ".join(f"{k} {v}" for k, v in sizes.items())
          + f" | all three equal {equal / n:.2f}, residual 0 {zero / (3 * n):.2f}, +-1 {one / (3 * n):.2f}")
