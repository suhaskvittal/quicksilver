#!/bin/bash
# author: Suhas Vittal

set -e

MODE="$1"

case "$MODE" in
    compile)
        EXPERIMENTS=(
            compile_eif
        )
        ;;
    sim)
        EXPERIMENTS=(
            sim_baseline
            sim_cat
            sim_footprint
            sim_nomem
        )
        ;;
    *)
        echo "usage: $0 {compile|sim}" >&2
        exit 1
        ;;
esac

rm -f commands.out
touch commands.out

for exp in "${EXPERIMENTS[@]}"; do
    echo "Running experiment: $exp"
    python3 -m scripts.oclk.run_all_workloads $exp >> commands.out
done
