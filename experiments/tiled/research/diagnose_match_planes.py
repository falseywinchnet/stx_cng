import numpy as np
from research import phase_corpus
from research import tiled_match_codec as M
from research.tiled_zstd_codec import tile_grid

def greedy(P, n, w, minlen=4, chain=16):
    heads = {}; i = 0; matches = covered = 0; lengths = []
    special = M.specials(w)
    while i < n:
        best = 0
        cands = [d for d in special if 1 <= d <= i]
        if i + 4 <= n:
            c = heads.get(tuple(P[i:i + 4]))
            if c: cands += [i - j for j in reversed(c[-chain:])]
        for d in cands:
            if P[i] != P[i - d]: continue
            L = 1
            while i + L < n and P[i + L] == P[i + L - d]: L += 1
            best = max(best, L)
        step = best if best >= minlen else 1
        if best >= minlen: matches += 1; covered += best; lengths.append(best)
        for j in range(i, i + step):
            if j + 4 <= n: heads.setdefault(tuple(P[j:j + 4]), []).append(j)
        i += step
    return matches, covered, (np.mean(lengths) if lengths else 0)

imgs = {c["name"]: c["rgb"] for c in phase_corpus.corpus() if c["name"] in ("retained_smooth_half", "surface_256_noise_0", "kodim23")}
for name, img in imgs.items():
    tot = dict(joint=[0, 0, 0], planes=[0, 0, 0])
    for (y0, y1, x0, x1) in tile_grid(img.shape[1], img.shape[0]):
        r, cls, q = M.analyse(img[y0:y1, x0:x1]); n = r.shape[0]; w = x1 - x0
        packed = ((r + 128).astype(np.int64) @ (np.int64(256) ** np.arange(r.shape[1], dtype=np.int64))).tolist()
        m, c, _ = greedy(packed, n, w); tot["joint"][0] += m; tot["joint"][1] += c
        for ch in range(3):
            m, c, _ = greedy(r[:, ch].tolist(), n, w); tot["planes"][0] += m; tot["planes"][1] += c
    px = img.shape[0] * img.shape[1]
    print(f"{name}: joint matches {tot['joint'][0]} covering {tot['joint'][1] / px:.2f} of pixels, mean {tot['joint'][1] / max(1, tot['joint'][0]):.1f} px | "
          f"per plane: {tot['planes'][0]} matches covering {tot['planes'][1] / (3 * px):.2f} of samples, mean {tot['planes'][1] / max(1, tot['planes'][0]):.1f}")
