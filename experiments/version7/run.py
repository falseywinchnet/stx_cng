"""Check the V7 probe against the retained bit-exact vectors (evidence/vectors/manifest.json): every vector must
encode to the recorded bytes and decode to the recorded samples. Optional --time runs the probe's in-process timing
mode on each vector-sized input (not a benchmark; the M4 results are in RESULTS.md).
    python3 -B run.py --binary <v7_probe> --vectors evidence/vectors [--time 5]"""
import argparse, json, subprocess, sys
from pathlib import Path
if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("--binary", required=True); ap.add_argument("--vectors", required=True); ap.add_argument("--time", type=int, default=0)
    a = ap.parse_args(); folder = Path(a.vectors); manifest = json.load(open(folder / "manifest.json"))
    if manifest.get("signature") != "CNGPHI08": print("manifest signature", manifest.get("signature"), "is not CNGPHI08"); sys.exit(1)
    failures = 0
    for v in manifest["vectors"]:
        run = subprocess.run([a.binary, "check", str(v["width"]), str(v["height"]), str(v["channels"]), str(folder / (v["name"] + ".rgb")), str(folder / (v["name"] + ".cngphi"))], capture_output=True, text=True)
        print(f"{v['name']:18s} {run.stdout.strip()} rc={run.returncode}")
        failures += run.returncode != 0
        if a.time:
            timed = subprocess.run([a.binary, "time", str(v["width"]), str(v["height"]), str(v["channels"]), str(folder / (v["name"] + ".rgb")), str(a.time)], capture_output=True, text=True)
            print("   ", timed.stdout.strip())
    print("FAIL" if failures else "PASS", failures, "failures of", len(manifest["vectors"]))
    sys.exit(1 if failures else 0)
