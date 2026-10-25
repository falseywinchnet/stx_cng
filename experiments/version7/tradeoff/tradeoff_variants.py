"""Generate the research kernel variants of the tradeoff harness from native/phase/phase_codec.cpp.

anchor: every frame after the first starts from the repetition table and adaptive models that the top-left frame
left behind (signature CNGPHX1A). Frames then depend on the anchor frame only: a parallel decoder decodes the anchor
once per worker; a damaged anchor loses the image. This is a fixed construction, not a mode: the base kernel and
the variant are separate programs writing distinguishable streams. Every textual replacement must match exactly
once, so the generator fails loudly when the base kernel changes under it."""
from pathlib import Path

BASE = Path("native/phase/phase_codec.cpp")


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"variant patch expects exactly one match, found {text.count(old)}: {old[:72]!r}")
    return text.replace(old, new)


def anchor_variant(text):
    text = replace_once(text, 'constexpr std::array<std::uint8_t, 8> kSignature = {0x43, 0x4E, 0x47, 0x50, 0x48, 0x49, 0x30, 0x37};  // "CNGPHI07"',
                        'constexpr std::array<std::uint8_t, 8> kSignature = {0x43, 0x4E, 0x47, 0x50, 0x48, 0x58, 0x31, 0x41};  // "CNGPHX1A" (anchor-seeded research variant)')
    text = replace_once(text, "struct Neighbours { std::int32_t left = 0;",
                        '''// Anchor seed: the repetition table and adaptive models as the top-left frame left them; every later frame starts from it.
struct Seed { std::vector<std::uint32_t> tags; std::vector<std::uint32_t> colours; std::uint64_t mask = 0; Models models; bool valid = false; };
void take_seed(const Workspace& w, Seed& s) { s.tags = w.repeats.tags; s.colours = w.repeats.colours; s.mask = w.repeats.mask; s.models = w.models; s.valid = true; }
void apply_seed(Workspace& w, const Seed* s) {
    if (s == nullptr || !s->valid) { return; }
    w.repeats.tags = s->tags; w.repeats.colours = s->colours; w.repeats.mask = s->mask; w.models = s->models;
}
struct Neighbours { std::int32_t left = 0;''')
    text = replace_once(text, "Status encode_frame(Workspace& workspace, const std::uint8_t* samples, Dimensions image, FrameRect r, std::vector<std::uint8_t>& payload, std::vector<SymbolTrace>* trace) {\n    payload.clear();\n    Dimensions frame; frame.width = r.width; frame.height = r.height; frame.channels = image.channels;\n    workspace.prepare(frame);\n    workspace.place(image.width, r.x0, r.y0, image.width);",
                        "Status encode_frame(Workspace& workspace, const std::uint8_t* samples, Dimensions image, FrameRect r, std::vector<std::uint8_t>& payload, std::vector<SymbolTrace>* trace, const Seed* seed) {\n    payload.clear();\n    Dimensions frame; frame.width = r.width; frame.height = r.height; frame.channels = image.channels;\n    workspace.prepare(frame);\n    apply_seed(workspace, seed);\n    workspace.place(image.width, r.x0, r.y0, image.width);")
    text = replace_once(text, "Status decode_frame_payload(Workspace& workspace, std::span<const std::uint8_t> payload, Dimensions image, FrameRect r, std::uint8_t* destination) {\n    Dimensions frame; frame.width = r.width; frame.height = r.height; frame.channels = image.channels;\n    workspace.prepare(frame);\n    workspace.place(image.width, r.x0, r.y0, image.width);",
                        "Status decode_frame_payload(Workspace& workspace, std::span<const std::uint8_t> payload, Dimensions image, FrameRect r, std::uint8_t* destination, const Seed* seed) {\n    Dimensions frame; frame.width = r.width; frame.height = r.height; frame.channels = image.channels;\n    workspace.prepare(frame);\n    apply_seed(workspace, seed);\n    workspace.place(image.width, r.x0, r.y0, image.width);")
    # encoder: seed from frame 0
    text = replace_once(text, "    Workspace workspace;   // one per call; prepare() refills it for each frame\n    for (std::uint32_t index = 0; index < frames; ++index) {\n        Status status = encode_frame(workspace, samples.data(), dimensions, frame_rect(dimensions, side, index), payloads[index], trace);\n        if (status != Status::ok) { return status; }\n    }",
                        "    Workspace workspace;   // one per call; prepare() refills it for each frame\n    Seed seed;\n    for (std::uint32_t index = 0; index < frames; ++index) {\n        Status status = encode_frame(workspace, samples.data(), dimensions, frame_rect(dimensions, side, index), payloads[index], trace, index == 0 ? nullptr : &seed);\n        if (status != Status::ok) { return status; }\n        if (index == 0) { take_seed(workspace, seed); }\n    }")
    # single-frame decode: decode the anchor first when needed
    text = replace_once(text, "    if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }\n    Workspace workspace;\n    return decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data());\n}",
                        "    if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }\n    Workspace workspace;\n    Seed seed;\n    if (index > 0) {\n        std::span<const std::uint8_t> anchor = stream.subspan(offsets[0], lengths[0]);\n        if (crc32_of(anchor) != checksums[0]) { return Status::bad_checksum; }\n        Status first = decode_frame_payload(workspace, anchor, dimensions, frame_rect(dimensions, side, 0), destination.data(), nullptr);\n        if (first != Status::ok) { return first; }\n        take_seed(workspace, seed);\n    }\n    return decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data(), index == 0 ? nullptr : &seed);\n}")
    # range decode: anchor once per worker
    text = replace_once(text, "    Workspace workspace;\n    for (std::uint32_t index = first; index < offsets.size(); index += stride) {\n        std::span<const std::uint8_t> payload = stream.subspan(offsets[index], lengths[index]);\n        if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }\n        Status status = decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data());\n        if (status != Status::ok) { return status; }\n    }\n    return Status::ok;\n}",
                        "    Workspace workspace;\n    Seed seed;\n    {\n        std::span<const std::uint8_t> anchor = stream.subspan(offsets[0], lengths[0]);\n        if (crc32_of(anchor) != checksums[0]) { return Status::bad_checksum; }\n        Status status = decode_frame_payload(workspace, anchor, dimensions, frame_rect(dimensions, side, 0), destination.data(), nullptr);\n        if (status != Status::ok) { return status; }\n        take_seed(workspace, seed);\n    }\n    for (std::uint32_t index = first; index < offsets.size(); index += stride) {\n        if (index == 0) { continue; }\n        std::span<const std::uint8_t> payload = stream.subspan(offsets[index], lengths[index]);\n        if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }\n        Status status = decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data(), &seed);\n        if (status != Status::ok) { return status; }\n    }\n    return Status::ok;\n}")
    # whole decode
    text = replace_once(text, "    Workspace workspace;   // one per call; prepare() refills it for each frame\n    for (std::uint32_t index = 0; index < offsets.size(); ++index) {\n        std::span<const std::uint8_t> payload = stream.subspan(offsets[index], lengths[index]);\n        if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }\n        Status status = decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data());\n        if (status != Status::ok) { return status; }\n    }\n    return Status::ok;\n}",
                        "    Workspace workspace;   // one per call; prepare() refills it for each frame\n    Seed seed;\n    for (std::uint32_t index = 0; index < offsets.size(); ++index) {\n        std::span<const std::uint8_t> payload = stream.subspan(offsets[index], lengths[index]);\n        if (crc32_of(payload) != checksums[index]) { return Status::bad_checksum; }\n        Status status = decode_frame_payload(workspace, payload, dimensions, frame_rect(dimensions, side, index), destination.data(), index == 0 ? nullptr : &seed);\n        if (status != Status::ok) { return status; }\n        if (index == 0) { take_seed(workspace, seed); }\n    }\n    return Status::ok;\n}")
    return text


VARIANTS = {"base": lambda t: t, "anchor": anchor_variant}


def generate(folder):
    """Write <folder>/<variant>/phase_codec.cpp for every variant; return {variant: path}."""
    base = BASE.read_text()
    out = {}
    for name, patch in VARIANTS.items():
        target = Path(folder) / name / "phase_codec.cpp"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(patch(base))
        out[name] = target
    return out


if __name__ == "__main__":
    import sys
    for name, path in generate(sys.argv[1] if len(sys.argv) > 1 else ".build/tradeoff").items():
        print(name, path)
