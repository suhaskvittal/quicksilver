# author: Suhas Vittal

import os
from sys import argv

SIM_INST_COUNT = 10_000_000
PRINT_PROGRESS = SIM_INST_COUNT // 10
LOCAL_MEMORY_CAPACITY = 12

##############################################
##############################################

WORKLOADS = [
    'BQ_e_cr2_120_trotter.rpc.xz',
    'BQ_shor_rsa256_iter_4.rpc.xz',
    'BQ_v_c2h4o_ethylene_oxide_240_trotter.rpc.xz',
    'BQ_v_hc3h2cn_288_trotter.rpc.xz',
    'BQ_grover_3sat_schoning_1710.rpc.xz'
]

COMPILE_TO = [
    'cr2_120_t',
    'shor_rsa256',
    'c2h4o_ethylene_oxide_240_t',
    'hc3h2cn_288_t',
    'grover_3sat_1710'
]

##############################################
##############################################

def get_compiled_file_path(workload_idx: int) -> str:
    name_base = COMPILE_TO[workload_idx]
    return f'benchmarks/bin/compiled/{name_base}_eif_a12.rpc.gz'

def print_compile_commands():
    for (i,w) in enumerate(WORKLOADS):
        compiled_file_path = get_compiled_file_path(i)

        cmd = f'./build/qs_memory_scheduler benchmarks/bin/{w} {compiled_file_path} -c {LOCAL_MEMORY_CAPACITY}'\
                + f' -i {int(SIM_INST_COUNT*5)} -pp {PRINT_PROGRESS} --dag-capacity {128*1024} -s 0'
        print(cmd)

##############################################
##############################################

D_FACTORY_PHYS_QUBIT_COUNT = 50000
D_FACTORY_L2_BUFFER_CAPACITY = 4
D_T_TELEPORT_LIMIT = 0
D_RPC_CAPACITY = 2

def get_stats_file_path(workload_idx: int, policy: str, postfix=None) -> str:
    os.system(f'mkdir -p out/rpc/simulation_results/{policy}')

    name_base = COMPILE_TO[workload_idx]
    stats_file = f'out/rpc/simulation_results/{policy}/{name_base}'
    if postfix is not None:
        stats_file = f'{stats_file}_{postfix}'
    stats_file = f'{stats_file}_{SIM_INST_COUNT//1_000_000}M.out'
    return stats_file

def print_simulation_commands(policy: str, 
                              factory_phys_qubit_count=D_FACTORY_PHYS_QUBIT_COUNT, 
                              factory_l2_buffer_capacity=D_FACTORY_L2_BUFFER_CAPACITY, 
                              t_teleport_limit=D_T_TELEPORT_LIMIT, 
                              rpc_capacity=D_RPC_CAPACITY
):
    options = f'-f {factory_phys_qubit_count} --factory-l2-buffer-capacity {factory_l2_buffer_capacity}'\
                + f' -ttpl {t_teleport_limit}'
    postfix = f'f{factory_phys_qubit_count}_b{factory_l2_buffer_capacity}_ttpl{t_teleport_limit}'

    if policy == 'rpc':
        options += f' -rpc --rpc-capacity {rpc_capacity}'
        postfix = f'{postfix}_r{rpc_capacity}'
    elif policy == 'sol':
        options += ' --bsol-elide-cliffords --bsol-sync'
    elif policy == 'sol_sync_only':
        options += ' --bsol-sync'
    elif policy == 'sol_elide_only':
        options += ' --bsol-elide-cliffords'


    for (i,w) in enumerate(WORKLOADS):
        trace_path = get_compiled_file_path(i)
        stats_path = get_stats_file_path(i, policy, postfix)

        cmd = f'./build/quicksilver {trace_path} {SIM_INST_COUNT} -pp {PRINT_PROGRESS} -a {LOCAL_MEMORY_CAPACITY} {options} > {stats_path}'
        print(cmd)

##############################################
##############################################

from sys import argv

mode = argv[1]

if mode == 'compile':
    print_compile_commands()
else:

    ## baseline:

    ## bandwidth SoL study:
    print_simulation_commands('sol')
    print_simulation_commands('sol_sync_only')
    print_simulation_commands('sol_elide_only')

    ## evals:
    for f in [50000, 75000, 100000]:
        if f == 50000:
            ttpl_array = [0,1,2]
        elif f == 75000:
            ttpl_array = [0,1,2,3,4]
        else:
            ttpl_array = [0,1,2,3,4,5,6]
        for b in [4, 8, 12, 16]:
            for ttpl in ttpl_array:
                print_simulation_commands('baseline', factory_phys_qubit_count=f, t_teleport_limit=ttpl)
                print_simulation_commands('rpc', factory_phys_qubit_count=f, t_teleport_limit=ttpl)

##############################################
##############################################
