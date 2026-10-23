"""Verify preserved experiment identities and summarize complete entropy operations."""
import gzip
import hashlib
import json
from pathlib import Path


def summarize():
    root = Path(__file__).resolve().parent
    for name in ('results', 'results_words', 'results_runs'):
        report = json.loads(gzip.decompress((root / 'evidence' / (name + '.json.gz')).read_bytes()))
        if not report['complete']:
            raise AssertionError('incomplete experiment')
        for original, expected in report['source_sha256'].items():
            filename = Path(original).name
            candidates = root.rglob(filename)
            found = False
            for candidate in candidates:
                if candidate.exists():
                    observed = hashlib.sha256(candidate.read_bytes()).hexdigest()
                    if observed == expected:
                        found = True
            if not found:
                raise AssertionError('missing source identity: ' + original)
        print(name, report['cpu'])
        for group, rows in report['groups'].items():
            original_bytes = 0
            replacement_bytes = 0
            times = dict(rans_encode=0.0, rans_decode=0.0, zstd_encode=0.0, zstd_decode=0.0)
            frames = 0
            for row in rows:
                if not row['exact']:
                    raise AssertionError('inexact entropy stream')
                original_bytes += row['original_bytes']
                replacement_bytes += row['replacement_accounting_bytes']
                for frame in row['frames']:
                    frames += 1
                    for field in times:
                        times[field] += frame['median_seconds'][field]
            size_ratio = replacement_bytes / original_bytes
            encode_ratio = times['rans_encode'] / times['zstd_encode']
            decode_ratio = times['rans_decode'] / times['zstd_decode']
            print(f'  {group}: {len(rows)} files, {frames} frames, '
                  f'{original_bytes} -> {replacement_bytes} bytes; '
                  f'size {size_ratio:.6f}, encode time {encode_ratio:.3f}, '
                  f'decode time {decode_ratio:.3f}')


if __name__ == '__main__':
    summarize()
