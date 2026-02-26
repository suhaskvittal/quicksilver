# author: Suhas Vittal

from scripts.common import *
from sys import argv

##############################################
##############################################

PROJECT = 'hint_isca2026'

'''
    Simulation settings:
'''
SIM_INST_COUNT = 100_000_000
PRINT_PROGRESS = SIM_INST_COUNT

'''
    Compiler settings:
'''
HINT_LOOKAHEAD_DEPTH = 512
COMPILE_INST_COUNT = int(1.2*SIM_INST_COUNT)

##############################################
##############################################

experiment = argv[1]

''' Compilation for EIF and HINT '''
if experiment == 'compile_eif':
    for w in workload_list():
        for a in [8, 12, 16, 20, 24, 32]:
            run_memory_scheduler(w, PROJECT, f'eif_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0)

if experiment == 'compile_hint':
    for w in workload_list():
        for a in [8, 12, 16, 20, 24, 32]:
            run_memory_scheduler(w, PROJECT, f'hint_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=1, 
                                kwargs={
                                    '--hint-lookahead-depth': 512
                                })

if experiment == 'sim_eif':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            run_memory_scheduler(w, PROJECT, f'eif_a{a}', 
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                                    '-epr': `
                                 })


##############################################
##############################################
