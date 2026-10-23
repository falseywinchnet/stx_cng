"""Verify whole-image evidence and optionally the emitted experimental files."""
import argparse
import gzip
import hashlib
import json
import math
import statistics
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    evidence = root / 'experiments/whole_codec/evidence'
    packet = evidence / 'first_corpus.json.gz'
    checks = json.loads((evidence / 'checks.json').read_text())
    assert digest(packet) == checks['report_sha256']
    assert checks['native_cases'] == 432 and checks['independent_cases'] == 414
    assert checks['vectors_sha256'] == checks['tagged_vectors_sha256']
    report = json.loads(gzip.decompress(packet.read_bytes()))
    assert report['complete'] and report['cpu'] == checks['execution_cpu']
    assert report['binary_sha256'] == checks['probe_binary_sha256']
    for name, expected in report['source_sha256'].items():
        assert digest(root / name) == expected, name
    admitted = json.loads((root / 'evidence/verification.json').read_text())
    counts = {'general': 131, 'rgba': 80, 'composites': 11}
    assert report['groups'].keys() == counts.keys()
    timings = ('encode_seconds', 'decode_seconds',
               'encode_entropy_seconds', 'decode_entropy_seconds')
    for group, count in counts.items():
        rows = report['groups'][group]
        originals = admitted['groups'][group]['images']
        assert len(rows) == len(originals) == count
        for row, original in zip(rows, originals):
            assert row['name'] == original['name'] and row['exact']
            assert row['input_sha256'] == original['cng_sha256']
            assert row['raw_sha256'] == original['source_sha256']
            assert row['reference_bytes'] == original['cng_bytes']
            assert row['raw_bytes'] == row['height'] * row['width'] * row['channels']
            assert row['candidate_bytes'] > 0 and row['process_peak_rss_bytes'] > 0
            if args.artifacts is not None:
                artifact = args.artifacts / row['candidate_path']
                assert artifact.stat().st_size == row['candidate_bytes']
                assert digest(artifact) == row['candidate_sha256']
                with artifact.open('rb') as stream:
                    assert stream.read(8) == bytes([137, 67, 78, 69, 13, 10, 26, 10])
            for implementation in ('reference', 'candidate'):
                record = row[implementation]
                assert record['encode_workspace_bytes'] > 0
                assert record['decode_workspace_bytes'] > 0
                for timing in timings:
                    samples = record[timing]
                    assert len(samples) == 7
                    assert all(math.isfinite(value) and value >= 0 for value in samples)
                    assert record['medians'][timing] == statistics.median(samples)
        ratios = [sum(row['candidate_bytes'] for row in rows)
                  / sum(row['reference_bytes'] for row in rows)]
        for timing in timings[:2]:
            ratios.append(sum(row['candidate']['medians'][timing] for row in rows)
                          / sum(row['reference']['medians'][timing] for row in rows))
        smaller = sum(row['candidate_bytes'] < row['reference_bytes'] for row in rows)
        equal = sum(row['candidate_bytes'] == row['reference_bytes'] for row in rows)
        print(group, 'size/encode/decode:', '/'.join(format(ratio, '.6f') for ratio in ratios),
              'smaller/equal/larger:', smaller, equal, count - smaller - equal)
    print('PASS: 222 complete images, source identities, exactness records and paired timings')


if __name__ == '__main__':
    run()
