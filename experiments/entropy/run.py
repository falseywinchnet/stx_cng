"""Probe frozen CSP entropy frames; keep code, data and timing identities explicit.

Run on one host after building. --corpus names the external research corpus root;
the published evidence pins the accepted inputs, while its historical locator
report resolves filenames. No image or model is selected by the measured result.
"""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path


def digest(path):
    result = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('.build/entropy/cng_entropy_probe'))
    parser.add_argument('--output', type=Path, default=Path('.build/entropy/results.json'))
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('preserve prior experiment: ' + str(args.output))
    frozen = json.loads((args.corpus / 'results/row_copy_comparison.json').read_text())
    admitted = json.loads(Path('evidence/verification.json').read_text())
    report = dict(complete=False,scope=__doc__,platform=platform.platform(),
                  binary_sha256=digest(args.binary),source_sha256={},groups={})
    if platform.system() == 'Darwin':
        report['cpu'] = subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip()
    for path in sorted(Path('experiments/entropy').glob('*')):
        if path.is_file():
            report['source_sha256'][str(path)] = digest(path)
    for group in ('general','rgba','composites'):
        rows = []
        report['groups'][group] = rows
        for original,reference in zip(frozen[group],admitted['groups'][group]['images']):
            packet = args.corpus / original['original_artifact']
            if digest(packet) != reference['cng_sha256'] or original['name'] != reference['name']:
                raise AssertionError('frozen packet identity')
            process = subprocess.run([str(args.binary),str(packet)],capture_output=True,text=True,check=True)
            row = json.loads(process.stdout)
            if not row['exact']:
                raise AssertionError('entropy round trip')
            row['name'] = reference['name']
            row['file_sha256'] = reference['cng_sha256']
            row['original_bytes'] = reference['cng_bytes']
            replacement = row['original_bytes']
            for frame in row['frames']:
                replacement += frame['candidate_payload_bytes'] - frame['zstd_bytes']
                frame['median_seconds'] = {}
                for field in ('rans_encode','rans_decode','zstd_encode','zstd_decode'):
                    frame['median_seconds'][field] = statistics.median(frame[field])
            row['replacement_accounting_bytes'] = replacement
            rows.append(row)
            args.output.write_text(json.dumps(report,indent=2)+'\n')
        if len(rows) != admitted['groups'][group]['count']:
            raise AssertionError('complete frozen group')
        print(group,len(rows),'files exact',flush=True)
    report['complete'] = True
    args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    run()
