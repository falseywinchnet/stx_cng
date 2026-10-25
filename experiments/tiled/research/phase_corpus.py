"""Corpus access for the phase-coding research scripts. Everything resolves inside the repository so the same
commands run on the M4 mirror: data/phase_corpus_manifest.json (written by research/prepare_phase_corpus.py)
pins the 123-image expanded RGB corpus, the dog/smooth reference fixture and GUI.Forms captures by RGB SHA-256
with their recorded v3/PNG9 sizes; Noto emoji, Kodak and TESTIMAGES are read from data/ directly."""
import json, io, hashlib
from pathlib import Path
import numpy as np
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
def load_png(path):
    im = Image.open(path)
    if im.mode not in ("RGB", "RGBA"):
        im = im.convert("RGBA" if ("A" in im.mode or im.mode == "P") else "RGB")
    return np.array(im)
def manifest():
    return json.load(open(ROOT / "data" / "phase_corpus_manifest.json"))["entries"]
def corpus(groups=None, verify=False):
    """Yield dicts(name, group, rgb, v3, png9) for the pinned corpus entries."""
    out = []
    for e in manifest():
        if groups is not None and e["group"] not in groups: continue
        arr = load_png(ROOT / e["file"])
        if verify and hashlib.sha256(arr.tobytes()).hexdigest() != e["rgb_sha256"]:
            raise RuntimeError("corpus image does not match its pinned hash: " + e["file"])
        out.append(dict(name=e["name"], group=e["group"], rgb=arr, v3=e["v3_bytes"], png9=e["png9_bytes"]))
    return out
def expanded():
    """The 123-image expanded RGB corpus with v3 and PNG9 reference sizes."""
    return [c for c in corpus() if c["group"] not in ("reference_fixture", "guiforms_capture")]
def png_bytes(arr):
    b = io.BytesIO(); Image.fromarray(arr).save(b, format="PNG", compress_level=9, optimize=True); return len(b.getvalue())
def jpeg100_bytes(arr):
    im = Image.fromarray(arr[:, :, :3]) if arr.shape[2] == 4 else Image.fromarray(arr)
    b = io.BytesIO(); im.save(b, format="JPEG", quality=100, subsampling=0, optimize=True); return len(b.getvalue())
def noto(kind="faces"):
    m = json.load(open(ROOT / "data" / "noto_atlas_manifest.json"))
    key = {"faces": "faces32", "mixed": "mixed64"}[kind]
    return [(Path(img["file"]).name, load_png(ROOT / img["file"]), img["png_bytes"]) for img in m["images"] if img["group"] == key]
def kodak():
    return [(p.name, load_png(p)) for p in sorted((ROOT / "data").glob("kodim*.png"))]
def testimages(limit=None):
    if not (ROOT / 'data' / 'testimages').exists(): return []
    ps = sorted((ROOT / "data" / "testimages" / "canonical").glob("*.png"))
    return [(p.name, load_png(p)) for p in (ps[:limit] if limit else ps)]
