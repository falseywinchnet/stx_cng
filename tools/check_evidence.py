"""Check the frozen v2 release hashes and curated historical evidence totals."""
import hashlib
import json
from pathlib import Path


def run():
    root = Path(__file__).resolve().parent.parent
    report = json.loads((root / 'evidence/verification.json').read_text())
    if not report['complete']:
        raise AssertionError('incomplete evidence')
    for relative, expected in report['source_sha256'].items():
        actual = hashlib.sha256((root / 'experiments/release_v2' / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise AssertionError('verified source changed: ' + relative)
    count = 0
    for group in report['groups'].values():
        if len(group['images']) != group['count']:
            raise AssertionError('image count')
        total_cng = 0
        total_png = 0
        wins = 0
        for row in group['images']:
            if not row['exact']:
                raise AssertionError('image equality')
            total_cng += row['cng_bytes']
            total_png += row['png9_bytes']
            wins += row['cng_bytes'] < row['png9_bytes']
        if total_cng != group['cng_bytes'] or total_png != group['png9_bytes'] or wins != group['size_wins']:
            raise AssertionError('size accounting')
        count += group['count']
    if count != 222:
        raise AssertionError('complete corpus')
    print('PASS: frozen v2 release-source hashes and 222 historical evidence records')


if __name__ == '__main__':
    run()
