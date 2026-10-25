"""What the per-image tables of the match-stage reference consist of, on the 96 Noto emoji and a few larger images:
compressed table bytes, used contexts, symbols listed (up to the last used) and symbols actually used per context,
symbols used once, and the code length of the payload, under the cost-aware parse. Sizes only.
    python -B -m research.diagnose_table_cost"""
import math
import numpy as np
from research import phase_corpus
from research import tiled_match_codec as M
from research.tiled_context_codec import normalize, quantize
from research.tiled_zstd_codec import Zstd, block_means, tile_grid

z = Zstd()
def study(img):
    tiles = [img[y0:y1, x0:x1] for (y0, y1, x0, x1) in tile_grid(img.shape[1], img.shape[0])] + [block_means(img)]
    parts = [M.tile_ops(t) for t in tiles]
    costs = M.bit_costs(M.count_symbols(parts))
    parts = [M.tile_ops_costed(t, costs) for t in tiles]
    counts = M.count_symbols(parts)
    qs = [quantize(counts[k]) for k in range(M.CONTEXTS)]
    blob = M.serialize_tables(qs, z)
    used = [int((counts[k] > 0).sum()) for k in range(M.CONTEXTS)]
    listed = [len(q) for q in qs]; once = int((counts == 1).sum())
    payload = 0.0
    for k in range(M.CONTEXTS):
        f = normalize(qs[k])
        for s, c in enumerate(counts[k][:len(f)]):
            if c: payload += -c * math.log2(f[s] / M.TOTAL)
    return dict(table=len(blob), contexts=sum(1 for u in used if u), listed=sum(listed), used=sum(used), once=once, payload=payload / 8)

emoji = [img for kind in ("faces", "mixed") for _, img, _ in phase_corpus.noto(kind)]
rows = [study(img) for img in emoji]
mean = {k: np.mean([r[k] for r in rows]) for k in rows[0]}
print("emoji mean: " + ", ".join(f"{k} {v:,.1f}" for k, v in mean.items()))
for c in phase_corpus.corpus():
    if c["name"] in ("kodim23", "checked_list_box_screen"):
        r = study(c["rgb"]); print(c["name"] + ": " + ", ".join(f"{k} {v:,.1f}" for k, v in r.items()))
