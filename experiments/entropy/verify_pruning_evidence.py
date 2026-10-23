"""Check complete pruning/key-width evidence and exact parser accounting."""
import gzip
import json
from pathlib import Path
from verify_replay_evidence import verify_sources


def run():
    root = Path(__file__).resolve().parent
    admitted = json.loads((root.parent.parent / 'evidence/verification.json').read_text())
    identities = {}
    for group in admitted['groups'].values():
        for row in group['images']:
            identities[row['name']] = row['cng_sha256']
    checks = json.loads((root / 'evidence/checks_sixth.json').read_text())
    assert len(checks['checks']) == 4
    for check in checks['checks']:
        assert check['native_cases'] == 432 and check['independent_cases'] == 414
        verify_sources(root, check)
        path = root / 'evidence' / ('results_' + check['variant'] + '.json.gz')
        report = json.loads(gzip.decompress(path.read_bytes()))
        assert report['complete']
        verify_sources(root, report)
        assert report['source_sha256'] == check['source_sha256']
        frames = 0
        files = 0
        for group, rows in report['groups'].items():
            references = admitted['groups'][group]['images']
            assert len(rows) == len(references)
            for row, reference in zip(rows, references):
                assert row['name'] == reference['name'] and row['file_sha256'] == reference['cng_sha256']
                assert row['exact'] and row['original_bytes'] == reference['cng_bytes']
                replacement = row['original_bytes']
                for frame in row['frames']:
                    replacement += frame['candidate_payload_bytes'] - frame['zstd_bytes']
                assert replacement == row['replacement_accounting_bytes']
                files += 1
                frames += len(row['frames'])
        assert files == 222 and frames == 1410
    diagnostic = json.loads((root / 'evidence/pruning_diagnostic.json').read_text())
    verify_sources(root, diagnostic)
    assert len(diagnostic['images']) == 6
    for image in diagnostic['images']:
        assert image['input_sha256'] == identities[image['name']]
        initial_frames = image['pruned']['frames']
        eight_frames = image['eight']['frames']
        assert len(initial_frames) == len(eight_frames)
        for initial, eight in zip(initial_frames, eight_frames):
            assert initial['kind'] == eight['kind'] and initial['raw_bytes'] == eight['raw_bytes']
            assert initial['zstd_bytes'] == eight['zstd_bytes'] and initial['exact'] and eight['exact']
            for frame, phase in ((initial, 'initial'), (initial, 'pruned'), (eight, 'initial')):
                counts = frame[phase]
                assert counts['copied'] + counts['literals'] == frame['raw_bytes']
                assert sum(counts['length_counts']) == counts['matches'] <= counts['events']
            assert initial['pruned']['matches'] <= initial['initial']['matches']
            assert initial['pruned']['literals'] >= initial['initial']['literals']
            assert eight['initial']['length_counts'][:2] == [0, 0]
    print('PASS: four complete corpora, source/check identities and six exact parser-accounting comparisons')


if __name__ == '__main__':
    run()
