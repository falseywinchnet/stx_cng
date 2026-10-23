"""Extract every retained entropy frame from the verified CNE image corpus."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import struct
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source = Path('experiments/parser_review/extract.py')
    spec = importlib.util.spec_from_file_location('packet_extract', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = json.loads(gzip.decompress(Path('experiments/length_limited/evidence/corpus.json.gz').read_bytes()))
    report = {'source_sha256': digest(Path(__file__)), 'frame_reader_sha256': digest(source), 'frames': []}
    bundle = args.output / 'frames.bin'
    with bundle.open('wb') as stream:
        for group, rows in original['groups'].items():
            for row in rows:
                path = args.corpus / row['candidate_path']
                assert digest(path) == row['candidate_sha256']
                for ordinal, frame in enumerate(module.frames(path.read_bytes())):
                    stream.write(struct.pack('<I', len(frame)))
                    stream.write(frame)
                    report['frames'].append({'group': group, 'name': row['name'], 'ordinal': ordinal,
                                             'image_sha256': row['candidate_sha256'],
                                             'frame_sha256': hashlib.sha256(frame).hexdigest(),
                                             'bytes': len(frame)})
    report['bundle_sha256'] = digest(bundle)
    (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Extracted', len(report['frames']), 'retained frames; discarded literal attempts are excluded')


if __name__ == '__main__':
    run()
