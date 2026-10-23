"""Check byte-preserving entropy reductions and paired measurement records."""
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
    evidence = root / 'experiments/entropy_reduction/evidence'
    checks = json.loads((evidence / 'checks.json').read_text())
    baseline = json.loads(gzip.decompress((root / 'experiments/whole_codec/evidence/first_corpus.json.gz').read_bytes()))
    for variant, identity in checks['variants'].items():
        packet = evidence / (variant + '.json.gz')
        assert digest(packet) == identity['corpus_report_sha256']
        report = json.loads(gzip.decompress(packet.read_bytes()))
        assert report['complete'] and report['variant'] == variant
        assert report['cpu'] == checks['execution_cpu']
        for name, expected in report['source_sha256'].items():
            assert digest(root / name) == expected, name
        assert identity['native_identical_packets'] == 368
        assert identity['uniform_depth_cases'] == 128
        assert digest(root / 'experiments/entropy_reduction/kernel_probe.cpp') == identity['kernel_source_sha256']
        assert report['groups'].keys() == baseline['groups'].keys()
        for group, rows in report['groups'].items():
            originals = baseline['groups'][group]
            assert len(rows) == len(originals)
            for row, original in zip(rows, originals):
                for name in ('name', 'input_sha256', 'raw_sha256', 'candidate_sha256',
                             'candidate_bytes', 'reference_bytes', 'raw_bytes', 'height', 'width', 'channels'):
                    assert row[name] == original[name], (variant, group, name)
                assert row['exact']
                for implementation in ('reference', 'candidate'):
                    for timing, median in row[implementation]['medians'].items():
                        samples = row[implementation][timing]
                        assert len(samples) == 7 and median == statistics.median(samples)
                        assert all(math.isfinite(value) and value >= 0 for value in samples)
                if args.artifacts is not None:
                    output = args.artifacts / variant / row['candidate_path']
                    assert digest(output) == row['candidate_sha256']
            ratios = []
            for timing in ('encode_seconds', 'decode_seconds', 'encode_entropy_seconds'):
                ratios.append(sum(row['candidate']['medians'][timing] for row in rows)
                              / sum(row['reference']['medians'][timing] for row in rows))
            print(variant, group, 'encode/decode/entropy-encode', '/'.join(format(value, '.6f') for value in ratios))
        kernel = evidence / ('kernel_' + variant + '.json')
        assert digest(kernel) == identity['kernel_report_sha256']
        measurements = json.loads(kernel.read_text())
        assert len(measurements['cases']) == 8 and measurements['observed'] > 0
        for case in measurements['cases']:
            for name in ('reference_seconds', 'candidate_seconds'):
                assert len(case[name]) == 7
                assert all(math.isfinite(value) and value > 0 for value in case[name])
    print('PASS: three complete corpora with identical file hashes, source identities and kernel records')


if __name__ == '__main__':
    run()
