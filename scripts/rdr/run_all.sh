#!/bin/bash
# author: Suhas Vittal

set -e

EXPERIMENTS=(
    sim_baseline
    sim_rdr
    sim_rltp
    sim_rdr_fixed_lookahead
    sim_baseline_tr_sens
    sim_rltp_tr_sens
    sim_rdr_tr_sens
#   sim_rltp_9_11
#   sim_rdr_with_rltp
    sim_baseline_na
    sim_rltp_na
    sim_rdr_na
)

rm commands.out
touch commands.out

for exp in "${EXPERIMENTS[@]}"; do
    echo "Running experiment: $exp"
    python3 -m scripts.rdr.run_all_workloads $exp >> commands.out
done
