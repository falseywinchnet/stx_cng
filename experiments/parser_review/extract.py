"""Extract retained Huffman packets from the hash-pinned private CNE corpus."""
import argparse
import gzip
import hashlib
import json
import struct
import zlib
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def frames(image):
    assert image[:8] == bytes([137, 67, 78, 69, 13, 10, 26, 10])
    position = 8
    while position < len(image):
        count = struct.unpack_from('>I', image, position)[0]
        kind = image[position + 4:position + 8]
        end = position + 8 + count
        assert end + 4 <= len(image)
        assert zlib.crc32(image[position + 4:end]) == struct.unpack_from('>I', image, end)[0]
        body = image[position + 8:end]
        if kind == b'BAND':
            sequences = struct.unpack_from('>I', body, 8)[0]
            yield body[12:12 + sequences]
            if body[12 + sequences] == 1:
                yield body[13 + sequences:]
        position = end + 4
    assert position == len(image)


def packets(frame):
    if frame[0] == 0:
        assert len(frame) == 5 + struct.unpack_from('<I', frame, 1)[0]
        return
    assert frame[0] == 16
    position = 5
    while position < len(frame):
        header = struct.unpack_from('<I', frame, position)[0]
        size = header & 0x3fffffff
        position += 4
        assert position + size <= len(frame)
        if header & 0x80000000:
            pass
        elif header & 0x40000000:
            yield frame[position:position + size]
        else:
            yield frame[position + 4:position + size]
        position += size
    assert position == len(frame)


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    original = json.loads(gzip.decompress(Path('experiments/whole_codec/evidence/first_corpus.json.gz').read_bytes()))
    report = {'source_sha256': digest(Path(__file__)), 'images': [], 'packets': 0}
    bundle = args.output / 'packets.bin'
    with bundle.open('wb') as stream:
        for group, rows in original['groups'].items():
            for row in rows:
                path = args.corpus / row['candidate_path']
                assert digest(path) == row['candidate_sha256']
                count = 0
                for frame in frames(path.read_bytes()):
                    for packet in packets(frame):
                        stream.write(struct.pack('<I', len(packet)))
                        stream.write(packet)
                        count += 1
                report['images'].append({'group': group, 'name': row['name'],
                                        'candidate_sha256': row['candidate_sha256'], 'packets': count})
                report['packets'] += count
    report['bundle_sha256'] = digest(bundle)
    (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Extracted', report['packets'], 'packets from', len(report['images']), 'verified files')


if __name__ == '__main__':
    run()
