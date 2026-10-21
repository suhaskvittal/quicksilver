# HINT experiments

Run these commands from the repository root. The compile and simulation modes
generate `commands.out`; execute its commands with the usual batch runner.
Compile the traces before running simulations.

```bash
bash scripts/hint/run_all.sh compile
bash scripts/hint/run_all.sh sim
```

Both modes include the main evaluation and the appendix. To generate only the
appendix commands, use `appendix_compile` and `appendix_sim` instead.

The appendix compares EIF, EIF-LA, and HINT on the chromium, ethylene oxide,
and hc3h2cn SELECT subroutines (`_q_sel`) and the available Gidney 2025 Shor
trace (`BQ_gidney25_rsa2048_256_of_20805`). It uses capacities 8, 12, 16, 24,
and 32, lookahead depth 256 for EIF-LA and HINT, and the main evaluation's
instruction limits, factory budget, and reaction time. HINT enables coalescing
and nonarbitrary victim selection, as in the main evaluation.

Appendix memory syndrome extraction takes **1,250 ns (1.25 µs)**, explicitly
set by `APPENDIX_MEMORY_CYCLE_TIME_NS` in `run_all_workloads.py`. These runs
retain remote memory and its 50,000 physical qubit EPR budget. The main
evaluation continues to use 1,250,000 ns (1.25 ms).

Appendix compiled traces and results are isolated under
`benchmarks/bin/compiled/hint_isca2026_appendix/` and
`out/hint_isca2026_appendix/`, respectively. Individual command groups are
also available, for example:

```bash
python3 -m scripts.hint.run_all_workloads compile_appendix_hint
python3 -m scripts.hint.run_all_workloads sim_appendix_hint
```

After all appendix simulations finish, generate the comparison table:

```bash
bash scripts/hint/run_all.sh appendix_report > out/hint_isca2026_appendix/comparison.csv
```

The CSV contains each policy's application throughput (`CLIENT 0/KIPS`) and
HINT's speedup relative to EIF and EIF-LA for every workload and capacity,
plus geometric mean speedups per capacity. A speedup above 1 means HINT is
faster. Reporting requires a positive, finite throughput from every run;
missing or invalid results fail the report instead of silently omitting runs.
