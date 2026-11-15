# author: Suhas Vittal

from scripts.common import *
from functools import cache
from pathlib import Path
import shlex
from sys import argv

##############################################
##############################################

PROJECT = 'hint_micro2026'
WORKLOAD_REPORT_DIR = Path('benchmarks/workload_reports')

'''
    Use the optimized traces for every study. This intentionally does not use
    `workload_list()` from `scripts/common.py`, which still names `_q.xz` traces
    that have been split into `_q_prep` and `_q_sel`.
'''
WORKLOADS = [
    'benchmarks/bin/optimized/BQ_beauregard_shor_rsa256_iter_4.xz',
    'benchmarks/bin/optimized/BQ_chromium_t.xz',
    'benchmarks/bin/optimized/BQ_c2h4o_ethylene_oxide_t.xz',
    'benchmarks/bin/optimized/BQ_hc3h2cn_t.xz',
    'benchmarks/bin/optimized/BQ_grover_3sat_schoning_1710.xz',
    'benchmarks/bin/optimized/BQ_chromium_q_prep.xz',
    'benchmarks/bin/optimized/BQ_chromium_q_sel.xz',
    'benchmarks/bin/optimized/BQ_c2h4o_ethylene_oxide_q_prep.xz',
    'benchmarks/bin/optimized/BQ_c2h4o_ethylene_oxide_q_sel.xz',
    'benchmarks/bin/optimized/BQ_hc3h2cn_q_prep.xz',
    'benchmarks/bin/optimized/BQ_hc3h2cn_q_sel.xz',
    'benchmarks/bin/optimized/BQ_bose_hubbard_q_prep.xz',
    'benchmarks/bin/optimized/BQ_manganese_nitride_q_prep.xz',
    'benchmarks/bin/optimized/BQ_gidney25_rsa2048_256_of_20805.xz',
    'benchmarks/bin/optimized/BQ_qaoa_random.xz',
    'benchmarks/bin/optimized/BQ_qaoa_powerlaw.xz',
    'benchmarks/bin/optimized/BQ_qaoa_3regular.xz',
]

'''
    Simulation settings:
'''
SIM_INST_COUNT = 100_000_000
PRINT_PROGRESS = SIM_INST_COUNT
FACTORY_BUDGET = 150_000
REACTION_TIME = 27

DEFAULT_CAPACITY = 16
CAP_ARRAY = [8, 16, 32, 48, 64]

REGIME = 'T'
MEMORY_CYCLE_TIME_NS = 1_250_000   # remote memory: ~1042x the 1200 ns compute default
BASELINE_MEMORY_CYCLE_TIME_NS = 1_250

# Remote-memory runs can go a long stretch (tens of thousands of cycles) between
# EPR-gated memory accesses becoming ready, which trips the simulator's default
# 1,000,000-cycle deadlock detector even though the access is about to become
# ready. Raise it 10x for every sim run in this study.
MAX_CYCLES_NO_PROGRESS = 10_000_000

# EPR_UNIT is a physical-qubit budget for entanglement distillation (matches
# `--remote-memory-epr-budget`, not a count of ED units). The base remote-memory
# runs use 2*EPR_UNIT = 50k physical qubits; the ED sensitivity sweep scales the
# same unit by u in {4, 8, 16} -> 100k / 200k / 400k.
EPR_UNIT = 50_000
EPR_BUDGET = 2*EPR_UNIT

'''
    Compiler settings:
'''
HINT_LOOKAHEAD_DEPTH = 256
EIF_LA_LOOKAHEAD_DEPTH = 256
LOOKAHEAD_DEPTHS = [16, 32, 64, 128, 256, 512]
COMPILE_INST_COUNT = int(2*SIM_INST_COUNT)
LOOKAHEAD_SWEEP_INST_COUNT = 20_000_000

OPT_INST_COUNT = 100

##############################################
##############################################

def remote_kwargs(epr_budget=EPR_BUDGET, memory_cycle_time_ns=MEMORY_CYCLE_TIME_NS) -> dict:
    return {
        '-rm': '',
        '--memory-cycle-time-ns': memory_cycle_time_ns,
        '--remote-memory-ed-protocol-id': 1,
        '--remote-memory-epr-budget': epr_budget,
        '--max-cycles-no-progress': MAX_CYCLES_NO_PROGRESS,
        '--reaction-time': REACTION_TIME,
        '-skip': 1,
        '--compute-code-distance': 25,
        '--memory-code-distance': 24
    }

def local_kwargs(memory_cycle_time_ns=BASELINE_MEMORY_CYCLE_TIME_NS) -> dict:
    return {
        '--memory-cycle-time-ns': memory_cycle_time_ns,
        '--max-cycles-no-progress': MAX_CYCLES_NO_PROGRESS,
        '--reaction-time': REACTION_TIME,
        '-skip': 1,
        '--compute-code-distance': 25,
        '--memory-code-distance': 24,
    }

@cache
def total_program_instructions(workload: str) -> int:
    report_path = WORKLOAD_REPORT_DIR / f'{get_workload_name(workload)}.out'
    if not report_path.is_file():
        raise FileNotFoundError(f'{report_path}: run scripts/generate_workload_reports.sh, then execute commands.out')
    for line in report_path.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == 'UNROLLED_INST_COUNT':
            count = int(fields[1])
            return count
    raise ValueError(f'{report_path}: qs_report did not report UNROLLED_INST_COUNT')


def report_commands():
    for workload in WORKLOADS:
        report_path = WORKLOAD_REPORT_DIR / f'{get_workload_name(workload)}.out'
        command = shlex.join(('./build/qs_report', workload, '-pp', '1000000000000'))
        print(f'{command} > {shlex.quote(str(report_path))}')

##############################################
##############################################

def report_comparison():
    import csv
    import math
    import sys
    from scripts.plotting_util import parse_stats_file

    # Use application instruction throughput for every workload.
    # Buffer rows so a missing/invalid run cannot produce a partial CSV report.
    rows = []
    for a in CAP_ARRAY:
        speedups = []
        for w in WORKLOADS:
            name = get_workload_name(w)
            kips = {}
            for policy in ('eif', 'eif_la', 'hint'):
                path = f'out/{PROJECT}/simulation_results/{policy}_a{a}/{name}.out'
                stats = parse_stats_file(path)
                value = float(stats['CLIENT 0']['KIPS'])
                if not math.isfinite(value) or value <= 0:
                    raise ValueError(f'{path}: expected positive, finite CLIENT 0/KIPS')
                kips[policy] = value
            ratios = (kips['hint'] / kips['eif'], kips['hint'] / kips['eif_la'])
            speedups.append(ratios)
            rows.append((name, a, MEMORY_CYCLE_TIME_NS,
                         kips['eif'], kips['eif_la'], kips['hint'], *ratios))
        means = [math.exp(sum(math.log(s[i]) for s in speedups) / len(speedups))
                 for i in range(2)]
        rows.append(('geomean', a, MEMORY_CYCLE_TIME_NS, '', '', '', *means))
    writer = csv.writer(sys.stdout)
    writer.writerow(('workload', 'capacity', 'memory_cycle_time_ns',
                     'eif_kips', 'eif_la_kips', 'hint_kips',
                     'hint_speedup_vs_eif', 'hint_speedup_vs_eif_la'))
    writer.writerows(rows)


experiment = argv[1]

''' Compilation for EIF and HINT '''
if experiment == 'report_comparison':
    report_comparison()

elif experiment == 'report_commands':
    report_commands()

elif experiment == 'compile_eif':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            run_memory_scheduler(w, PROJECT, f'eif_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0)

elif experiment == 'compile_eif_la':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            depths = LOOKAHEAD_DEPTHS if (a == DEFAULT_CAPACITY) else [EIF_LA_LOOKAHEAD_DEPTH]
            for ld in depths:
                run_memory_scheduler(w, PROJECT, f'eif_la_ld{ld}_a{a}', active_set_capacity=a,
                                    inst_limit=COMPILE_INST_COUNT,
                                    scheduler_id=0,
                                    kwargs={
                                        '--eif-lookahead-depth': ld
                                    })

elif experiment == 'compile_hint':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            depths = LOOKAHEAD_DEPTHS if (a == DEFAULT_CAPACITY) else [HINT_LOOKAHEAD_DEPTH]
            for ld in depths:
                run_memory_scheduler(w, PROJECT, f'hint_ld{ld}_a{a}', active_set_capacity=a,
                                    inst_limit=COMPILE_INST_COUNT,
                                    scheduler_id=1,
                                    kwargs={
                                        '--hint-lookahead-depth': ld,
                                        '--hint-use-coalescing': '',
                                        '--hint-use-nonarbitrary-victim-selection': ''
                                    })

elif experiment == 'compile_test_opt':
    # NOTE: ./build/qs_memory_optimality only exists when CMake is configured
    # with -DCOMPILE_ILP_OPT=ON (it depends on OR-Tools).
    for w in WORKLOADS:
        run_memory_optimality(w, PROJECT, f'opt', active_set_capacity=4, inst_limit=OPT_INST_COUNT)

elif experiment == 'sim_baseline':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'baseline_a{a}', f'eif_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=local_kwargs())

elif experiment == 'sim_eif':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'eif_a{a}', f'eif_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs())

elif experiment == 'sim_eif_la':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'eif_la_a{a}', f'eif_la_ld{EIF_LA_LOOKAHEAD_DEPTH}_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs())

elif experiment == 'sim_hint':
    for w in WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'hint_a{a}', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs())

elif experiment == 'sim_eif_ed_sensitivity':
    for w in WORKLOADS:
        for epr_unit_count in [1, 4, 8, 16]:
            run_quicksilver(w, PROJECT, f'eif_ed_sensitivity_u{epr_unit_count}', f'eif_a{DEFAULT_CAPACITY}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=DEFAULT_CAPACITY,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs(epr_unit_count*EPR_UNIT))
        run_quicksilver(w, PROJECT, f'eif_ed_sensitivity_perfect', f'eif_a{DEFAULT_CAPACITY}',
                             inst_limit=SIM_INST_COUNT,
                             active_set_capacity=DEFAULT_CAPACITY,
                             total_program_inst=total_program_instructions(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs=remote_kwargs(100*EPR_UNIT))

elif experiment == 'sim_eif_la_ed_sensitivity':
    for w in WORKLOADS:
        for epr_unit_count in [1, 4, 8, 16]:
            run_quicksilver(w, PROJECT, f'eif_la_ed_sensitivity_u{epr_unit_count}', f'eif_la_ld{EIF_LA_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=DEFAULT_CAPACITY,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs(epr_unit_count*EPR_UNIT))
        run_quicksilver(w, PROJECT, f'eif_la_ed_sensitivity_perfect', f'eif_la_ld{EIF_LA_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                             inst_limit=SIM_INST_COUNT,
                             active_set_capacity=DEFAULT_CAPACITY,
                             total_program_inst=total_program_instructions(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs=remote_kwargs(100*EPR_UNIT))

elif experiment == 'sim_hint_ed_sensitivity':
    for w in WORKLOADS:
        for epr_unit_count in [1, 4, 8, 16]:
            run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_u{epr_unit_count}', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=DEFAULT_CAPACITY,
                                 total_program_inst=total_program_instructions(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs(epr_unit_count*EPR_UNIT))
        run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_perfect', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                             inst_limit=SIM_INST_COUNT,
                             active_set_capacity=DEFAULT_CAPACITY,
                             total_program_inst=total_program_instructions(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs=remote_kwargs(100*EPR_UNIT))

else:
    print(f'unknown experiment: {experiment}')
    exit(1)

##############################################
##############################################
