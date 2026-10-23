"""Verify bounded-candidate comparisons, equivalent refactors and caller storage."""
import gzip
import json
from pathlib import Path
from verify_replay_evidence import verify_sources


def run():
    root = Path(__file__).resolve().parent
    admitted = json.loads((root.parent.parent / 'evidence/verification.json').read_text())
    reports = {}
    checks = json.loads((root / 'evidence/checks_eighth.json').read_text())
    assert len(checks['checks']) == 4
    for check in checks['checks']:
        verify_sources(root, check)
        verify_sources(root, check['build_inputs'])
        assert check['native_cases'] == 432 and check['independent_cases'] == 414
        name = check['variant']
        report = json.loads(gzip.decompress((root / 'evidence' / ('results_' + name + '.json.gz')).read_bytes()))
        assert report['complete'] and check['source_sha256'] == report['source_sha256']
        verify_sources(root, report)
        files = 0
        frames = 0
        for group, rows in report['groups'].items():
            reference = admitted['groups'][group]['images']
            assert len(rows) == len(reference)
            for row, original in zip(rows, reference):
                assert row['exact'] and row['name'] == original['name']
                assert row['file_sha256'] == original['cng_sha256'] and row['original_bytes'] == original['cng_bytes']
                replacement = row['original_bytes']
                for frame in row['frames']:
                    replacement += frame['candidate_payload_bytes'] - frame['zstd_bytes']
                assert replacement == row['replacement_accounting_bytes']
                files += 1
                frames += len(row['frames'])
        assert files == 222 and frames == 1410
        reports[name] = report
    for variant in ('two_tail', 'tagged'):
        for group, rows in reports['two_candidates']['groups'].items():
            for first, second in zip(rows, reports[variant]['groups'][group]):
                assert first['replacement_accounting_bytes'] == second['replacement_accounting_bytes']
                assert len(first['frames']) == len(second['frames'])
                for a, b in zip(first['frames'], second['frames']):
                    assert a['raw_bytes'] == b['raw_bytes'] and a['rans_bytes'] == b['rans_bytes']
    records = {row['variant']: row for row in checks['checks']}
    assert records['two_candidates']['vectors_sha256'] == records['two_tail']['vectors_sha256'] == records['tagged']['vectors_sha256']
    workspaces = json.loads((root / 'evidence/workspaces_eighth.json').read_text())
    verify_sources(root, workspaces)
    assert workspaces['workspaces']['two_candidates']['encoder_bytes'] == 525328
    assert workspaces['workspaces']['tagged']['encoder_bytes'] == 656400
    for workspace in workspaces['workspaces'].values():
        assert workspace['decoder_bytes'] == 131088 and workspace['block_bytes'] == 131072
    print('PASS: four complete corpora, equivalent refactor sizes/vectors, build/source identities and caller storage')


if __name__ == '__main__':
    run()
