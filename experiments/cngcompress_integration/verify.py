"""Validate current public integration and its two complete measurement runs."""
import gzip
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    root = Path('experiments/cngcompress_integration/evidence')
    checks = json.loads((root / 'checks.json').read_text())
    for name, expected in checks['source_sha256'].items():
        assert digest(Path(name)) == expected, name
    for name, expected in checks['evidence_sha256'].items():
        assert digest(root / name) == expected, name
    pin = json.loads(Path('docs/CNGCOMPRESS_SOURCE.json').read_text())
    for name, expected in pin['sha256'].items():
        assert digest(Path('stx_cngcompress') / name) == expected, name
    release = json.loads(Path('evidence/verification.json').read_text())
    previous = None
    for name in ('whole-first', 'whole-repeat'):
        report = json.loads(gzip.decompress((root / (name + '.json.gz')).read_bytes()))
        assert report['complete']
        identity = []
        rows = []
        for group, count in (('general', 131), ('rgba', 80), ('composites', 11)):
            current = report['groups'][group]
            assert len(current) == count
            for row, reference in zip(current, release['groups'][group]['images']):
                assert row['exact'] and row['name'] == reference['name']
                assert row['raw_sha256'] == reference['source_sha256']
                assert row['input_sha256'] == reference['cng_sha256']
                identity.append([row['name'], row['candidate_sha256'], row['candidate_bytes']])
            rows += current
        if previous is not None:
            assert previous == identity
        previous = identity
        assert sum(row['candidate_bytes'] for row in rows) == 197257667
        print(name, {metric: sum(row['candidate']['medians'][metric] for row in rows) /
                     sum(row['reference']['medians'][metric] for row in rows)
                     for metric in ('encode_seconds', 'decode_seconds')})
    for name in ('tests-v3-final.log', 'installed-v3-final.log'):
        log = (root / name).read_text()
        assert '100% tests passed, 0 tests failed out of 1' in log
        assert 'explicit v2 rejection' in log
    for name in ('collections-first', 'collections-repeat'):
        assert 'PASS: 3 collections, both forms, both codecs, 7 paired trials' in (root / (name + '.log')).read_text()
    libraries = (root / 'libraries-v3-final.log').read_text()
    assert 'libz.1.dylib' in libraries and 'libc++.1.dylib' in libraries and 'zstd' not in libraries
    capacity = subprocess.check_output([sys.executable, '-B', 'experiments/cngcompress_integration/capacity.py'], text=True)
    assert json.loads(capacity) == json.loads((root / 'capacity.json').read_text())
    print('PASS: public v3 integration, two 222-image runs, collection forms, installed consumer and source pin')


if __name__ == '__main__':
    run()
