# author: Suhas Vittal

from scripts.common import *
from sys import argv

##############################################
##############################################

PROJECT = 'hint_isca2026'

'''
    Workloads targeted by this study: beauregard Shor, three trotterization
    chemistry benchmarks (chromium, ethylene oxide, hc3h2cn), and Grover.

    NOTE: this intentionally does not use `workload_list()` from
    `scripts/common.py` -- that shared list still contains `_q.xz` traces that
    no longer exist on disk (split into `_q_prep`/`_q_sel`), and is kept as-is
    for `scripts/rdr/`.
'''
HINT_WORKLOADS = [
    'benchmarks/bin/BQ_beauregard_shor_rsa256_iter_4.xz',
    'benchmarks/bin/BQ_chromium_t.xz',
    'benchmarks/bin/BQ_c2h4o_ethylene_oxide_t.xz',
    'benchmarks/bin/BQ_hc3h2cn_t.xz',
    'benchmarks/bin/BQ_grover_3sat_schoning_1710.xz',
]

'''
    Simulation settings:
'''
SIM_INST_COUNT = 100_000_000
PRINT_PROGRESS = SIM_INST_COUNT
FACTORY_BUDGET = 75000
REACTION_TIME = 30

DEFAULT_CAPACITY = 12
CAP_ARRAY = [8, 12, 16, 24, 32]

REGIME = 'T'
MEMORY_CYCLE_TIME_NS = 1_250_000   # remote memory: ~1042x the 1200 ns compute default

# Remote-memory runs can go a long stretch (tens of thousands of cycles) between
# EPR-gated memory accesses becoming ready, which trips the simulator's default
# 1,000,000-cycle deadlock detector even though the access is about to become
# ready. Raise it 10x for every sim run in this study.
MAX_CYCLES_NO_PROGRESS = 10_000_000

# EPR_UNIT is a physical-qubit budget for entanglement distillation (matches
# `--remote-memory-epr-budget`, not a count of ED units). The base remote-memory
# runs use 2*EPR_UNIT = 50k physical qubits; the ED sensitivity sweep scales the
# same unit by u in {4, 8, 16} -> 100k / 200k / 400k.
EPR_UNIT = 25_000
EPR_BUDGET = 2*EPR_UNIT

'''
    Compiler settings:
'''
HINT_LOOKAHEAD_DEPTH = 256
COMPILE_INST_COUNT = int(2*SIM_INST_COUNT)

OPT_INST_COUNT = 100

##############################################
##############################################

def remote_kwargs(epr_budget=EPR_BUDGET) -> dict:
    return {
        '--regime': REGIME,
        '-rm': '',
        '--memory-cycle-time-ns': MEMORY_CYCLE_TIME_NS,
        '--remote-memory-epr-budget': epr_budget,
        '--max-cycles-no-progress': MAX_CYCLES_NO_PROGRESS,
        '--reaction-time': REACTION_TIME,
        '-skip': 1
    }

def local_kwargs(memory_cycle_time_ns=MEMORY_CYCLE_TIME_NS) -> dict:
    return {
        '--regime': REGIME,
        '--memory-cycle-time-ns': memory_cycle_time_ns,
        '--max-cycles-no-progress': MAX_CYCLES_NO_PROGRESS,
        '--reaction-time': REACTION_TIME
    }

##############################################
##############################################

experiment = argv[1]

''' Compilation for EIF and HINT '''
if experiment == 'compile_eif':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            run_memory_scheduler(w, PROJECT, f'eif_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0)

elif experiment == 'compile_eif_la':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            for ld in [8, 16, 32, 64, 128, 256, 512]:
                run_memory_scheduler(w, PROJECT, f'eif_la_ld{ld}_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0,
                                    kwargs={
                                        '--eif-lookahead-depth': ld
                                    })

elif experiment == 'compile_hint':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            for ld in [8, 16, 32, 64, 128, 256, 512]:
                run_memory_scheduler(w, PROJECT, f'hint_ld{ld}_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=1,
                                    kwargs={
                                        '--hint-lookahead-depth': ld,
                                        '--hint-use-coalescing': '',
                                        '--hint-use-nonarbitrary-victim-selection': ''
                                    })

elif experiment == 'compile_test_opt':
    # NOTE: ./build/qs_memory_optimality only exists when CMake is configured
    # with -DCOMPILE_ILP_OPT=ON (it depends on OR-Tools).
    for w in HINT_WORKLOADS:
        run_memory_optimality(w, PROJECT, f'opt', active_set_capacity=4, inst_limit=OPT_INST_COUNT)

elif experiment == 'sim_baseline':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'baseline_a{a}', f'eif_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=local_kwargs(1250))

elif experiment == 'sim_eif':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'eif_a{a}', f'eif_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs())

elif experiment == 'sim_eif_la':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'eif_la_a{a}', f'eif_la_ld{HINT_LOOKAHEAD_DEPTH}_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs())

elif experiment == 'sim_hint':
    for w in HINT_WORKLOADS:
        for a in CAP_ARRAY:
            run_quicksilver(w, PROJECT, f'hint_a{a}', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{a}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs())

elif experiment == 'sim_eif_ed_sensitivity':
    for w in HINT_WORKLOADS:
        for epr_unit_count in [4, 8, 16]:
            run_quicksilver(w, PROJECT, f'eif_ed_sensitivity_u{epr_unit_count}', f'eif_a{DEFAULT_CAPACITY}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=DEFAULT_CAPACITY,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs(epr_unit_count*EPR_UNIT))
        run_quicksilver(w, PROJECT, f'eif_ed_sensitivity_perfect', f'eif_a{DEFAULT_CAPACITY}',
                             inst_limit=SIM_INST_COUNT,
                             active_set_capacity=DEFAULT_CAPACITY,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs=remote_kwargs(100*EPR_UNIT))

elif experiment == 'sim_eif_la_ed_sensitivity':
    for w in HINT_WORKLOADS:
        for epr_unit_count in [4, 8, 16]:
            run_quicksilver(w, PROJECT, f'eif_la_ed_sensitivity_u{epr_unit_count}', f'eif_la_ld{HINT_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=DEFAULT_CAPACITY,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs(epr_unit_count*EPR_UNIT))
        run_quicksilver(w, PROJECT, f'eif_la_ed_sensitivity_perfect', f'eif_la_ld{HINT_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                             inst_limit=SIM_INST_COUNT,
                             active_set_capacity=DEFAULT_CAPACITY,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs=remote_kwargs(100*EPR_UNIT))

elif experiment == 'sim_hint_ed_sensitivity':
    for w in HINT_WORKLOADS:
        for epr_unit_count in [4, 8, 16]:
            run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_u{epr_unit_count}', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                                 inst_limit=SIM_INST_COUNT,
                                 active_set_capacity=DEFAULT_CAPACITY,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs=remote_kwargs(epr_unit_count*EPR_UNIT))
        run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_perfect', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{DEFAULT_CAPACITY}',
                             inst_limit=SIM_INST_COUNT,
                             active_set_capacity=DEFAULT_CAPACITY,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs=remote_kwargs(100*EPR_UNIT))

else:
    print(f'unknown experiment: {experiment}')
    exit(1)

##############################################
##############################################
