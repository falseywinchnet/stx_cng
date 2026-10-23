"""Enumerate owned-array capacities over admitted widths at maximum height."""
import json


def bound(size):
    assert 0 <= size <= 2097160
    return size + 4 * ((size + 131071) // 131072) + 5


def run():
    maxima = {name: {'bytes': 0} for name in ('encode', 'decode_stream', 'decode_into', 'entropy_input')}
    admitted = 0
    for channels in (3, 4):
        for width in range(1, 65537):
            padded_width = ((width + 127) // 128) * 128
            height = min((4194304 // padded_width // 128) * 128, 65536 - padded_width, 65536)
            if height < 128:
                continue
            admitted += 1
            stride = width * channels
            rows = (1048576 - channels) // stride
            assert rows > 0
            samples = min(rows, height) * stride
            sequences = samples // channels // 4 + 1
            raw_sequences = sequences * 12
            packed = ((samples // channels + 1) // 2) * 2 * channels
            separated = 2 * packed + 4
            payload = 13 + bound(raw_sequences) + samples
            common = stride + separated + payload + sequences * 12
            sizes = {
                'encode': common + 2 * samples + max(raw_sequences, bound(separated)) + 65536 * 4 + 656400,
                'decode_stream': common + 2 * samples + max(raw_sequences, packed) + 131088,
                'decode_into': common + samples + max(raw_sequences, packed) + 131088,
                'entropy_input': max(raw_sequences, separated),
            }
            for name, size in sizes.items():
                if size > maxima[name]['bytes']:
                    maxima[name] = {'bytes': size, 'width': width, 'height': height, 'channels': channels}
    print(json.dumps({'admitted_width_channels': admitted, 'maxima': maxima,
                     'scope': 'Owned capacities only; excludes caller storage, stack, allocator and process overhead.'}, indent=2))


if __name__ == '__main__':
    run()
