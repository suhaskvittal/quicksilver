#!/bin/bash
# Generate one qs_report command per HINT workload for parallel execution.

set -euo pipefail

mkdir -p benchmarks/workload_reports
python3 -m scripts.hint.run_all_workloads report_commands > commands.out
