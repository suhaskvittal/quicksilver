#!/bin/sh

rm commands.out
touch commands.out

# baseline results:
python3 scripts/dms/run_all_workloads.py baseline 12 1 viszlai >> commands.out
python3 scripts/dms/run_all_workloads.py viszlai 12 1000 viszlai >> commands.out
python3 scripts/dms/run_all_workloads.py hint 12 1000 hint >> commands.out

# memory slowdown motivation:
python3 scripts/dms/run_all_workloads.py baseline_5x 12 5 viszlai >> commands.out
python3 scripts/dms/run_all_workloads.py baseline_10x 12 10 viszlai >> commands.out
python3 scripts/dms/run_all_workloads.py baseline_100x 12 100 viszlai >> commands.out
