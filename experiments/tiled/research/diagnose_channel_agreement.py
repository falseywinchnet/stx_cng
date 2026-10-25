"""How often the MED residuals of the three colour channels agree, per image group: the share of pixels with
r_red == r_green and r_blue == r_green, overall and among quiet pixels (activity class below the colour gate), and
how often a pixel's agreement matches its left neighbour's. Sizes only.
    python -B -m research.diagnose_channel_agreement"""
import numpy as np
from research import phase_corpus
from research import tiled_match_codec as M
from research.tiled_zstd_codec import GATE, tile_grid

groups = {}
for c in phase_corpus.corpus():
    groups.setdefault(c["group"], []).append(c["rgb"])
for kind in ("faces", "mixed"):
    groups["noto_" + kind] = [img for _, img, _ in phase_corpus.noto(kind)]
for g, imgs in sorted(groups.items()):
    agree = quiet_agree = quiet = same_as_left = n = 0
    for img in imgs:
        for (y0, y1, x0, x1) in tile_grid(img.shape[1], img.shape[0]):
            r, cls, _ = M.analyse(img[y0:y1, x0:x1]); w = x1 - x0
            a = (r[:, 0] == r[:, 1]) & (r[:, 2] == r[:, 1]); q = cls < GATE
            agree += a.sum(); quiet += q.sum(); quiet_agree += (a & q).sum(); n += a.size
            grid = a.reshape(-1, w); same_as_left += (grid[:, 1:] == grid[:, :-1]).sum() + grid.shape[0]
    print(f"{g:22s} agree {agree / n:.2f}  among quiet {quiet_agree / max(1, quiet):.2f}  quiet share {quiet / n:.2f}  agreement equals left neighbour's {same_as_left / n:.2f}")
