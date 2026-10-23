"""Extract existing exact CNG vectors to expose noise and long-distance behavior."""
import hashlib
import json
import struct
from pathlib import Path


def run():
    source = Path('.build/decode-tables/vectors.bin')
    output = Path('.build/zxc-comparison/controls')
    output.mkdir(exist_ok=False)
    data = source.read_bytes()
    sizes = [0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 255, 256, 257, 65534, 65535, 65536,
             65537, 131071, 131072, 131073, 262143, 262144, 262145]
    periods = [0, 1, 2, 3, 7, 257, 65535, 65536, 131072]
    position = 0
    records = []
    with (output / 'frames.bin').open('wb') as bundle:
        for size in sizes:
            for period in periods:
                for disturbances in range(2):
                    raw_size, encoded_size = struct.unpack_from('<II', data, position)
                    position += 8
                    assert raw_size == size
                    raw = data[position:position + size]
                    position += size
                    encoded = data[position:position + encoded_size]
                    position += encoded_size
                    if size not in (131072, 262145) or period not in (0, 1, 7, 65536, 131072):
                        continue
                    records.append({'ordinal': len(records), 'size': size, 'period': period,
                                    'disturbances': disturbances,
                                    'raw_sha256': hashlib.sha256(raw).hexdigest(),
                                    'frame_sha256': hashlib.sha256(encoded).hexdigest()})
                    bundle.write(struct.pack('<I', encoded_size))
                    bundle.write(encoded)
    assert position == len(data) and len(records) == 20
    report = {'source_sha256': hashlib.sha256(data).hexdigest(), 'cases': records,
              'bundle_sha256': hashlib.sha256((output / 'frames.bin').read_bytes()).hexdigest()}
    (output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Extracted', len(records), 'noise, repetition and distance controls')


if __name__ == '__main__':
    run()
