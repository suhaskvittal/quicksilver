# HINT experiments

Run these commands from the repository root. The compile and simulation modes
generate `commands.out`; execute its commands with the usual batch runner.
Compilation reads the 17 input traces from `benchmarks/bin/optimized/`.
Compile the traces before running simulations.

```bash
bash scripts/hint/run_all.sh compile
bash scripts/hint/run_all.sh sim
```

Every command group uses the same 17 workloads: Beauregard Shor, Gidney
Shor, Grover, the chromium, ethylene oxide, and hc3h2cn Trotter, SELECT, and
prepare traces, the Bose Hubbard and manganese nitride prepare traces, and
random, power-law, and 3-regular QAOA.
All workloads enter the baseline, EIF, EIF-LA, HINT, and EPR
sensitivity studies. The capacity sweep is 8, 16, 24, 32, 48, and 64.
The EPR sensitivity study uses capacity 16.
EIF-LA and HINT use lookahead depth 64 in simulation; the compiler also
sweeps depths 8, 16, 32, 64, 128, 256, and 512. Depth 64 compiles 200 million
instructions for simulation; the other lookahead depths compile 20 million
instructions each. HINT enables coalescing and nonarbitrary victim selection.

All remote-memory runs use **1,250,000 ns (1.25 ms)** memory syndrome
extraction and a 50,000 physical qubit EPR budget for the standard policies.
The ideal baseline uses 1,250 ns.

The simulator baseline (without `-rm`) also uses EPR pairs for all memory
accesses. Its ideal distribution starts with an empty buffer, holds at most
eight perfect pairs, and produces one pair every `4 * memory_code_distance`
memory cycles while the buffer has space. This ideal supply adds no physical
qubit footprint. Memory-side bus routing is removed; compute-side routing
contention and its footprint remain. `-rm` selects the existing EPR
distillation model instead of this ideal supply.

All compiled traces and results are written under
`benchmarks/bin/compiled/hint_micro2026/` and `out/hint_micro2026/`,
respectively. Individual command groups are also available, for example:

```bash
python3 -m scripts.hint.run_all_workloads compile_hint
python3 -m scripts.hint.run_all_workloads sim_hint
```

After all simulations finish, generate the comparison table:

```bash
bash scripts/hint/run_all.sh report > out/hint_micro2026/comparison.csv
```

The CSV contains each policy's application throughput (`CLIENT 0/KIPS`) and
HINT's speedup relative to EIF and EIF-LA for every workload and capacity,
plus geometric mean speedups per capacity. A speedup above 1 means HINT is
faster. Reporting requires a positive, finite throughput from every run;
missing or invalid results fail the report instead of silently omitting runs.
