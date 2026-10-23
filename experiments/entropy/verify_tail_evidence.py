"""Verify padding-removal evidence, frame monotonicity and identical tail refactoring."""
import gzip
import json
from pathlib import Path
from verify_replay_evidence import verify_sources


def run():
    root = Path(__file__).resolve().parent
    reports = {}
    for variant in ('table', 'unpadded', 'short_stream', 'short_tail'):
        path = root / 'evidence' / ('results_' + variant + '.json.gz')
        report = json.loads(gzip.decompress(path.read_bytes()))
        assert report['complete']
        verify_sources(root, report)
        assert sum(len(rows) for rows in report['groups'].values()) == 222
        assert sum(len(row['frames']) for rows in report['groups'].values() for row in rows) == 1410
        reports[variant] = report
    for group, baseline_rows in reports['table']['groups'].items():
        for index, baseline in enumerate(baseline_rows):
            previous = baseline
            for variant in ('unpadded', 'short_stream', 'short_tail'):
                row = reports[variant]['groups'][group][index]
                assert row['exact'] and row['name'] == baseline['name'] and row['file_sha256'] == baseline['file_sha256']
                assert row['original_bytes'] == baseline['original_bytes']
                assert row['replacement_accounting_bytes'] <= previous['replacement_accounting_bytes']
                assert len(row['frames']) == len(previous['frames'])
                replacement = row['original_bytes']
                for old, frame in zip(previous['frames'], row['frames']):
                    assert frame['raw_bytes'] == old['raw_bytes'] and frame['zstd_bytes'] == old['zstd_bytes']
                    assert frame['rans_bytes'] <= old['rans_bytes']
                    if variant == 'short_tail':
                        assert frame['rans_bytes'] == old['rans_bytes']
                    replacement += frame['candidate_payload_bytes'] - frame['zstd_bytes']
                assert replacement == row['replacement_accounting_bytes']
                previous = row
    checks = json.loads((root / 'evidence/checks_seventh.json').read_text())
    for record in checks['checks']:
        assert record['native_cases'] == 432 and record['independent_cases'] == 414
        verify_sources(root, record)
        assert record['source_sha256'] == reports[record['variant']]['source_sha256']
    for record in checks['tail_checks']:
        assert record['native_cases'] == 405 and record['independent_cases'] == 405
        verify_sources(root, record)
    for name in ('checks', 'tail_checks'):
        records = {row['variant']: row for row in checks[name]}
        assert records['short_stream']['vectors_sha256'] == records['short_tail']['vectors_sha256']
    print('PASS: three complete corpora, 1410 nonincreasing frame sizes, identical tail vectors, source/check identities')


if __name__ == '__main__':
    run()
