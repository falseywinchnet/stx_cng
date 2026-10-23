"""Verify replay provenance and bounded-description comparisons."""
import gzip
import hashlib
import json
from pathlib import Path


def verify_sources(root, report):
    for name, expected in report['source_sha256'].items():
        found = False
        for candidate in root.rglob(Path(name).name):
            if candidate.is_file() and hashlib.sha256(candidate.read_bytes()).hexdigest() == expected:
                found = True
        assert found, name


def paired_frames(first, second):
    assert len(first['images']) == len(second['images'])
    pairs = []
    for left, right in zip(first['images'], second['images']):
        assert left['name'] == right['name'] and left['file_sha256'] == right['file_sha256']
        assert len(left['frames']) == len(right['frames'])
        for a, b in zip(left['frames'], right['frames']):
            assert a['kind'] == b['kind'] and a['raw_bytes'] == b['raw_bytes'] and a['zstd_bytes'] == b['zstd_bytes']
            pairs.append((a, b))
    return pairs


def run():
    root = Path(__file__).resolve().parent
    release = json.loads((root.parent.parent / 'evidence/verification.json').read_text())
    admitted = {}
    for group, data in release['groups'].items():
        for image in data['images']:
            admitted[(group, image['name'])] = image['cng_sha256']
    reports = {}
    names = ('lengths', 'events', 'table', 'account', 'packed', 'recent')
    for name in names:
        report = json.loads((root / 'evidence' / ('replay_' + name + '.json')).read_text())
        verify_sources(root, report)
        assert len(report['images']) == 6
        for image in report['images']:
            assert image['file_sha256'] == admitted[(image['group'], image['name'])]
            for frame in image['frames']:
                assert frame['exact']
        reports[name] = report
    for first, second in paired_frames(reports['lengths'], reports['events']):
        assert first['replay_min8_bytes'] == second['replay_min8_bytes']
        assert first['replay_min3_bytes'] == second['replay_min3_bytes']
    for first, second in paired_frames(reports['events'], reports['table']):
        for key in ('replay_min8_bytes', 'replay_min3_bytes', 'replay_event_bytes'):
            assert second[key] <= first[key]
    for first, second in paired_frames(reports['table'], reports['account']):
        assert first['replay_event_bytes'] == second['replay_event_bytes']
        total = 5
        for key, value in second.items():
            if key.startswith('event_'):
                total += value
        assert total == second['replay_event_bytes']
    for first, second in paired_frames(reports['table'], reports['packed']):
        assert second['replay_event_bytes'] <= first['replay_event_bytes']
    baseline = json.loads(gzip.decompress((root / 'evidence/results_history.json.gz').read_bytes()))
    current = json.loads(gzip.decompress((root / 'evidence/results_table.json.gz').read_bytes()))
    assert baseline['complete'] and current['complete']
    verify_sources(root, current)
    frames = 0
    for group, rows in current['groups'].items():
        assert len(rows) == len(baseline['groups'][group])
        for old, new in zip(baseline['groups'][group], rows):
            assert old['name'] == new['name'] and old['file_sha256'] == new['file_sha256']
            assert old['replacement_accounting_bytes'] >= new['replacement_accounting_bytes']
            assert len(old['frames']) == len(new['frames'])
            for first, second in zip(old['frames'], new['frames']):
                assert second['rans_bytes'] <= first['rans_bytes']
                assert first['raw_bytes'] == second['raw_bytes'] and first['kind'] == second['kind']
                frames += 1
    assert frames == 1410
    print('PASS: six replay reports, exact component accounting, and nonincreasing sizes on 1410 candidate frames')


if __name__ == '__main__':
    run()
