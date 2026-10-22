#!/usr/bin/env python3
"""Add an explicit HOST REDUCTION ESTIMATE to verified HBM2 measurements.

Replication is already inside kernel_including_upload_cycles. Never add its
derived bytes/bandwidth as another delay. This tool does not modify gem5 ticks.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

# Audited source/config maps from d3981fc. New implementations require re-audit.
SOURCE_MAP_SHA256 = 'ce380f8300a568abf25b9a0b534cf838f188f7e4cf59621ce5bdc2398124e232'
CONFIG_MAP_SHA256 = '36e156f8c0dec1562c3ad9728fd3642d2ec4f9865c3d15e481767207f4c7d5b2'
BACKEND = 'HBM2_diagnostic_not_paper_DDR5'


def map_digest(value):
    encoded = json.dumps(value, sort_keys=True, separators=(',', ':')).encode()
    return hashlib.sha256(encoded).hexdigest()


def file_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def positive_int(row, key):
    value = row[key]
    if type(value) is not int or value <= 0:
        raise ValueError(f'{key} must be a positive integer')
    return value


def account(row, ns_per_row, setup_ns):
    if not math.isfinite(ns_per_row) or ns_per_row <= 0:
        raise ValueError('host reduction ns per row must be positive and finite')
    if not math.isfinite(setup_ns) or setup_ns < 0:
        raise ValueError('host reduction setup ns must be nonnegative and finite')
    if row['backend'] != BACKEND or row['correct'] is not True:
        raise ValueError('only verified HBM2 diagnostic results are supported')
    if row['host_reduction_timed'] is not False:
        raise ValueError('host reduction may already be timed; refusing double counting')
    if row['tCK_ns'] != 1:
        raise ValueError('unexpected clock for the audited backend')
    m, k = positive_int(row, 'M'), positive_int(row, 'K')
    mp, kp = positive_int(row, 'M_padded'), positive_int(row, 'K_padded')
    if m > 8192 or k > 8192 or mp != (m+1023)//1024*1024 or kp != (k+255)//256*256:
        raise ValueError('unexpected dimensions or padding for audited kernel')
    preload = positive_int(row, 'weight_preload_cycles') * row['tCK_ns']
    kernel = positive_int(row, 'kernel_including_upload_cycles') * row['tCK_ns']
    readback = positive_int(row, 'result_read_cycles') * row['tCK_ns']
    # computeGemv: each output tile uploads all input bursts to 16 channels,
    # one rank per channel. Broadcast to eight PIM blocks is internal.
    upload_transactions = (mp // 1024) * (kp // 16) * 16
    host_ns = setup_ns + mp * ns_per_row
    measured_ns = kernel + readback
    resident_ns = measured_ns + host_ns
    cold_ns = preload + resident_ns
    if not all(math.isfinite(v) for v in (host_ns, resident_ns, cold_ns)):
        raise ValueError('host model overflow')
    flops = 2*m*k
    return {
        'M': m, 'K': k, 'M_padded': mp, 'K_padded': kp,
        'backend': BACKEND,
        'derived_input_upload_transactions': upload_transactions,
        'derived_input_upload_bytes': upload_transactions * 32,
        'replication_already_in_kernel_time': True,
        'additional_replication_ns': 0,
        'derived_result_read_bytes': mp * 32,
        'host_reduction_rows': mp,
        'source_level_fp16_to_fp32_conversions': mp * 16,
        'source_level_fp32_additions': mp * 16,
        'modeled_output_bytes_fp32': mp * 4,
        'weight_preload_simulated_ns': preload,
        'kernel_including_upload_simulated_ns': kernel,
        'result_read_simulated_ns': readback,
        'backend_measured_ns': measured_ns,
        'host_reduction_ns_per_row': ns_per_row,
        'host_reduction_setup_ns': setup_ns,
        'host_reduction_estimated_ns': host_ns,
        'resident_weights_estimated_ns': resident_ns,
        'cold_weights_estimated_ns': cold_ns,
        'useful_flops': flops,
        'backend_measured_gflops': flops / measured_ns,
        'resident_weights_estimated_gflops': flops / resident_ns,
        'cold_weights_estimated_gflops': flops / cold_ns,
        'host_reduction_timed_in_simulator': False,
        'host_reduction_modeled': True,
        'paper_reproduction_complete': False,
    }


def load_results(folder):
    meta_path = folder / 'manifest.json'
    meta = json.loads(meta_path.read_text())
    if meta['status'] != 'complete' or meta['backend'] != BACKEND:
        raise ValueError('input must be a complete HBM2 diagnostic run')
    if map_digest(meta['build']['sources']) != SOURCE_MAP_SHA256:
        raise ValueError('source fingerprint differs: re-audit traffic/reduction accounting; do not bypass')
    if map_digest(meta['configs']) != CONFIG_MAP_SHA256:
        raise ValueError('configuration fingerprint differs: current assumptions do not apply')
    cases = meta['cases']
    if not cases or any(not isinstance(c, list) or len(c) != 2 or
                        any(type(v) is not int or not 1 <= v <= 8192 for v in c)
                        for c in cases):
        raise ValueError('invalid case list')
    if len({tuple(c) for c in cases}) != len(cases):
        raise ValueError('duplicate cases')
    fingerprints = {'manifest.json': file_digest(meta_path)}
    rows = []
    for m, k in cases:
        path = folder / f'M{m}_K{k}.json'
        row = json.loads(path.read_text())
        if (row['M'], row['K']) != (m, k):
            raise ValueError('case coordinates do not match filename')
        fingerprints[path.name] = file_digest(path)
        rows.append(row)
    return rows, fingerprints


def plot(rows, out, label):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(max(8, len(rows)*.6), 5.2))
    x = list(range(len(rows)))
    ax.bar([v-.18 for v in x], [r['backend_measured_gflops'] for r in rows], .36,
           label='Simulated kernel incl. upload + readback')
    ax.bar([v+.18 for v in x], [r['resident_weights_estimated_gflops'] for r in rows], .36,
           label='Above + modeled host reduction')
    ax.set_xticks(x, [f'{r["M"]}\n{r["K"]}' for r in rows], fontsize=8)
    ax.set_xlabel('M (upper row), K (lower row); batch = 1')
    ax.set_ylabel('Useful GFLOP/s')
    ax.set_title('HBM2 + host reduction estimate (not paper Fig.12 reproduction)')
    ax.legend(fontsize=8)
    ax.set_axisbelow(True); ax.grid(axis='y', alpha=.25)
    fig.text(.5,.03, f'Host scenario: {label}\n'
             f'{rows[0]["host_reduction_ns_per_row"]:g} ns/padded row; '
             f'{rows[0]["host_reduction_setup_ns"]:g} ns setup/GEMV; resident weights; no phase overlap.',
             ha='center', fontsize=8)
    fig.tight_layout(rect=(0,.1,1,1))
    fig.savefig(out / 'replication_reduction_estimate.png', dpi=180)
    plt.close(fig)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--host-reduction-ns-per-row', required=True, type=float)
    p.add_argument('--host-reduction-setup-ns', type=float, default=0)
    p.add_argument('--host-model-label', required=True,
                   help='State calibration source or explicitly label the illustrative assumption')
    p.add_argument('--plot', action='store_true')
    a = p.parse_args()
    if not a.host_model_label.strip():
        p.error('--host-model-label cannot be empty')
    source_rows, fingerprints = load_results(a.input)
    rows = [account(r, a.host_reduction_ns_per_row, a.host_reduction_setup_ns)
            for r in source_rows]
    if a.out.exists() and any(a.out.iterdir()):
        p.error('choose an empty output directory; source measurements must not be overwritten')
    a.out.mkdir(parents=True, exist_ok=True)
    with (a.out / 'accounted.csv').open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
    metadata = {
        'kind': 'simulated_backend_plus_explicit_host_estimate',
        'baseline_commit': 'd3981fca792301536146412a2a4d9ed52bb5251b',
        'input_directory': str(a.input.resolve()), 'input_sha256': fingerprints,
        'host_model_label': a.host_model_label,
        'host_reduction_ns_per_row': a.host_reduction_ns_per_row,
        'host_reduction_setup_ns': a.host_reduction_setup_ns,
        'traffic_counts_are_derived_not_measured_counters': True,
        'replication_already_in_kernel_time': True,
        'assumption': 'phases serialized; weights resident for resident column; host FP16->FP32 reduction',
        'host_reduction_model_contract': 'per-row cost includes local buffer access, conversions, sum and FP32 output store; setup once per GEMV',
        'host_reduction_timed_in_simulator': False,
        'paper_reproduction_complete': False,
        'cases': [[r['M'],r['K']] for r in rows],
    }
    (a.out / 'accounting_manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    if a.plot: plot(rows, a.out, a.host_model_label)
    print(f'PASS: accounted {len(rows)} cases; replication NOT charged twice')
    print(a.out / 'accounted.csv')
    print('Host reduction is an explicit estimate, not a measured gem5 CPU latency.')


if __name__ == '__main__':
    main()
