"""Four-axis tradeoff harness for the frame codec: size, progressive decode, recoverability under damage, and (from
the pod job, research/tradeoff_pod.py) decoder work. One fixed construction per grid point:

    kernel  : base (CNGPHI07) | anchor (CNGPHX1A: later frames start from the top-left frame's table and models)
    side    : 128 | 255 (frame side)
    mosaic  : none | m16 | m16+m4   (redundant coarse layers: block means over 16x16 and 4x4 cells, each coded as a
              small CNGPHI07 image by the base kernel and stored twice; the first layer is the progressive picture
              and the fill for frames lost to damage)

Subcommands (run on the Mini with the existing Python; sizes only, no timing):
    build                      generate the kernel variants and build both drivers
    sizes   [--workers 4]      per image: stream bytes and frame payload lengths for every kernel x side, mosaic bytes
                               and fill PSNR, OptiPNG reference bytes            -> results/tradeoff_sizes.json
    damage  [--workers 4]      burst damage of 1%, 5%, 20% of the file, three seeds, every grid point: fraction of
                               pixels exact, fraction filled from the mosaic, PSNR  -> results/tradeoff_damage.json
    report                     the front: one row per grid point                  -> results/tradeoff_front.json, stdout
Damage model: bursts of 512 random bytes, ceil(fraction * length / 512) of them at uniformly random positions over
the whole file (index, payloads, mosaic copies alike), at the same file fractions for both kernels. A frame is lost when its checksum fails; the anchor kernel
loses every frame when the anchor frame is lost; the mosaic survives when either copy decodes."""
import argparse, json, math, shutil, struct, subprocess, tempfile, zlib
from multiprocessing import Pool
from pathlib import Path
import numpy as np
from research import phase_corpus
from research import tradeoff_variants

FOLDER = Path(".build/tradeoff")
LIMIT = 0
KERNELS = ("base", "anchor")
SIDES = (128, 255)
MOSAICS = ("none", "m16", "m16+m4")
LEVELS = (0.01, 0.05, 0.20)
SEEDS = (1, 2, 3)
BURST = 512


def compiler():
    for c in ("/opt/homebrew/opt/llvm/bin/clang++", shutil.which("clang++"), shutil.which("g++"), "c++"):
        if c and Path(c).exists() or (c and shutil.which(c)):
            return c
    return "c++"


def build():
    paths = tradeoff_variants.generate(FOLDER)
    cxx = compiler()
    for name, source in paths.items():
        binary = FOLDER / name / "phase_driver"
        subprocess.run([cxx, "-std=c++20", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Inative/phase", str(source), "native/phase/phase_driver.cpp", "-o", str(binary)], check=True)
        print("built", binary, "with", cxx)
    return subprocess.check_output([cxx, "--version"], text=True).splitlines()[0]


def driver(kernel):
    return str((FOLDER / kernel / "phase_driver").resolve())


def items():
    out = [(c["group"], c["name"], c["rgb"]) for c in phase_corpus.corpus()]
    for kind in ("faces", "mixed"):
        for n, img, _ in phase_corpus.noto(kind):
            out.append(("noto_" + kind, n, img))
    return out[:LIMIT] if LIMIT else out


def run(args):
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def encode(kernel, img, side, folder, tag):
    raw = folder / f"{tag}.rgb"; out = folder / f"{tag}.cngphi"
    raw.write_bytes(img.tobytes())
    rc, _ = run([driver(kernel), "encode-side", str(img.shape[1]), str(img.shape[0]), str(img.shape[2]), str(raw), str(out), str(side)])
    if rc != 0:
        raise RuntimeError(f"encode failed {kernel} {side} {tag}")
    return out.read_bytes()


def decode_exact(kernel, data, img, folder, tag):
    src = folder / f"{tag}.in"; back = folder / f"{tag}.out"; src.write_bytes(data)
    rc, _ = run([driver(kernel), "decode", str(src), str(back)])
    return rc == 0 and back.read_bytes() == img.tobytes()


def frame_lengths(data):
    """Payload lengths in raster order from the (front) directory; frames from the header fields."""
    W, H, C, side = struct.unpack(">IIBB", data[8:18])
    n = -(-W // side) * -(-H // side)
    return [struct.unpack(">I", data[22 + 8 * i:26 + 8 * i])[0] for i in range(n)], (W, H, C, side)


def block_means(img, cell):
    H, W, C = img.shape
    h, w = -(-H // cell), -(-W // cell)
    pad = np.zeros((h * cell, w * cell, C), np.float64); pad[:H, :W] = img
    count = np.zeros((h * cell, w * cell, 1), np.float64); count[:H, :W] = 1
    s = pad.reshape(h, cell, w, cell, C).sum(axis=(1, 3)); k = count.reshape(h, cell, w, cell, 1).sum(axis=(1, 3))
    return np.rint(s / np.maximum(k, 1)).astype(np.uint8)


def upsample(mosaic, cell, shape):
    H, W, _ = shape
    return np.repeat(np.repeat(mosaic, cell, axis=0), cell, axis=1)[:H, :W]


def psnr(a, b):
    err = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    return 99.0 if err == 0 else 10 * math.log10(255.0 ** 2 / err)


def sizes_one(task):
    group, name, img = task
    with tempfile.TemporaryDirectory(prefix="tradeoff-") as t:
        folder = Path(t)
        row = dict(group=group, name=name, w=int(img.shape[1]), h=int(img.shape[0]), c=int(img.shape[2]), streams={}, mosaic={})
        for kernel in KERNELS:
            for side in SIDES:
                data = encode(kernel, img, side, folder, f"{kernel}{side}")
                lengths, _ = frame_lengths(data)
                row["streams"][f"{kernel}{side}"] = dict(bytes=len(data), frames=lengths, exact=decode_exact(kernel, data, img, folder, f"d{kernel}{side}"))
        for cell in (16, 4):
            m = block_means(img, cell)
            data = encode("base", m, 128, folder, f"m{cell}")
            row["mosaic"][f"m{cell}"] = dict(bytes=len(data), cells=[int(m.shape[1]), int(m.shape[0])], fill_psnr=psnr(upsample(m, cell, img.shape), img), exact=decode_exact("base", data, m, folder, f"dm{cell}"))
        return row


def sizes(workers):
    opti = {}
    for r in json.load(open("results/optipng_reference.json"))["rows"]:
        opti[(r["group"], r["name"])] = dict(optipng=r["optipng_o7"], png=r["png_original"], png9=r.get("png9"))
    rows = []
    with Pool(workers) as pool:
        for row in pool.imap_unordered(sizes_one, items()):
            row.update(opti.get((row["group"], row["name"]), {}))
            rows.append(row)
            print(f"{row['group']:20s} {row['name']:36s} " + " ".join(f"{k}={v['bytes']}" for k, v in row["streams"].items()) + f" m16={row['mosaic']['m16']['bytes']} m4={row['mosaic']['m4']['bytes']}", flush=True)
            json.dump(dict(scope=__doc__, rows=rows), open("results/tradeoff_sizes.json", "w"), indent=1)
    print("DONE", len(rows))


def layout(main, mosaics):
    """File layout for a grid point: main stream, then each mosaic stream twice. Returns bytes and segment spans."""
    parts = [("main", main)]
    for key, data in mosaics:
        parts.append((key + "/a", data)); parts.append((key + "/b", data))
    spans = {}; out = bytearray(); pos = 0
    for key, data in parts:
        spans[key] = (pos, pos + len(data)); out += data; pos += len(data)
    return bytes(out), spans


def damage_one(task):
    group, name, img = task
    results = []
    with tempfile.TemporaryDirectory(prefix="tradeoff-") as t:
        folder = Path(t)
        mosaics = {}
        for cell in (16, 4):
            m = block_means(img, cell); mosaics[cell] = (m, encode("base", m, 128, folder, f"m{cell}"))
        H, W, C = img.shape; pixels = H * W
        for kernel in KERNELS:
            for side in SIDES:
                main = encode(kernel, img, side, folder, f"{kernel}{side}")
                lengths, (_, _, _, s) = frame_lengths(main)
                across = -(-W // s)
                rects = [(min(s, H - (i // across) * s), min(s, W - (i % across) * s)) for i in range(len(lengths))]
                for mosaic in MOSAICS:
                    use = [] if mosaic == "none" else [("m16", mosaics[16][1])] + ([("m4", mosaics[4][1])] if mosaic == "m16+m4" else [])
                    data, spans = layout(main, use)
                    for level in LEVELS:
                        for seed in SEEDS:
                            rng = np.random.default_rng([seed, int(level * 1000), side, MOSAICS.index(mosaic), zlib.crc32(name.encode())])   # same burst positions (as file fractions) for both kernels
                            damaged = bytearray(data)
                            bursts = max(1, math.ceil(level * len(data) / BURST))
                            for _ in range(bursts):
                                at = int(rng.random() * max(1, len(data) - BURST))
                                damaged[at:at + BURST] = rng.integers(0, 256, BURST, dtype=np.uint8).tobytes()
                            a, b = spans["main"]
                            (folder / "dmg.cngphi").write_bytes(bytes(damaged[a:b]))
                            rc, out = run([driver(kernel), "salvage", str(folder / "dmg.cngphi"), str(folder / "dmg.rgb"), str(folder / "dmg.mask")])
                            if rc == 0:
                                mask = np.frombuffer((folder / "dmg.mask").read_bytes(), np.uint8).astype(bool)
                                restored = np.frombuffer((folder / "dmg.rgb").read_bytes(), np.uint8).reshape(H, W, C).copy()
                            else:
                                mask = np.zeros(len(lengths), bool); restored = np.zeros_like(img)
                            exact_pixels = sum(r[0] * r[1] for r, ok in zip(rects, mask) if ok)
                            # the finest surviving mosaic fills the lost frames
                            fill = None
                            for key, cell in (("m4", 4), ("m16", 16)):
                                if key not in [u[0] for u in use]: continue
                                for copy in ("a", "b"):
                                    p, q = spans[key + "/" + copy]
                                    (folder / "mos.cngphi").write_bytes(bytes(damaged[p:q]))
                                    rc2, _ = run([driver("base"), "decode", str(folder / "mos.cngphi"), str(folder / "mos.rgb")])
                                    if rc2 == 0 and (folder / "mos.rgb").read_bytes() == mosaics[cell][0].tobytes():
                                        fill = (key, upsample(mosaics[cell][0], cell, img.shape)); break
                                if fill: break
                            lost = np.ones((H, W), bool)
                            for i, ok in enumerate(mask):
                                if ok:
                                    y0, x0 = (i // across) * s, (i % across) * s
                                    lost[y0:y0 + rects[i][0], x0:x0 + rects[i][1]] = False
                            if fill is not None:
                                restored[lost] = fill[1][lost]
                            else:
                                restored[lost] = 128
                            results.append(dict(kernel=kernel, side=side, mosaic=mosaic, level=level, seed=seed, file_bytes=len(data), bursts=bursts,
                                                frames=len(lengths), frames_ok=int(mask.sum()), exact_fraction=exact_pixels / pixels,
                                                filled_fraction=(0.0 if fill is None else 1 - exact_pixels / pixels), fill=(fill[0] if fill else None),
                                                psnr=psnr(restored, img), header_lost=(rc != 0)))
    return dict(group=group, name=name, w=W, h=H, c=C, results=results)


def damage(workers):
    rows = []
    with Pool(workers) as pool:
        for row in pool.imap_unordered(damage_one, items()):
            rows.append(row)
            base = [r for r in row["results"] if r["kernel"] == "base" and r["side"] == 128 and r["mosaic"] == "m16"]
            print(f"{row['group']:20s} {row['name']:36s} base128+m16 exact@1/5/20%: " + " ".join(f"{np.mean([r['exact_fraction'] for r in base if r['level'] == l]):.2f}" for l in LEVELS) + " psnr: " + " ".join(f"{np.mean([r['psnr'] for r in base if r['level'] == l]):.1f}" for l in LEVELS), flush=True)
            json.dump(dict(scope=__doc__, burst=BURST, levels=LEVELS, seeds=SEEDS, rows=rows), open("results/tradeoff_damage.json", "w"), indent=1)
    print("DONE", len(rows))


def report():
    """The front. Progressive and damage scores are given over all images and over the multi-frame images (at least
    four frames at side 128: 104 of 236), because a single-frame image has nothing to contain and one 512-byte burst
    in a 300-byte file destroys it whatever the construction; the pixel-weighted exact fraction is given as well."""
    sizes_rows = json.load(open("results/tradeoff_sizes.json"))["rows"]
    damage_rows = json.load(open("results/tradeoff_damage.json"))["rows"] if Path("results/tradeoff_damage.json").exists() else []
    pod = json.load(open("results/tradeoff_pod.json")) if Path("results/tradeoff_pod.json").exists() else None
    multi = {(r["group"], r["name"]) for r in sizes_rows if len(r["streams"]["base128"]["frames"]) >= 4}
    opti_total = sum(r["optipng"] for r in sizes_rows)
    front = []
    for kernel in KERNELS:
        for side in SIDES:
            for mosaic in MOSAICS:
                key = f"{kernel}{side}"
                def extra_bytes(r):
                    return (2 * r["mosaic"]["m16"]["bytes"] if mosaic != "none" else 0) + (2 * r["mosaic"]["m4"]["bytes"] if mosaic == "m16+m4" else 0)
                def point_bytes(r):
                    return r["streams"][key]["bytes"] + extra_bytes(r)
                total = sum(point_bytes(r) for r in sizes_rows)
                groups = {}
                for r in sizes_rows:
                    g = groups.setdefault(r["group"], [0, 0]); g[0] += point_bytes(r); g[1] += r["optipng"]
                prog = dict(first=[], first_psnr=[], second=[], second_psnr=[], half=[], half_multi=[])
                for r in sizes_rows:
                    st = r["streams"][key]; n = len(st["frames"]); index = 22 + 8 * n + 4
                    fb = point_bytes(r); m16 = r["mosaic"]["m16"]["bytes"]; m4 = r["mosaic"]["m4"]["bytes"]
                    if mosaic == "none":
                        prog["first"].append((index + st["frames"][0]) / fb)
                    else:
                        prog["first"].append((index + m16) / fb); prog["first_psnr"].append(r["mosaic"]["m16"]["fill_psnr"])
                    if mosaic == "m16+m4":
                        prog["second"].append((index + 2 * m16 + m4) / fb); prog["second_psnr"].append(r["mosaic"]["m4"]["fill_psnr"])
                    W, H = r["w"], r["h"]; across = -(-W // side); acc = index + extra_bytes(r); covered = 0
                    for i, L in enumerate(st["frames"]):
                        acc += L; covered += min(side, H - (i // across) * side) * min(side, W - (i % across) * side)
                        if covered * 2 >= W * H: break
                    prog["half"].append(acc / fb)
                    if (r["group"], r["name"]) in multi: prog["half_multi"].append(acc / fb)
                dmg = {}
                for level in LEVELS:
                    sel = [(row, x) for row in damage_rows for x in row["results"] if x["kernel"] == kernel and x["side"] == side and x["mosaic"] == mosaic and x["level"] == level]
                    if not sel: continue
                    xs = [x for _, x in sel]; ms = [x for row, x in sel if (row["group"], row["name"]) in multi]
                    pixels = [row["w"] * row["h"] for row, _ in sel]
                    dmg[str(level)] = dict(exact=float(np.mean([x["exact_fraction"] for x in xs])), exact_pixel_weighted=float(np.average([x["exact_fraction"] for x in xs], weights=pixels)),
                                           exact_multi=float(np.mean([x["exact_fraction"] for x in ms])), filled_multi=float(np.mean([x["filled_fraction"] for x in ms])),
                                           psnr_multi=float(np.median([x["psnr"] for x in ms])), header_lost=float(np.mean([x["header_lost"] for x in xs])), header_lost_multi=float(np.mean([x["header_lost"] for x in ms])))
                speed = pod.get("points", {}).get(key) if pod else None
                front.append(dict(kernel=kernel, side=side, mosaic=mosaic, bytes=total, ratio_optipng=total / opti_total, groups={g: v[0] / v[1] for g, v in groups.items()},
                                  exact_all=all(r["streams"][key]["exact"] for r in sizes_rows),
                                  first_picture_fraction=float(np.median(prog["first"])), first_picture_psnr=(float(np.median(prog["first_psnr"])) if prog["first_psnr"] else None),
                                  second_picture_fraction=(float(np.median(prog["second"])) if prog["second"] else None), second_picture_psnr=(float(np.median(prog["second_psnr"])) if prog["second_psnr"] else None),
                                  half_pixels_fraction=float(np.median(prog["half"])), half_pixels_fraction_multi=float(np.median(prog["half_multi"])), damage=dmg, speed=speed))
    json.dump(dict(scope=__doc__, report=report.__doc__, optipng_total=opti_total, multi_frame_images=len(multi), front=front), open("results/tradeoff_front.json", "w"), indent=1)
    def cell(v, f="{:.3f}"):
        return "-" if v is None else f.format(v)
    print(f"multi-frame images: {len(multi)} of {len(sizes_rows)}")
    print("| kernel | side | mosaic | bytes / OptiPNG | photos | tiles | emoji | first picture: file % / PSNR | second: file % / PSNR | half pixels: file % (multi) | exact @1% all / px-weighted / multi | exact @5% multi | exact @20% multi | filled @5% multi | PSNR @5% multi | Ir/px 1T | ns/px 1T | ns/px 4T |")
    for p in front:
        d = p["damage"]; sp = p["speed"] or {}; g = p["groups"]
        cols = [p["kernel"], str(p["side"]), p["mosaic"], cell(p["ratio_optipng"]), cell(g.get("archive_photographs")), cell(g.get("archive_structured"), "{:.2f}"),
                cell((g.get("noto_faces", 0) * 32 + g.get("noto_mixed", 0) * 64) / 96 if "noto_faces" in g else None),
                f"{100 * p['first_picture_fraction']:.1f} / {cell(p['first_picture_psnr'], '{:.1f}')}",
                "-" if p["second_picture_fraction"] is None else f"{100 * p['second_picture_fraction']:.1f} / {cell(p['second_picture_psnr'], '{:.1f}')}",
                f"{100 * p['half_pixels_fraction_multi']:.1f}"]
        if "0.01" in d:
            cols.append(f"{d['0.01']['exact']:.3f} / {d['0.01']['exact_pixel_weighted']:.3f} / {d['0.01']['exact_multi']:.3f}")
            cols += [cell(d[str(l)]["exact_multi"]) if str(l) in d else "-" for l in LEVELS[1:]]
            cols += [cell(d["0.05"]["filled_multi"]), cell(d["0.05"]["psnr_multi"], "{:.1f}")]
        else:
            cols += ["-"] * 5
        cols += [cell(sp.get("ir_per_pixel"), "{:.0f}"), cell(sp.get("ns_per_pixel_1"), "{:.0f}"), cell(sp.get("ns_per_pixel_4"), "{:.0f}")]
        print("| " + " | ".join(cols) + " |")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("command", choices=["build", "sizes", "damage", "report"]); ap.add_argument("--workers", type=int, default=4); ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    LIMIT = a.limit
    if a.command == "build": build()
    elif a.command == "sizes": sizes(a.workers)
    elif a.command == "damage": damage(a.workers)
    else: report()
