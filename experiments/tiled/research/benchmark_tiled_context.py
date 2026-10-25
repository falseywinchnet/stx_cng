"""Benchmark of the tiled context-coded reference (research/tiled_context_codec.py) on the phase corpus and the Noto
fixtures: complete-file bytes split into tables, mosaic, index and tile payloads, and exact decode of every image.
WebP, OptiPNG, PNG9, v3 and CNGPHI08 sizes come from the earlier records (results/tiled_zstd_benchmark_med.json for
WebP and the zstd construction it is compared with). Sizes only.
    python -B -m research.benchmark_tiled_context [--workers 4] [--limit N]   -> results/tiled_context_benchmark.json"""
import argparse, json
from multiprocessing import Pool
import numpy as np
from research import tiled_context_codec as K
from research import tiled_zstd_codec as T
from research import benchmark_tiled_zstd as B


def one(task):
    group, name, img = task
    z = T.Zstd(); detail = {}
    data = K.encode(img, z=z, detail=detail)
    exact = bool(np.array_equal(K.decode(data, z=z), img))
    return dict(group=group, name=name, w=int(img.shape[1]), h=int(img.shape[0]), c=int(img.shape[2]), bytes=len(data), exact=exact, **detail)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--workers", type=int, default=4); ap.add_argument("--limit", type=int, default=0); a = ap.parse_args()
    B.LIMIT = a.limit
    earlier = {(r["group"], r["name"]): r for r in json.load(open("results/tiled_zstd_benchmark_med.json"))["rows"]}
    rows = []
    with Pool(a.workers) as pool:
        for r in pool.imap_unordered(one, B.items()):
            r["webp"] = earlier[(r["group"], r["name"])]["webp"]; r["zstd_construction"] = earlier[(r["group"], r["name"])]["bytes"]
            rows.append(r)
            print(f"{r['group']:20s} {r['name']:36s} bytes={r['bytes']:8d} tables={r['tables']:6d} mosaic={r['mosaic']:6d} webp={r['webp']:8d} zstd={r['zstd_construction']:8d} exact={r['exact']}", flush=True)
            json.dump(dict(scope=__doc__, codec=K.__doc__, rows=rows), open("results/tiled_context_benchmark.json", "w"), indent=1)
    print("\nall exact:", all(r["exact"] for r in rows), "|", len(rows), "images")
    B.table(rows)
    print("\n| set | n | this / zstd construction | tables % of file | mosaic % of file |")
    for label, groups in list(B.SETS.items()) + [("all", None)]:
        rr = [r for r in rows if groups is None or r["group"] in groups]
        if not rr: continue
        tot = sum(r["bytes"] for r in rr)
        print(f"| {label} | {len(rr)} | {tot / sum(r['zstd_construction'] for r in rr):.3f} | {100 * sum(r['tables'] for r in rr) / tot:.1f} | {100 * 2 * sum(r['mosaic'] for r in rr) / tot:.1f} |")
    print("DONE")
