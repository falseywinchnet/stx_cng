"""Decoder-work axis of the tradeoff harness, for the x86-64 pod: deterministic cachegrind instruction and first-level
data-miss counts of a one-thread decode, and threaded frame decode times, for the grid points base128, base255 and
anchor128 on a fixed image set. Usage (in the staged job directory): python3 -B -m research.tradeoff_pod"""
import json, platform, re, shutil, subprocess
from pathlib import Path
from research import phase_corpus, tradeoff_variants

IMAGES = [("reference_fixture", "dog-half"), ("archive_photographs", "kodim23"), ("reference_fixture", "smooth-half"), ("guiforms_capture", "checked_list_box_screen"),
          ("research_noisy", "noise8"), ("archive_structured", "tile_16_phase_0"), ("noto_faces", "emoji_u1f600.png")]
POINTS = [("base", 128), ("base", 255), ("anchor", 128)]


def main():
    folder = Path(".build/tradeoff"); paths = tradeoff_variants.generate(folder)
    cxx = shutil.which("g++") or shutil.which("clang++") or "c++"
    drivers = {}
    for name, source in paths.items():
        binary = folder / name / "phase_driver"
        subprocess.run([cxx, "-std=c++20", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Inative/phase", str(source), "native/phase/phase_driver.cpp", "-o", str(binary), "-pthread"], check=True)
        drivers[name] = str(binary.resolve())
    corpus = {(c["group"], c["name"]): c["rgb"] for c in phase_corpus.corpus()}
    for kind in ("faces", "mixed"):
        for n, img, _ in phase_corpus.noto(kind): corpus[("noto_" + kind, n)] = img
    valgrind = shutil.which("valgrind")
    report = dict(platform=platform.platform(), compiler=subprocess.check_output([cxx, "--version"], text=True).splitlines()[0], valgrind=valgrind, images=[], points={})
    work = folder / "work"; work.mkdir(exist_ok=True)
    for group, name in IMAGES:
        img = corpus[(group, name)]; H, W, C = img.shape; raw = work / "in.rgb"; raw.write_bytes(img.tobytes())
        row = dict(group=group, name=name, w=W, h=H, c=C, points={})
        for kernel, side in POINTS:
            d = drivers[kernel]; out = work / f"{kernel}{side}.cngphi"
            subprocess.run([d, "encode-side", str(W), str(H), str(C), str(raw), str(out), str(side)], check=True, capture_output=True)
            point = dict(bytes=out.stat().st_size)
            if valgrind:
                r = subprocess.run([valgrind, "--tool=cachegrind", "--cache-sim=yes", "--cachegrind-out-file=/dev/null", d, "decode", str(out), str(work / "back.rgb")], capture_output=True, text=True)
                ir = re.search(r"I\s+refs:\s+([\d,]+)", r.stderr); d1 = re.search(r"D1\s+misses:\s+([\d,]+)", r.stderr)
                point["ir_per_pixel"] = int(ir.group(1).replace(",", "")) / (W * H) if ir else None
                point["d1_miss_per_pixel"] = int(d1.group(1).replace(",", "")) / (W * H) if d1 else None
                assert (work / "back.rgb").read_bytes() == img.tobytes()
            for threads in (1, 2, 4):
                # the frames mode encodes with the default side; use it for side 128 points, and the time mode's serial decode for 255
                if side == 128:
                    r = subprocess.run([d, "frames", str(W), str(H), str(C), str(raw), "5", str(threads)], capture_output=True, text=True, check=True)
                    point[f"ns_per_pixel_{threads}"] = float(re.search(r"decode_ns_per_pixel_min ([\d.]+)", r.stdout).group(1))
                elif threads == 1:
                    r = subprocess.run([d, "time", str(W), str(H), str(C), str(raw), "5"], capture_output=True, text=True, check=True)
                    point["ns_per_pixel_1_default_side"] = float(re.search(r"decode_ns_per_pixel_min ([\d.]+)", r.stdout).group(1))
            row["points"][f"{kernel}{side}"] = point
            print(group, name, kernel, side, point, flush=True)
        report["images"].append(row)
    for kernel, side in POINTS:
        key = f"{kernel}{side}"; vals = [r["points"][key] for r in report["images"]]
        summary = {}
        for field in ("ir_per_pixel", "d1_miss_per_pixel", "ns_per_pixel_1", "ns_per_pixel_2", "ns_per_pixel_4"):
            xs = sorted(v[field] for v in vals if v.get(field) is not None)
            if xs: summary[field] = xs[len(xs) // 2]
        report["points"][key] = summary
    Path("results").mkdir(exist_ok=True)
    Path("results/tradeoff_pod.json").write_text(json.dumps(report, indent=1) + "\n")
    print("DONE", json.dumps(report["points"]))


if __name__ == "__main__":
    main()
