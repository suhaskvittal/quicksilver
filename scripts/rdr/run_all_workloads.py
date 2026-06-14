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

DEFAULT_RDR_DEGREE = 1
DEFAULT_RDR_LOOKAHEAD_DEPTH = 256
DEFAULT_RDR_COST_SCALE = 4.0

def get_rdr_binary(w: str) -> str:
    name = get_workload_name(w)
    return f'benchmarks/bin/rdr/{name}.xz'

##############################################
##############################################

experiment = argv[1]

if experiment == 'compile_eif':
    for w in workload_list():
        run_memory_scheduler(w, PROJECT, 'eif', active_set_capacity=4, inst_limit=COMPILE_INST_COUNT, scheduler_id=0)

if experiment == 'sim_baseline':
    for w in workload_list():
        for f in range(30000, 80000, 5000):
            _f = f//1000
            run_quicksilver(w, PROJECT, f'baseline_f{_f}k', 'eif',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=4,
                            total_program_inst=get_total_inst_count_for_workload(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=f)

if experiment == 'sim_rdr':
    for w in workload_list():
        for f in range(30000, 80000, 5000):
            _f = f//1000
            run_quicksilver(w, PROJECT, f'rdr_f{_f}k', 'eif_rdr',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=4,
                            total_program_inst=get_total_inst_count_for_workload(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=f,
                            kwargs={
                                '-rdr': 1,
                                '--rdr-capacity': 2,
                                '--rdr-start-layer': 2,
                                '--rdr-lookahead-depth': DEFAULT_RDR_LOOKAHEAD_DEPTH,
                                '--rdr-degree': DEFAULT_RDR_DEGREE,
                                '--rdr-completion-buffer-capacity': 4,
                                '--rdr-fixed-lookahead': '',
                                '--rdr-cost-scale': DEFAULT_RDR_COST_SCALE
                            })

if experiment == 'sim_rltp':
    for w in workload_list():
        for f in range(30000, 80000, 5000):
            for rltp in [4,6]:
                _f = f//1000
                run_quicksilver(w, PROJECT, f'rltp_{rltp}_f{_f}k', 'eif',
                                inst_limit=SIM_INST_COUNT,
                                active_set_capacity=4,
                                total_program_inst=get_total_inst_count_for_workload(w),
                                print_progress=PRINT_PROGRESS,
                                factory_budget=f,
                                kwargs={
                                    '--rltp-degree': rltp,
                                })

if experiment == 'sim_baseline_tr_sens':
    for w in workload_list():
        for tr in [1,23]:
            for f in range(30000, 80000, 5000):
                _f = f//1000
                run_quicksilver(w, PROJECT, f'baseline_f{_f}k_tr{tr}', 'eif',
                                inst_limit=SIM_INST_COUNT,
                                active_set_capacity=4,
                                total_program_inst=get_total_inst_count_for_workload(w),
                                print_progress=PRINT_PROGRESS,
                                factory_budget=f,
                                kwargs={'--reaction-time': tr})

if experiment == 'sim_rltp_tr_sens':
    for w in workload_list():
        for rltp in [4, 6]:
            for tr in [1, 23]:
                for f in range(30000, 80000, 5000):
                    _f = f//1000
                    run_quicksilver(w, PROJECT, f'rltp_{rltp}_f{_f}k_tr{tr}', 'eif',
                                    inst_limit=SIM_INST_COUNT,
                                    active_set_capacity=4,
                                    total_program_inst=get_total_inst_count_for_workload(w),
                                    print_progress=PRINT_PROGRESS,
                                    factory_budget=f,
                                    kwargs={
                                        '--rltp-degree': rltp,
                                        '--reaction-time': tr
                                    })

if experiment == 'sim_rdr_tr_sens':
    for w in workload_list():
        for tr in [1, 23]:
            for f in range(30000, 80000, 5000):
                _f = f//1000
                run_quicksilver(w, PROJECT, f'rdr_f{_f}k_tr{tr}', 'eif_rdr',
                                inst_limit=SIM_INST_COUNT,
                                active_set_capacity=4,
                                total_program_inst=get_total_inst_count_for_workload(w),
                                print_progress=PRINT_PROGRESS,
                                factory_budget=f,
                                kwargs={
                                    '-rdr': 1,
                                    '--rdr-capacity': 2,
                                    '--rdr-start-layer': 2,
                                    '--rdr-lookahead-depth': DEFAULT_RDR_LOOKAHEAD_DEPTH,
                                    '--rdr-degree': DEFAULT_RDR_DEGREE,
                                    '--rdr-completion-buffer-capacity': 4,
                                    '--rdr-fixed-lookahead': '',
                                    '--reaction-time': tr,
                                    '--rdr-cost-scale': DEFAULT_RDR_COST_SCALE
                                })

if experiment == 'sim_rltp_9_11':
    for w in workload_list():
        for f in range(50000, 110000, 5000):
            for rltp in [8]:
                _f = f//1000
                run_quicksilver(w, PROJECT, f'rltp_{rltp}_f{_f}k', 'eif',
                                inst_limit=SIM_INST_COUNT,
                                active_set_capacity=4,
                                total_program_inst=get_total_inst_count_for_workload(w),
                                print_progress=PRINT_PROGRESS,
                                factory_budget=f,
                                kwargs={
                                    '--rltp-degree': rltp,
                                })


if experiment == 'sim_rdr_with_rltp':
    for w in workload_list():
        for f in range(50000, 110000, 5000):
            _f = f//1000
            run_quicksilver(w, PROJECT, f'rdr_with_rltp_f{_f}k', 'eif_rdr',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=4,
                            total_program_inst=get_total_inst_count_for_workload(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=f,
                            kwargs={
                                '-rdr': 1,
                                '--rdr-capacity': 2,
                                '--rdr-start-layer': 2,
                                '--rdr-lookahead-depth': DEFAULT_RDR_LOOKAHEAD_DEPTH,
                                '--rdr-degree': DEFAULT_RDR_DEGREE,
                                '--rdr-completion-buffer-capacity': 4,
                                '--rdr-fixed-lookahead': '',
                                '--rltp-degree': 4,
                                '--rdr-cost-scale': DEFAULT_RDR_COST_SCALE
                            })
