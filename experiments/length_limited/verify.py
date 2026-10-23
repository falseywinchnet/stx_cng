"""Verify exact-length candidate provenance and complete-operation evidence."""
import csv
import gzip
import hashlib
import io
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    root = Path('experiments/length_limited/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert digest(Path(name)) == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert digest(root / name) == expected, name
    assert checks['optimality_checks'] == 11264 and checks['match_cases'] == 432 and checks['independent_inverses'] == 414
    packets = list(csv.DictReader(io.StringIO(gzip.decompress((root / 'packets.csv.gz').read_bytes()).decode())))
    assert len(packets) == 2953
    assert sum(int(row['baseline_bytes']) - int(row['candidate_bytes']) for row in packets) == 109611
    corpus = json.loads(gzip.decompress((root / 'corpus.json.gz').read_bytes()))
    direct = json.loads(gzip.decompress((root / 'direct.json.gz').read_bytes()))
    assert corpus['complete'] and direct['complete']
    for report in (corpus, direct):
        for name, expected in report['source_sha256'].items():
            assert digest(Path(name)) == expected, name
    baseline = json.loads(gzip.decompress(Path('experiments/whole_codec/evidence/first_corpus.json.gz').read_bytes()))
    release = json.loads(Path('evidence/verification.json').read_text())
    total = 0
    saved = 0
    identities = {}
    for group, rows in corpus['groups'].items():
        old_rows = baseline['groups'][group]
        published = release['groups'][group]['images']
        assert len(rows) == len(old_rows) == len(published)
        for row, old, reference in zip(rows, old_rows, published):
            assert row['name'] == old['name'] == reference['name']
            assert row['exact'] and row['raw_sha256'] == reference['source_sha256']
            assert row['reference_bytes'] == reference['cng_bytes']
            assert row['candidate_bytes'] <= old['candidate_bytes']
            saved += old['candidate_bytes'] - row['candidate_bytes']
            total += 1
            identities[row['name']] = row
        metrics = {}
        for field in ('encode_seconds', 'decode_seconds', 'encode_entropy_seconds'):
            metrics[field] = sum(row['candidate']['medians'][field] for row in rows) / sum(row['reference']['medians'][field] for row in rows)
        print(group, metrics)
    assert total == 222 and saved == 109611 and len(direct['cases']) == 7
    for case in direct['cases']:
        assert len(case['runs']) == 5
        for result in case['runs']:
            assert result['exact'] and result['candidate_bytes'] == identities[case['name']]['candidate_bytes']
    print('PASS: 222 exact complete images, 35 direct runs, optimality records and source identities')


if __name__ == '__main__':
    run()
