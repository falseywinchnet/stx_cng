"""Benchmark of the tiled context-coded reference with whole-pixel and per-plane matches (research/tiled_plane_codec.py) on the phase
corpus and the Noto fixtures: complete-file bytes, tables and mosaic share, matches and matched pixels, and exact
decode of every image. WebP, OptiPNG, PNG9, v3 and CNGPHI08 sizes, the zstd construction and the context-coded
reference without matches come from their earlier records. Sizes only.
    python -B -m research.benchmark_tiled_plane [--workers 4] [--limit N]   -> results/tiled_plane_benchmark.json"""
import argparse, json
from multiprocessing import Pool
import numpy as np
from research import tiled_plane_codec as M
from research import tiled_zstd_codec as T
from research import benchmark_tiled_zstd as B


def one(task):
    group, name, img = task
    z = T.Zstd(); detail = {}
    data = M.encode(img, z=z, detail=detail)
    exact = bool(np.array_equal(M.decode(data, z=z), img))
    return dict(group=group, name=name, w=int(img.shape[1]), h=int(img.shape[0]), c=int(img.shape[2]), bytes=len(data), exact=exact, **detail)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--workers", type=int, default=4); ap.add_argument("--limit", type=int, default=0); a = ap.parse_args()
    B.LIMIT = a.limit
    zstd = {(r["group"], r["name"]): r for r in json.load(open("results/tiled_zstd_benchmark_med.json"))["rows"]}
    ctx = {(r["group"], r["name"]): r for r in json.load(open("results/tiled_context_benchmark.json"))["rows"]}
    first = {(r["group"], r["name"]): r for r in json.load(open("results/tiled_cache_benchmark_m4.json"))["rows"]}
    rows = []
    with Pool(a.workers) as pool:
        for r in pool.imap_unordered(one, B.items()):
            key = (r["group"], r["name"])
            r["webp"] = zstd[key]["webp"]; r["zstd_construction"] = zstd[key]["bytes"]; r["context_only"] = ctx[key]["bytes"]; r["previous"] = first[key]["bytes"]
            rows.append(r)
            print(f"{r['group']:20s} {r['name']:36s} bytes={r['bytes']:8d} context_only={r['context_only']:8d} zstd={r['zstd_construction']:8d} webp={r['webp']:8d} tables={r['tables']} whole={r["whole_matches"]} cached={r['cached']} plane_matches={r["plane_matches"]} exact={r['exact']}", flush=True)
            json.dump(dict(scope=__doc__, codec=M.__doc__, rows=rows), open("results/tiled_plane_benchmark.json", "w"), indent=1)
    print("\nall exact:", all(r["exact"] for r in rows), "|", len(rows), "images")
    B.table(rows)
    print("\n| set | n | this / CNGM0004 | this / context only | this / zstd construction | cached pixels % | tables % of file |")
    for label, groups in list(B.SETS.items()) + [("all", None)]:
        rr = [r for r in rows if groups is None or r["group"] in groups]
        if not rr: continue
        tot = sum(r["bytes"] for r in rr)
        print(f"| {label} | {len(rr)} | {tot / sum(r['previous'] for r in rr):.3f} | {tot / sum(r['context_only'] for r in rr):.3f} | {tot / sum(r['zstd_construction'] for r in rr):.3f} | "
              f"{100 * sum(r['cached'] for r in rr) / sum(r['w'] * r['h'] for r in rr):.1f} | {100 * sum(r['tables'] for r in rr) / tot:.1f} |")
    print("DONE")
