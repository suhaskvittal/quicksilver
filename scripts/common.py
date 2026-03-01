# author: Suhas Vittal

import os
from sys import argv

WORKLOADS = [
    'benchmarks/bin/BQ_e_cr2_120_trotter.rpc.xz',
    'benchmarks/bin/BQ_shor_rsa256_iter_4.rpc.xz',
    'benchmarks/bin/BQ_v_c2h4o_ethylene_oxide_240_trotter.rpc.xz',
    'benchmarks/bin/BQ_v_hc3h2cn_288_trotter.rpc.xz',
    'benchmarks/bin/BQ_grover_3sat_schoning_1710.rpc.xz'
]

##############################################
##############################################

def workload_list() -> list[str]:
    return WORKLOADS

def memory_scheduler_exe() -> str:
    return './build/qs_memory_scheduler'

def quicksilver_exe() -> str:
    return './build/quicksilver'

##############################################
##############################################

def get_workload_name(filepath: str):
    basename = os.path.basename(filepath)
    return os.path.splitext(basename)[0]

def get_compiler_output_folder_path(project: str, policy: str) -> str:
    folder_path = f'benchmarks/bin/compiled/{project}/{policy}'
    os.system(f'mkdir -p {folder_path}')
    return folder_path

def get_compiler_stats_folder_path(project: str, policy: str) -> str:
    folder_path = f'out/{project}/compiler_results/{policy}'
    os.system(f'mkdir -p {folder_path}')
    return folder_path

def get_simulation_stats_folder_path(project: str, policy: str) -> str:
    folder_path = f'out/{project}/simulation_results/{policy}'
    os.system(f'mkdir -p {folder_path}')
    return folder_path

def get_total_inst_count_for_workload(filepath: str):
    w = get_workload_name(filepath)
    stats_path = f'benchmarks/stats/{w}.txt'
    with open(stats_path) as f:
        for line in f:
            key, _, value = line.partition(' ')
            if key == 'UNROLLED_INSTRUCTION_COUNT':
                return int(value.strip())

##############################################
##############################################

def join_command_line_args(cmd: str, kwargs) -> str:
    arg_string = ' '.join(f'{k} {v}' for (k,v) in kwargs.items())
    return f'{cmd} {arg_string}'

##############################################
##############################################

'''
    Both run functions below return a string for the command
    to be executed. Up-to the caller as to how this string
    should be consumed.

    The string is automatically printed by this function.
'''

def run_memory_scheduler(workload_file_path: str, 
                         project: str, 
                         policy: str, 
                         active_set_capacity=12,
                         inst_limit=150_000_000,
                         print_progress=10_000_000,
                         dag_capacity=64*1024,
                         scheduler_id=0,  # 0 = EIF, 1 = HINT
                         kwargs=None
) -> str:
    w = get_workload_name(workload_file_path)

    output_folder_path = get_compiler_output_folder_path(project, policy)
    stats_folder_path = get_compiler_stats_folder_path(project, policy)

    output_path = f'{output_folder_path}/{w}.gz'
    stats_path = f'{stats_folder_path}/{w}.out'

    cmd = f'{memory_scheduler_exe()} {workload_file_path} {output_path}'\
          + f' -c {active_set_capacity}'\
          + f' -i {inst_limit}'\
          + f' -pp {print_progress}'\
          + f' --dag-capacity {dag_capacity}'\
          + f' -s {scheduler_id}'
    if kwargs is not None:
        cmd = join_command_line_args(cmd, kwargs)
    cmd = f'{cmd} &> {stats_path}'
    print(cmd)
    return cmd


def run_quicksilver(workload_file_path: str,
                    project: str,
                    policy: str,
                    compiler_policy: str,
                    inst_limit=100_000_000,
                    active_set_capacity=12,
                    total_program_inst=1_000_000_000,
                    print_progress=10_000_000,
                    factory_budget=50_000,
                    use_compiled_binary=True,
                    kwargs=None
) -> str:
    w = get_workload_name(workload_file_path)

    if use_compiled_binary:
        compiled_binary_folder_path = get_compiler_output_folder_path(project, compiler_policy)
        compiled_binary_path = f'{compiled_binary_folder_path}/{w}.gz'
    else:
        compiled_binary_path = workload_file_path
    stats_folder_path = get_simulation_stats_folder_path(project, policy)
    stats_path = f'{stats_folder_path}/{w}.out'

    cmd = f'{quicksilver_exe()} {compiled_binary_path} {inst_limit}'\
            + f' -pp {print_progress}'\
            + f' --total-program-instructions {total_program_inst}'\
            + f' -a {active_set_capacity}'\
            + f' -f {factory_budget}'
    if kwargs is not None:
        cmd = join_command_line_args(cmd, kwargs)
    cmd = f'{cmd} &> {stats_path}'
    print(cmd)
    return cmd

##############################################
##############################################
