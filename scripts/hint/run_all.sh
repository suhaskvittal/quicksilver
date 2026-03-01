#!/bin/bash

rm -rf commands.out
touch commands.out

if [ "$1" == "compile" ]; then
    python3 -m scripts.hint.run_all_workloads compile_eif >> commands.out
    python3 -m scripts.hint.run_all_workloads compile_hint >> commands.out

elif [ "$1" == "sim" ]; then
    python3 -m scripts.hint.run_all_workloads sim_baseline >> commands.out
    python3 -m scripts.hint.run_all_workloads sim_eif >> commands.out
    python3 -m scripts.hint.run_all_workloads sim_hint >> commands.out
    python3 -m scripts.hint.run_all_workloads sim_hint_ed_sensitivity >> commands.out
    python3 -m scripts.hint.run_all_workloads sim_eif_mismatch_sensitivity >> commands.out
    python3 -m scripts.hint.run_all_workloads sim_hint_mismatch_sensitivity >> commands.out
    python3 -m scripts.hint.run_all_workloads sim_hint_factory_sensitivity >> commands.out

else
    echo "Usage: $0 [compile|sim]"
    exit 1
fi
