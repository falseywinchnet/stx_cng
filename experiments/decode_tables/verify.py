"""Verify grouped-reader identities, ordinary checks and conflicting timing records."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    root = Path('experiments/decode_tables/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert digest(Path(name)) == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert digest(root / name) == expected, name
    for filename in ('profile', 'profile-batch', 'linked', 'linked-tail', 'linked-batch-tail', 'frames'):
        rows = list(csv.DictReader(io.StringIO(gzip.decompress((root / (filename + '.csv.gz')).read_bytes()).decode())))
        assert len(rows) == (1413 if filename == 'frames' else 2953)
        column = 'width' if filename == 'profile' else 'batch'
        ratio = sum(float(row[column]) for row in rows) / sum(float(row['baseline']) for row in rows)
        print(filename, 'decode ratio', ratio)
    corpus = json.loads(gzip.decompress((root / 'corpus.json.gz').read_bytes()))
    direct = json.loads(gzip.decompress((root / 'direct.json.gz').read_bytes()))
    prior = json.loads(gzip.decompress(Path('experiments/length_limited/evidence/corpus.json.gz').read_bytes()))
    assert corpus['complete'] and direct['complete']
    for report in (corpus, direct):
        for name, expected in report['source_sha256'].items():
            assert digest(Path(name)) == expected, name
    total = 0
    for group, rows in corpus['groups'].items():
        assert len(rows) == len(prior['groups'][group])
        for row, old in zip(rows, prior['groups'][group]):
            assert row['exact'] and row['name'] == old['name']
            assert row['candidate_sha256'] == old['candidate_sha256']
            assert row['raw_sha256'] == old['raw_sha256']
            total += 1
    assert total == 222 and len(direct['cases']) == 7
    for case in direct['cases']:
        assert len(case['runs']) == 5
        for result in case['runs']:
            assert result['exact'] and result['candidate_bytes'] == result['reference_bytes']
    assert checks['huffman_cases'] == 1178 and checks['match_cases'] == 432
    assert checks['uniform_depth_checks'] == 128 and checks['independent_inverses'] == 414
    print('PASS: 222 byte-identical images, 35 direct runs and retained decoder comparisons')


if __name__ == '__main__':
    run()
