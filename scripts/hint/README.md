# HINT experiments

Run these commands from the repository root. The compile and simulation modes
generate `commands.out`; execute its commands with the usual batch runner.
Compilation reads the nine input traces from `benchmarks/bin/optimized/`.
Compile the traces before running simulations.

```bash
bash scripts/hint/run_all.sh compile
bash scripts/hint/run_all.sh sim
```

Both modes include the main and appendix workloads in the same EIF, EIF-LA,
and HINT command groups. The main workloads also have baseline and EPR
sensitivity simulations.

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

The simulator baseline (without `-rm`) also uses EPR pairs for all memory
accesses. Its ideal distribution starts with an empty buffer, holds at most
eight perfect pairs, and produces one pair every `4 * memory_code_distance`
memory cycles while the buffer has space. This ideal supply adds no physical
qubit footprint. Memory-side bus routing is removed; compute-side routing
contention and its footprint remain. `-rm` selects the existing EPR
distillation model instead of this ideal supply.

All compiled traces and results are written under
`benchmarks/bin/compiled/hint_isca2026/` and `out/hint_isca2026/`,
respectively. Individual command groups are also available, for example:

```bash
python3 -m scripts.hint.run_all_workloads compile_hint
python3 -m scripts.hint.run_all_workloads sim_hint
```

After all appendix simulations finish, generate the comparison table:

```bash
bash scripts/hint/run_all.sh appendix_report > out/hint_isca2026/appendix_comparison.csv
```

The CSV contains each policy's application throughput (`CLIENT 0/KIPS`) and
HINT's speedup relative to EIF and EIF-LA for every workload and capacity,
plus geometric mean speedups per capacity. A speedup above 1 means HINT is
faster. Reporting requires a positive, finite throughput from every run;
missing or invalid results fail the report instead of silently omitting runs.
