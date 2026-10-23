"""Measure complete image operations, including codec allocation and discarded attempts."""
import argparse
import hashlib
import json
import platform
import re
import statistics
import subprocess
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('.build/cng-v3-integration/probe'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    assert platform.system() == 'Darwin', 'this runner records Darwin time -l RSS units'
    frozen = json.loads((args.corpus / 'results/row_copy_comparison.json').read_text())
    admitted = json.loads(Path('evidence/verification.json').read_text())
    report = {'complete': False, 'scope': __doc__, 'platform': platform.platform(),
              'cpu': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
              'binary_sha256': digest(args.binary), 'source_sha256': {}, 'groups': {}}
    paths = list(Path('experiments/cngcompress_integration').glob('*'))
    paths += list(Path('src').glob('*')) + list(Path('include/stx_cng').glob('*'))
    paths += list(Path('stx_cngcompress/src').glob('*')) + list(Path('stx_cngcompress/include').glob('*'))
    paths += [Path('CMakeLists.txt')]
    previous = json.loads(Path('.build/cngcompress-gate-short/whole-first/results.json').read_text())
    for path in sorted(paths):
        if path.is_file():
            report['source_sha256'][str(path)] = digest(path)
    for group in ('general', 'rgba', 'composites'):
        folder = args.output / group
        folder.mkdir()
        rows = []
        report['groups'][group] = rows
        originals = frozen[group]
        references = admitted['groups'][group]['images']
        assert len(originals) == len(references)
        for index, (original, reference) in enumerate(zip(originals, references)):
            packet = args.corpus / original['original_artifact']
            assert original['name'] == reference['name'] and digest(packet) == reference['cng_sha256']
            candidate = folder / (str(index) + '.cng')
            raw = folder / (str(index) + '.raw')
            process = subprocess.run(['/usr/bin/time', '-l', str(args.binary), str(packet), str(candidate), str(raw)],
                                     capture_output=True, text=True, check=True)
            row = json.loads(process.stdout)
            assert row['exact'] and row['reference_bytes'] == reference['cng_bytes']
            assert digest(raw) == reference['source_sha256']
            assert candidate.stat().st_size == row['candidate_bytes']
            prior = previous['groups'][group][index]
            assert prior['name'] == reference['name']
            prior_path = Path('.build/cngcompress-gate-short/whole-first') / prior['candidate_path']
            old_bytes = prior_path.read_bytes()
            new_bytes = bytearray(candidate.read_bytes())
            assert new_bytes[:8] == bytes([137, 67, 83, 80, 13, 10, 26, 10]) and new_bytes[16] == 3
            new_bytes[:8] = old_bytes[:8]
            new_bytes[16] = old_bytes[16]
            new_bytes[32:36] = old_bytes[32:36]
            assert bytes(new_bytes) == old_bytes, reference['name']
            rss = re.search(r'(\d+)\s+maximum resident set size', process.stderr)
            assert rss is not None
            row.update(name=reference['name'], input_sha256=reference['cng_sha256'],
                       raw_sha256=reference['source_sha256'], candidate_sha256=digest(candidate),
                       candidate_path=str(candidate.relative_to(args.output)),
                       process_peak_rss_bytes=int(rss.group(1)))
            for implementation in ('reference', 'candidate'):
                row[implementation]['medians'] = {name: statistics.median(row[implementation][name]) for name in
                    ('encode_seconds', 'decode_seconds', 'encode_entropy_seconds', 'decode_entropy_seconds')}
            rows.append(row)
            # Only this run's generated raw intermediate is removed after its
            # exact source hash is checked. The emitted candidate file remains.
            raw.unlink()
            (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
            if (index + 1) % 20 == 0:
                print(group, index + 1, 'complete images exact', flush=True)
        print(group, len(rows), 'complete images exact', flush=True)
    report['complete'] = True
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    run()
