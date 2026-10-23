"""Verify strict-size rejection, unchanged files and both comparison references."""
import argparse
import gzip
import hashlib
import json
import math
import statistics
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def samples(record):
    for implementation in ('reference', 'candidate'):
        for field in ('encode_seconds', 'decode_seconds', 'encode_entropy_seconds', 'decode_entropy_seconds'):
            values = record[implementation][field]
            assert len(values) == 7
            assert all(math.isfinite(value) and value >= 0 for value in values)
            if 'medians' in record[implementation]:
                assert statistics.median(values) == record[implementation]['medians'][field]


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    evidence = root / 'experiments/raw_fallback/evidence'
    checks = json.loads((evidence / 'checks.json').read_text())
    assert checks['vectors_sha256'] == checks['previous_vectors_sha256']
    assert checks['native_match_cases'] == 432 and checks['match_budgets_per_case'] == 5
    assert checks['independent_match_cases'] == 414
    assert checks['native_huffman_cases'] == 368 and checks['huffman_budgets_per_case'] == 7
    reports = {}
    for name, expected in checks['reports'].items():
        packet = evidence / (name + '.json.gz')
        assert digest(packet) == expected
        report = json.loads(gzip.decompress(packet.read_bytes()))
        assert report['complete'] and report['cpu'] == checks['execution_cpu']
        for path, source_hash in report['source_sha256'].items():
            assert digest(root / path) == source_hash, path
        reports[name] = report
    assert reports['corpus']['binary_sha256'] == checks['binaries']['cng_fallback_probe']
    assert reports['direct']['binary_sha256'] == checks['binaries']['cng_direct_comparison']
    baseline = json.loads(gzip.decompress((root / 'experiments/whole_codec/evidence/first_corpus.json.gz').read_bytes()))
    indexed = {}
    assert reports['corpus']['groups'].keys() == baseline['groups'].keys()
    for group, rows in reports['corpus']['groups'].items():
        originals = baseline['groups'][group]
        assert len(rows) == len(originals)
        for row, original in zip(rows, originals):
            for field in ('name', 'raw_sha256', 'input_sha256', 'candidate_sha256', 'reference_bytes', 'candidate_bytes'):
                assert row[field] == original[field]
            assert row['exact']
            samples(row)
            indexed[row['name']] = row
            if args.artifacts is not None:
                assert digest(args.artifacts / 'corpus' / row['candidate_path']) == row['candidate_sha256']
        print(group, 'encode ratio', sum(row['candidate']['medians']['encode_seconds'] for row in rows)
              / sum(row['reference']['medians']['encode_seconds'] for row in rows))
    assert len(reports['direct']['cases']) == 7
    for case in reports['direct']['cases']:
        original = indexed[case['name']]
        assert case['source_sha256'] == original['candidate_sha256']
        assert len(case['runs']) == 5
        ratios = []
        for record in case['runs']:
            assert record['exact'] and record['reference_bytes'] == record['candidate_bytes'] == original['candidate_bytes']
            samples(record)
            ratios.append(statistics.median(record['candidate']['encode_seconds'])
                          / statistics.median(record['reference']['encode_seconds']))
        print(case['name'], 'direct median encode ratio', statistics.median(ratios))
        if args.artifacts is not None:
            assert digest(args.artifacts / 'direct' / (case['name'] + '.cne')) == original['candidate_sha256']
    print('PASS: unchanged full corpus, direct candidate comparisons, source identities and budget-check records')


if __name__ == '__main__':
    run()
