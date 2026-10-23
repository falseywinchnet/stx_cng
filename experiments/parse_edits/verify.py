"""Verify retained parse-edit evidence and its current source identities."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    root = Path('experiments/parse_edits/evidence')
    record = json.loads((root / 'checks.json').read_text())
    for name, expected in record['source_sha256'].items():
        assert digest(Path(name)) == expected, name
    for name, expected in record['evidence_sha256'].items():
        assert digest(root / name) == expected, name
    rows = list(csv.DictReader(io.StringIO(gzip.decompress((root / 'rows.csv.gz').read_bytes()).decode())))
    blocks = [row for row in rows if row['kind'] == 'block']
    frames = [row for row in rows if row['kind'] == 'frame']
    assert len(blocks) == 3135 and len(frames) == 1413
    assert sum(int(row['proposals']) for row in blocks) == 54192
    assert sum(int(row['improving']) for row in blocks) == 11148
    assert all(int(row['selected_bytes']) <= int(row['baseline_bytes']) for row in rows)
    assert sum(int(row['baseline_bytes']) for row in frames) == 180122478
    assert sum(int(row['selected_bytes']) for row in frames) == 180113405
    manifest = json.loads(gzip.decompress((root / 'manifest.json.gz').read_bytes()))
    prior = json.loads(gzip.decompress(Path('experiments/whole_codec/evidence/first_corpus.json.gz').read_bytes()))
    admitted = {(group, row['name']): row['candidate_sha256'] for group, group_rows in prior['groups'].items() for row in group_rows}
    assert len(manifest['frames']) == len(frames)
    for index, frame in enumerate(manifest['frames']):
        assert admitted[(frame['group'], frame['name'])] == frame['image_sha256']
        assert frame['bytes'] == int(frames[index]['baseline_bytes'])
    depths = list(csv.DictReader(io.StringIO(gzip.decompress((root / 'depth.csv.gz').read_bytes()).decode())))
    assert len(depths) == 2953
    assert sum(int(row['current_bits']) - int(row['unrestricted_bits']) for row in depths) == 3389540
    print('PASS: 54192 bounded edits, 1413 frame identities, depth bounds and pinned evidence')


if __name__ == '__main__':
    run()
