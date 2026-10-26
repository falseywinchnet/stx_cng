"""The code length table of the native CNG encoder: for every 12-bit frequency f in 1..4096, the code length in bits
that the Python reference's cost-aware parse uses, -numpy.log2(f / 4096) as research/tiled_plane_codec.bit_costs
computes it, written as exact hexadecimal float literals so the native parse makes the same decisions on every
platform. Writes src/code_lengths.hpp (run from the repository root with experiments/tiled on PYTHONPATH).
    PYTHONPATH=experiments/tiled python3 -B -m research.generate_cng_code_lengths"""
from pathlib import Path
import numpy as np

lines = ["// Generated from the format's Python reference: code_length[f] = -log2(f / 4096) for every 12-bit frequency f, as",
         "// the reference computes it (numpy float64), in exact hexadecimal form. Entry 0 is unused.",
         "#pragma once", "#include <array>", "", "namespace stx_cng::detail {", "", "inline constexpr std::array<double, 4097> kCodeLength = {"]
values = ["0x0p+0"] + [float(-np.log2(f / 4096)).hex() for f in range(1, 4097)]
values = [v.replace("-0x0.0p+0", "0x0p+0").replace("0x0.0p+0", "0x0p+0") for v in values]
for k in range(0, len(values), 6):
    lines.append("    " + ", ".join(values[k:k + 6]) + ",")
lines += ["};", "", "}  // namespace stx_cng::detail", ""]
Path("src/code_lengths.hpp").write_text("\n".join(lines))
print("wrote src/code_lengths.hpp")
