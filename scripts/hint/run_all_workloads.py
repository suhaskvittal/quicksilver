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
EPR_BULK_COUNT = 3472
FACTORY_BUDGET = 50000

'''
    Compiler settings:
'''
HINT_LOOKAHEAD_DEPTH = 512
COMPILE_INST_COUNT = int(2*SIM_INST_COUNT)

##############################################
##############################################

experiment = argv[1]

''' Compilation for EIF and HINT '''
if experiment == 'compile_eif':
    for w in workload_list():
        for a in [8, 12, 16, 20, 24, 32]:
            run_memory_scheduler(w, PROJECT, f'eif_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0,
                                 kwargs={'-rpc': 1})

elif experiment == 'compile_hint':
    for w in workload_list():
        for a in [8, 12, 16, 20, 24, 32]:
            run_memory_scheduler(w, PROJECT, f'hint_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=1, 
                                kwargs={
                                    '-rpc': 1,
                                    '--hint-lookahead-depth': 512
                                })

elif experiment == 'sim_baseline':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            run_quicksilver(w, PROJECT, f'baseline_a{a}', f'eif_a{a}',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs={
                                    '--memory-syndrome-extraction-round-time-ns': 1250,
                                 })

elif experiment == 'sim_eif':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            run_quicksilver(w, PROJECT, f'eif_a{a}', f'eif_a{a}',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                                    '-epr': 2*EPR_BULK_COUNT,
                                    '--epr-ll-buffer-capacity': 1,
                                    '--substrate-mismatch-factor': 1000
                                 })

elif experiment == 'sim_hint':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            run_quicksilver(w, PROJECT, f'hint_a{a}', f'hint_a{a}',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=a,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                                    '-epr': 2*EPR_BULK_COUNT,
                                    '--epr-ll-buffer-capacity': 1,
                                    '--substrate-mismatch-factor': 1000
                                 })

elif experiment == 'sim_hint_ed_sensitivity':
    for w in workload_list():
        for epr_unit_count in [1, 4, 8, 16]:
            run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_u{epr_unit_count}', f'hint_a12',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=12,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                                    '-epr': epr_unit_count*EPR_BULK_COUNT,
                                    '--epr-ll-buffer-capacity': 1,
                                    '--substrate-mismatch-factor': 1000
                                 })
        run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_perfect', f'hint_a12',
                             inst_limit=SIM_INST_COUNT, 
                             active_set_capacity=12,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs={
                                '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                             })

elif experiment == 'sim_eif_mismatch_sensitivity':
    for w in workload_list():
        for smf in [10, 50, 100, 500]:
            run_quicksilver(w, PROJECT, f'eif_mismatch_sensitivity_smf{smf}', f'eif_a12',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=12,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1250*smf,
                                    '-epr': 2*EPR_BULK_COUNT,
                                    '--epr-ll-buffer-capacity': 1,
                                    '--substrate-mismatch-factor': smf
                                 })

elif experiment == 'sim_hint_mismatch_sensitivity':
    for w in workload_list():
        for smf in [10, 50, 100, 500]:
            run_quicksilver(w, PROJECT, f'hint_mismatch_sensitivity_smf{smf}', f'hint_a12',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=12,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=FACTORY_BUDGET,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1250*smf,
                                    '-epr': 2*EPR_BULK_COUNT,
                                    '--epr-ll-buffer-capacity': 1,
                                    '--substrate-mismatch-factor': smf
                                 })

elif experiment == 'sim_hint_factory_sensitivity':
    for w in workload_list():
        for f in [10000, 25000, 100_000]:
            run_quicksilver(w, PROJECT, f'hint_factory_sensitivity_f{f}', f'hint_a12',
                                 inst_limit=SIM_INST_COUNT, 
                                 active_set_capacity=12,
                                 total_program_inst=get_total_inst_count_for_workload(w),
                                 print_progress=PRINT_PROGRESS,
                                 factory_budget=f,
                                 kwargs={
                                    '--memory-is-remote': '',
                                    '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                                    '-epr': 2*EPR_BULK_COUNT,
                                    '--epr-ll-buffer-capacity': 1,
                                    '--substrate-mismatch-factor': 1000
                                 })

else:
    print(f'unknown experiment: {experiment}')
    exit(1)

##############################################
##############################################
