#!/usr/bin/env python3
"""
Runs `qs_v_synthesis_estimate` over every benchmark binary in `benchmarks/bin/`
and reports the estimated reduction in non-Clifford gate count if Rz/Rx
synthesis targeted Clifford+V (V = sqrt(T)) instead of Clifford+T.

See main/qs_v_synthesis_estimate.cpp for the underlying model:
    T-count = 0.53 * log2(1/eps)
    V-count = 0.23 * log2(1/eps)
Per-rotation T-count is taken from the benchmark's stored synthesis; the
V-count is predicted by inverting the T curve for the implied precision.
"""

import argparse
import csv
import glob
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run_one(exe, bin_file, csv_file):
    result = subprocess.run([exe, bin_file, "-csv", csv_file],
                            capture_output=True, text=True)
    return bin_file, result.returncode, result.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin-dir", default=os.path.join(REPO_ROOT, "benchmarks", "bin"),
                    help="Directory of benchmark binaries (non-recursive glob of *.xz/*.gz)")
    ap.add_argument("--exe", default=os.path.join(REPO_ROOT, "build", "qs_v_synthesis_estimate"),
                    help="Path to the qs_v_synthesis_estimate executable")
    ap.add_argument("--csv", default=os.path.join(REPO_ROOT, "scripts", "v_synthesis_experiment.csv"),
                    help="Output CSV file (overwritten at the start of each run)")
    ap.add_argument("--jobs", type=int, default=4, help="Parallel worker count")
    args = ap.parse_args()

    if not os.path.isfile(args.exe):
        sys.exit(f"error: executable not found: {args.exe} "
                f"(build it with: cmake --build build --target qs_v_synthesis_estimate)")

    bin_files = sorted(
        glob.glob(os.path.join(args.bin_dir, "*.xz")) +
        glob.glob(os.path.join(args.bin_dir, "*.gz"))
    )
    if not bin_files:
        sys.exit(f"error: no *.xz/*.gz files found in {args.bin_dir}")

    # start each sweep from a clean CSV so re-runs don't append duplicate rows.
    if os.path.exists(args.csv):
        os.remove(args.csv)

    failures = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, args.exe, f, args.csv) for f in bin_files]
        for fut in as_completed(futures):
            bin_file, rc, stderr = fut.result()
            if rc != 0:
                failures.append((bin_file, rc, stderr))

    if failures:
        print("FAILURES:", file=sys.stderr)
        for bin_file, rc, stderr in failures:
            print(f"  {os.path.basename(bin_file)}: exit {rc}\n{stderr}", file=sys.stderr)

    if not os.path.exists(args.csv):
        sys.exit("error: no CSV rows were produced (all runs failed)")

    with open(args.csv, newline="") as f:
        rows = list(csv.DictReader(f))
    rows.sort(key=lambda r: r["benchmark"])

    # column widths
    name_w = max(len(r["benchmark"]) for r in rows) + 2
    header = (f'{"benchmark":<{name_w}}{"baseline":>14}{"V-model":>14}'
            f'{"reduction":>14}{"% reduction":>14}')
    print(header)
    print("-" * len(header))

    total_baseline = total_vmodel = total_reduction = 0
    for r in rows:
        baseline = int(r["baseline_nonclifford"])
        vmodel = int(r["v_model_nonclifford"])
        reduction = int(r["abs_reduction"])
        pct = float(r["pct_reduction"])
        total_baseline += baseline
        total_vmodel += vmodel
        total_reduction += reduction
        print(f'{r["benchmark"]:<{name_w}}{baseline:>14}{vmodel:>14}{reduction:>14}{pct:>13.2f}%')

    print("-" * len(header))
    total_pct = 100.0 * total_reduction / total_baseline if total_baseline else 0.0
    print(f'{"TOTAL":<{name_w}}{total_baseline:>14}{total_vmodel:>14}{total_reduction:>14}{total_pct:>13.2f}%')

    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
