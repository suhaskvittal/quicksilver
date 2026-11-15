#!/bin/bash
# author: Suhas Vittal

set -e

MODE="$1"

case "$MODE" in
    compile)
        EXPERIMENTS=(
            compile_eif
            compile_eif_la
            compile_hint
        )
        ;;
    sim)
        EXPERIMENTS=(
            sim_baseline
            sim_eif
            sim_eif_la
            sim_hint
            sim_eif_ed_sensitivity
            sim_eif_la_ed_sensitivity
            sim_hint_ed_sensitivity
        )
        ;;
    report)
        exec python3 -m scripts.hint.run_all_workloads report_comparison
        ;;
    *)
        echo "usage: $0 {compile|sim|report}" >&2
        exit 1
        ;;
esac

rm -f commands.out
touch commands.out

for exp in "${EXPERIMENTS[@]}"; do
    echo "Running experiment: $exp"
    python3 -m scripts.hint.run_all_workloads $exp >> commands.out
done
