#!/usr/bin/env python3
"""Measured HBM2 GEMV diagnostic. This does not reproduce paper Fig.12 yet."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BACKEND = ROOT / 'ext/pimsim/PIMSimulator'
BUILD = ROOT / 'build/fig12'
EXE = BUILD / 'gemv_probe'
MS = (768, 2048, 3072, 4096, 8192)
KS = (2048, 3072, 4096, 8192)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    files = [ROOT / 'tools/fig12/gemv_probe.cc']
    for folder in ('src', 'src/pim', 'lib'):
        for suffix in ('*.cpp', '*.h'):
            files.extend((BACKEND / folder).glob(suffix))
    return {str(p.relative_to(ROOT)): digest(p) for p in sorted(set(files))}


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    sources = sorted((BACKEND / 'src').glob('*.cpp'))
    sources += sorted((BACKEND / 'src/pim').glob('*.cpp'))
    command = ['g++', '-std=c++17', '-O2', '-Wno-deprecated-declarations',
               '-I' + str(BACKEND / 'src'), '-I' + str(BACKEND / 'lib'),
               str(ROOT / 'tools/fig12/gemv_probe.cc')]
    command += [str(p) for p in sources] + ['-pthread', '-o', str(EXE)]
    # A failed rebuild must never leave a previous executable usable.
    EXE.unlink(missing_ok=True)
    with (BUILD / 'build.log').open('w') as log:
        completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if completed.returncode:
        EXE.unlink(missing_ok=True)
        raise RuntimeError('build failed; see build/fig12/build.log')
    meta = {'sources': source_hashes(), 'executable_sha256': digest(EXE),
            'command': command}
    (BUILD / 'build.json').write_text(json.dumps(meta, indent=2) + '\n')
    print('PASS build; functional data storage enabled')


def run_cases(args):
    if not EXE.exists() or not (BUILD / 'build.json').exists():
        raise RuntimeError('first run: python3 tools/fig12/run.py build')
    build_meta = json.loads((BUILD / 'build.json').read_text())
    if build_meta['sources'] != source_hashes() or build_meta['executable_sha256'] != digest(EXE):
        raise RuntimeError('sources or executable changed; rebuild first')
    out = Path(args.out).resolve()
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('choose an empty output directory to avoid mixing runs')
    out.mkdir(parents=True, exist_ok=True)
    cases = [(128, 256), (768, 2048)] if args.action == 'smoke' else [(m, k) for m in MS for k in KS]
    metadata = {'status': 'running', 'backend': 'HBM2_diagnostic_not_paper_DDR5',
                'cases': cases, 'build': build_meta,
                'configs': {str(p.relative_to(ROOT)): digest(p) for p in (
                    BACKEND / 'ini/HBM2_samsung_2M_16B_x64.ini', BACKEND / 'system_hbm.ini')},
                'host_reduction_timed': False, 'paper_reproduction_complete': False}
    manifest = out / 'manifest.json'
    manifest.write_text(json.dumps(metadata, indent=2) + '\n')
    rows = []
    try:
        for m, k in cases:
            name = f'M{m}_K{k}'
            result = out / (name + '.json')
            with (out / (name + '.log')).open('w') as log:
                subprocess.run([str(EXE), str(m), str(k), str(result)], cwd=BACKEND,
                               stdout=log, stderr=subprocess.STDOUT, check=True, timeout=args.timeout)
            row = json.loads(result.read_text())
            if row['correct'] is not True or (row['M'], row['K']) != (m, k):
                raise RuntimeError('invalid functional result: ' + name)
            flops = 2 * m * k  # Useful mathematical work; exclude padding from numerator.
            ns = row['kernel_including_upload_cycles'] * row['tCK_ns']
            read_ns = row['result_read_cycles'] * row['tCK_ns']
            row['useful_flops'] = flops
            row['kernel_including_upload_gflops'] = flops / ns
            row['kernel_plus_readback_gflops'] = flops / (ns + read_ns)
            rows.append(row)
            print(f'PASS {name}; padded={row["M_padded"]}x{row["K_padded"]}', flush=True)
        with (out / 'diagnostic.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader(); writer.writerows(rows)
        metadata['status'] = 'complete'
    except BaseException as exc:
        metadata['status'] = 'cancelled' if isinstance(exc, KeyboardInterrupt) else 'failed'
        raise
    finally:
        manifest.write_text(json.dumps(metadata, indent=2) + '\n')
    print('Measured diagnostic data:', out / 'diagnostic.csv')
    print('NOT paper Fig.12: DDR5, pure-kernel timing and host reduction remain unimplemented.')


def plot(args):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    folder = Path(args.out).resolve()
    meta = json.loads((folder / 'manifest.json').read_text())
    if meta['status'] != 'complete':
        raise RuntimeError('refusing to plot an incomplete/failed run')
    with (folder / 'diagnostic.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    cases = [(int(r['M']), int(r['K'])) for r in rows]
    if cases != [tuple(c) for c in meta['cases']] or any(r['correct'] != 'True' for r in rows):
        raise RuntimeError('case list or correctness validation failed')
    x = list(range(len(rows)))
    fig, ax = plt.subplots(figsize=(max(7, len(rows) * .57), 4.8))
    ax.bar([v-.18 for v in x], [float(r['kernel_including_upload_gflops']) for r in rows],
           width=.36, label='Kernel incl. input upload')
    ax.bar([v+.18 for v in x], [float(r['kernel_plus_readback_gflops']) for r in rows],
           width=.36, label='Kernel + result readback')
    ax.set_xticks(x, [f'{m}\n{k}' for m,k in cases], fontsize=8)
    ax.set_xlabel('M (upper row), K (lower row); batch = 1')
    ax.set_ylabel('Useful GFLOP/s (2MK / measured time)')
    ax.set_title('HBM2 GEMV diagnostic — NOT paper Fig.12 reproduction')
    ax.legend(fontsize=8)
    ax.set_axisbelow(True); ax.grid(axis='y', alpha=.25)
    fig.text(.5,.015,'Host reduction not timed; weight preload excluded; padded work included in elapsed time.',
             ha='center', fontsize=8)
    fig.tight_layout(rect=(0,.05,1,1))
    fig.savefig(folder / 'hbm_gemv_diagnostic.png', dpi=180)
    fig.savefig(folder / 'hbm_gemv_diagnostic.pdf')
    print(folder / 'hbm_gemv_diagnostic.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'smoke', 'sweep', 'plot'))
    parser.add_argument('--out', default='results/fig12_hbm_diagnostic')
    parser.add_argument('--timeout', type=int, default=600, help='seconds per case')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    if args.action == 'build': build()
    elif args.action == 'plot': plot(args)
    else: run_cases(args)


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('ERROR:', exc, file=sys.stderr)
        sys.exit(1)
