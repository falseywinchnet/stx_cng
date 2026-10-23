"""Verify the independent event-backend corpus and workspace records."""
import gzip
import json
from pathlib import Path
from verify_replay_evidence import verify_sources


def run():
    root = Path(__file__).resolve().parent
    reports = {}
    names = ('event_backend', 'event_eight', 'event_small', 'event_large', 'event_partitioned')
    for name in names:
        path = root / 'evidence' / ('results_' + name + '.json.gz')
        report = json.loads(gzip.decompress(path.read_bytes()))
        assert report['complete']
        verify_sources(root, report)
        frames = 0
        files = 0
        for rows in report['groups'].values():
            for row in rows:
                assert row['exact']
                files += 1
                frames += len(row['frames'])
        assert files == 222 and frames == 1410
        reports[name] = report
    for group, rows in reports['event_small']['groups'].items():
        original = reports['event_eight']['groups'][group]
        for first, second in zip(original, rows):
            assert first['name'] == second['name'] and first['file_sha256'] == second['file_sha256']
            assert first['replacement_accounting_bytes'] == second['replacement_accounting_bytes']
            assert len(first['frames']) == len(second['frames'])
            for a, b in zip(first['frames'], second['frames']):
                assert a['rans_bytes'] == b['rans_bytes'] and a['raw_bytes'] == b['raw_bytes']
    for name in ('workspaces_fifth.json', 'checks_fifth.json'):
        verify_sources(root, json.loads((root / 'evidence' / name).read_text()))
    checks = json.loads((root / 'evidence/checks_fifth.json').read_text())
    for check in checks['checks']:
        verify_sources(root, check)
    print('PASS: five complete event-backend corpora, compact-workspace size equivalence, and check/workspace source identities')


if __name__ == '__main__':
    run()
