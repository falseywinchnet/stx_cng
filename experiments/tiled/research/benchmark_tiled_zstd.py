"""Benchmark of the tiled transform + zstd reference (research/tiled_zstd_codec.py) on the phase corpus and the Noto
fixtures: complete-file bytes, exact decode of every image, and the reference sizes it sits between, all on the same
images: lossless WebP (Pillow/libwebp, method 6, quality 100, exact), OptiPNG -o7 and PNG level 9 (from
results/optipng_reference.json), CSP v3 (manifest) and the CNGPHI08 frame codec (results/native_phase_hashes_arm64_v7e.json).

    python -B -m research.benchmark_tiled_zstd main [--workers 4] [--limit N]
        default construction: encode, decode, exactness, WebP size  -> results/tiled_zstd_benchmark.json
    python -B -m research.benchmark_tiled_zstd ablations [--workers 4]
        each component switched off in turn: sizes for every image, exact decode for images up to 20,000 pixels
                                                                    -> results/tiled_zstd_ablations.json
Sizes only; the Python decoder's time is not a measurement of the construction."""
import argparse, io, json, sys
from multiprocessing import Pool
from pathlib import Path
import numpy as np
from PIL import Image
from research import phase_corpus
from research import tiled_zstd_codec as T

SETS = {
    "photographs": ("archive_photographs",), "screen captures": ("guiforms_capture",),
    "smooth graphics": ("research_smooth", "archive_smooth"),
    "mixed": ("research_mixed", "reference_fixture", "research_photo", "archive_composite"),
    "noise": ("research_noisy", "archive_noise_mixed"), "emoji": ("noto_faces", "noto_mixed"),
    "periodic tiles": ("archive_structured", "research_structured"),
}
ABLATIONS = {
    "default": {}, "no phase-rounded plane": dict(plane=False), "no phase sign flip": dict(flip=False),
    "no stream split": dict(split=False), "no colour gate": dict(gate=False), "no dictionary": dict(dictionary=False),
}
LIMIT = 0


def items():
    out = [(c["group"], c["name"], c["rgb"]) for c in phase_corpus.corpus()]
    for kind in ("faces", "mixed"):
        for n, img, _ in phase_corpus.noto(kind):
            out.append(("noto_" + kind, n, img))
    return out[:LIMIT] if LIMIT else out


def webp_bytes(img):
    b = io.BytesIO()
    Image.fromarray(img, "RGBA" if img.shape[2] == 4 else "RGB").save(b, format="WEBP", lossless=True, quality=100, method=6, exact=True)
    data = b.getvalue()
    back = np.array(Image.open(io.BytesIO(data)).convert("RGBA" if img.shape[2] == 4 else "RGB"))
    return len(data), bool(np.array_equal(back, img))


CFG = dict(T.CONFIG)
OUT = "results/tiled_zstd_benchmark.json"


def main_one(task):
    group, name, img, cfg = task   # the configuration travels with the task: spawned workers re-import defaults
    z = T.Zstd()
    data = T.encode(img, cfg=cfg, z=z)
    exact = bool(np.array_equal(T.decode(data, cfg=cfg, z=z), img))
    wb, wexact = webp_bytes(img)
    tiles = len(T.tile_grid(img.shape[1], img.shape[0]))
    shared = 0
    for t in T.parse(data)[3][1:]:
        shared += t["payload"][0] & 1
    return dict(group=group, name=name, w=int(img.shape[1]), h=int(img.shape[0]), c=int(img.shape[2]), bytes=len(data), exact=exact,
                tiles=tiles, tiles_sharing=shared, webp=wb, webp_exact=wexact)


def references():
    opt = {(r["group"], r["name"]): r for r in json.load(open("results/optipng_reference.json"))["rows"]}
    v8 = {(r["group"], r["name"]): r["bytes"] for r in json.load(open("results/native_phase_hashes_arm64_v7e.json"))["rows"]}
    v3 = {(e["group"], e["name"]): e.get("v3_bytes") for e in phase_corpus.manifest()}
    return opt, v8, v3


def table(rows, key="bytes"):
    opt, v8, v3 = references()
    print(f"| set | n | this / OptiPNG | WebP / OptiPNG | this / WebP | this / PNG9 | this / v3 (n) | this / CNGPHI08 | smaller than WebP | smaller than OptiPNG |")
    for label, groups in list(SETS.items()) + [("all", None)]:
        rr = [r for r in rows if groups is None or r["group"] in groups]
        if not rr: continue
        k = lambda r: (r["group"], r["name"])
        this = sum(r[key] for r in rr); o = sum(opt[k(r)]["optipng_o7"] for r in rr); wb = sum(r["webp"] for r in rr)
        p9r = [r for r in rr if opt[k(r)].get("png9")]; v3r = [r for r in rr if v3.get(k(r))]
        p9 = sum(opt[k(r)]["png9"] for r in p9r); t9 = sum(r[key] for r in p9r)
        v3b = sum(v3[k(r)] for r in v3r); t3 = sum(r[key] for r in v3r)
        cg = sum(v8[k(r)] for r in rr)
        print(f"| {label} | {len(rr)} | {this / o:.3f} | {wb / o:.3f} | {this / wb:.3f} | {(t9 / p9) if p9 else float('nan'):.3f} | "
              f"{(t3 / v3b) if v3b else float('nan'):.3f} ({len(v3r)}) | {this / cg:.3f} | {sum(r[key] < r['webp'] for r in rr)} of {len(rr)} | "
              f"{sum(r[key] < opt[k(r)]['optipng_o7'] for r in rr)} of {len(rr)} |")


def main(workers):
    rows = []
    with Pool(workers) as pool:
        for r in pool.imap_unordered(main_one, [item + (dict(CFG),) for item in items()]):
            rows.append(r)
            print(f"{r['group']:20s} {r['name']:36s} bytes={r['bytes']:8d} webp={r['webp']:8d} exact={r['exact']} webp_exact={r['webp_exact']} sharing={r['tiles_sharing']}/{r['tiles'] - 1}", flush=True)
            json.dump(dict(scope=__doc__, codec=T.__doc__, config=CFG, rows=rows), open(OUT, "w"), indent=1)
    print("\nall exact:", all(r["exact"] for r in rows), "| WebP all exact:", all(r["webp_exact"] for r in rows), "|", len(rows), "images")
    table(rows)
    print("DONE")


def ablation_one(task):
    group, name, img = task
    z = T.Zstd(); out = dict(group=group, name=name, w=int(img.shape[1]), h=int(img.shape[0]), sizes={}, exact={})
    for label, change in ABLATIONS.items():
        cfg = dict(T.CONFIG, **change)
        data = T.encode(img, cfg=cfg, z=z)
        out["sizes"][label] = len(data)
        if img.shape[0] * img.shape[1] <= 20000:
            out["exact"][label] = bool(np.array_equal(T.decode(data, cfg=cfg, z=z), img))
    return out


def ablations(workers):
    rows = []
    with Pool(workers) as pool:
        for r in pool.imap_unordered(ablation_one, items()):
            rows.append(r)
            print(f"{r['group']:20s} {r['name']:36s} " + " ".join(f"{v}" for v in r["sizes"].values()), flush=True)
            json.dump(dict(scope=__doc__, ablations=ABLATIONS, rows=rows), open("results/tiled_zstd_ablations.json", "w"), indent=1)
    checked = [v for r in rows for v in r["exact"].values()]
    print(f"\nexact decodes checked: {len(checked)}, all exact: {all(checked)}")
    print("| set | " + " | ".join(ABLATIONS) + " |")
    for label, groups in list(SETS.items()) + [("all", None)]:
        rr = [r for r in rows if groups is None or r["group"] in groups]
        base = sum(r["sizes"]["default"] for r in rr)
        print(f"| {label} | " + " | ".join(f"{sum(r['sizes'][a] for r in rr) / base:.3f}" for a in ABLATIONS) + " |")
    print("DONE")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("command", choices=["main", "ablations"]); ap.add_argument("--workers", type=int, default=4); ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--config", default="{}", help="JSON overrides of the codec CONFIG for the main run"); ap.add_argument("--out", default=OUT)
    a = ap.parse_args(); LIMIT = a.limit; CFG.update(json.loads(a.config)); OUT = a.out
    main(a.workers) if a.command == "main" else ablations(a.workers)
