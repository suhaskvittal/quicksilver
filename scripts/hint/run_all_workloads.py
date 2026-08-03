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
EPR_BULK_COUNT = 2*3472
FACTORY_BUDGET = 50000

'''
    Compiler settings:
'''
HINT_LOOKAHEAD_DEPTH = 256
COMPILE_INST_COUNT = int(2*SIM_INST_COUNT)

OPT_INST_COUNT = 100

##############################################
##############################################

experiment = argv[1]

''' Compilation for EIF and HINT '''
if experiment == 'compile_eif':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            run_memory_scheduler(w, PROJECT, f'eif_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0)

elif experiment == 'compile_eif_la':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            for ld in [8, 16, 32, 64, 128, 256, 512]:
                run_memory_scheduler(w, PROJECT, f'eif_la_ld{ld}_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=0,
                                    kwargs={
                                        '--eif-lookahead-depth': ld
                                    })

elif experiment == 'compile_hint':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            for ld in [8, 16, 32, 64, 128, 256, 512]:
                run_memory_scheduler(w, PROJECT, f'hint_ld{ld}_a{a}', active_set_capacity=a, inst_limit=COMPILE_INST_COUNT, scheduler_id=1, 
                                    kwargs={
                                        '--hint-lookahead-depth': ld,
                                        '--hint-use-coalescing': '',
                                        '--hint-use-nonarbitrary-victim-selection': ''
                                    })

elif experiment == 'compile_test_opt':
    for w in workload_list():
        run_memory_optimality(w, PROJECT, f'opt', active_set_capacity=4, inst_limit=OPT_INST_COUNT)

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

elif experiment == 'sim_eif_la':
    for w in workload_list():
        for a in [8, 12, 16, 24, 32]:
            run_quicksilver(w, PROJECT, f'eif_la_a{a}', f'eif_la_ld{HINT_LOOKAHEAD_DEPTH}_a{a}',
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
            run_quicksilver(w, PROJECT, f'hint_a{a}', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a{a}',
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

elif experiment == 'sim_eif_ed_sensitivity':
    for w in workload_list():
        for epr_unit_count in [4, 8, 16]:
            run_quicksilver(w, PROJECT, f'eif_ed_sensitivity_u{epr_unit_count}', f'eif_a12',
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
        run_quicksilver(w, PROJECT, f'eif_ed_sensitivity_perfect', f'eif_a12',
                             inst_limit=SIM_INST_COUNT, 
                             active_set_capacity=12,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs={
                                '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                             })

elif experiment == 'sim_eif_la_ed_sensitivity':
    for w in workload_list():
        for epr_unit_count in [4, 8, 16]:
            run_quicksilver(w, PROJECT, f'eif_la_ed_sensitivity_u{epr_unit_count}', f'eif_la_ld{HINT_LOOKAHEAD_DEPTH}_a12',
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
        run_quicksilver(w, PROJECT, f'eif_la_ed_sensitivity_perfect', f'eif_la_ld{HINT_LOOKAHEAD_DEPTH}_a12',
                             inst_limit=SIM_INST_COUNT, 
                             active_set_capacity=12,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs={
                                '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                             })

elif experiment == 'sim_hint_ed_sensitivity':
    for w in workload_list():
        for epr_unit_count in [4, 8, 16]:
            run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_u{epr_unit_count}', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a12',
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
        run_quicksilver(w, PROJECT, f'hint_ed_sensitivity_perfect', f'hint_ld{HINT_LOOKAHEAD_DEPTH}_a12',
                             inst_limit=SIM_INST_COUNT, 
                             active_set_capacity=12,
                             total_program_inst=get_total_inst_count_for_workload(w),
                             print_progress=PRINT_PROGRESS,
                             factory_budget=FACTORY_BUDGET,
                             kwargs={
                                '--memory-syndrome-extraction-round-time-ns': 1_250_000,
                             })

else:
    print(f'unknown experiment: {experiment}')
    exit(1)

##############################################
##############################################
