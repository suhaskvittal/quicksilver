# author: Suhas Vittal

from scripts.common import *
from sys import argv

PROJECT = 'rdr_micro2026'

'''
    Simulation settings:
'''
SIM_INST_COUNT = 10_000_000
PRINT_PROGRESS = SIM_INST_COUNT
COMPILE_INST_COUNT = 5*SIM_INST_COUNT

def get_rdr_binary(w: str) -> str:
    name = get_workload_name(w)
    return f'benchmarks/bin/rdr/{name}.xz'

##############################################
##############################################

experiment = argv[1]

if experiment == 'compile_eif':
    for w in workload_list():
        run_memory_scheduler(get_rdr_binary(w), PROJECT, 'eif', active_set_capacity=4, inst_limit=COMPILE_INST_COUNT, scheduler_id=0)

if experiment == 'sim_baseline':
    for w in workload_list():
        for f in [20000, 40000, 60000, 80000, 100000]:
            _f = f//1000
            run_quicksilver(w, PROJECT, f'baseline_f{_f}', 'eif',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=4,
                            total_program_inst=get_total_inst_count_for_workload(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=f)

if experiment == 'sim_rdr':
    for w in workload_list():
        for f in [20000, 40000, 60000, 80000, 100000]:
            _f = f//1000
            run_quicksilver(w, PROJECT, f'rdr_f{_f}', 'eif',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=4,
                            total_program_inst=get_total_inst_count_for_workload(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=f,
                            kwargs={
                                '-rdr': 1,
                                '--rdr-capacity': 2,
                                '--rdr-start-layer': 2,
                                '--rdr-lookahead-depth': 8,
                                '--rdr-inst-delta-limit': 250
                            })
