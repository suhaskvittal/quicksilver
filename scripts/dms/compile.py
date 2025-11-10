# author: Suhas Vittal

import os
from sys import argv

SIM_INST_COUNT = 100_000_000
PRINT_PROGRESS = SIM_INST_COUNT // 10

# need to compile a little extra than simulated
COMPILE_INST_COUNT = int(SIM_INST_COUNT * 1.2)

##############################################
##############################################

WORKLOADS = [
    'BQ_e_cr2_120_trotter.xz',
    'BQ_shor_rsa256_iter_4.xz',
    'BQ_v_c2h4o_ethylene_oxide_240_trotter.xz',
    'BQ_v_hc3h2cn_288_trotter.xz',
    'BQ_grover_3sat_schoning_1710.xz'
]

COMPILE_TO = [
    'cr2_120',
    'shor_rsa256',
    'c2h4o_ethylene_oxide_240',
    'hc3h2cn_288',
    'grover_3sat_1710'
]

##############################################
##############################################

def compiled_file_path(workload: str, policy: str, cmp_count: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'benchmarks/bin/compiled/{policy}/{name_base}_c{cmp_count}.gz'

def stats_file_path(workload: str, policy: str, cmp_count: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'out/dms/compiled_results/{policy}/{name_base}_c{cmp_count}_{SIM_INST_COUNT//1_000_000}M.out'

##############################################
##############################################

pol = argv[1]
cmp_count = argv[2]

# determine memory access scheduler ID
if pol == 'viszlai':
    mas_id = 0
else:
    mas_id = 1

os.system(f'mkdir -p out/dms/compiled_results/{pol}')
os.system(f'mkdir -p benchmarks/bin/compiled/{pol}')

for (i,w) in enumerate(WORKLOADS):
    compiled_output_path = compiled_file_path(i, pol, cmp_count)
    stats_output_path = stats_file_path(i, pol, cmp_count)

    cmd = f'./build/qs_mem_compile benchmarks/bin/{w} {compiled_output_path}'\
            + f' -i {COMPILE_INST_COUNT} -c {cmp_count} -e {mas_id} -pp {PRINT_PROGRESS} &> {stats_output_path}'
    print(cmd)
