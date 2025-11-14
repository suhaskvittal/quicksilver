# author: Suhas Vittal

import os
from sys import argv

SIM_INST_COUNT = 100_000_000
TOTAL_INST_COUNT = 1_000_000_000
PRINT_PROGRESS = SIM_INST_COUNT // 10

CMP_SC_COUNT = 12

FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 100

MEM_BB_ROUND_NS = 1400
MEM_BB_NUM_MODULES = 4

##############################################
##############################################

def get_epr_generation_frequency(mem_round_ns: int):
    return 18 * MEM_BB_ROUND_NS / mem_round_ns # 2 memory cycles

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

def stats_file_path(workload: int, policy: str, reduction: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'out/qoc/simulation_results/{policy}/{name_base}_r{reduction}_{SIM_INST_COUNT//1_000_000}M.out'

##############################################
##############################################

policy = argv[1]
reduce_where = int(argv[2])
reduction = float(argv[3])
regime = int(argv[4])  # 0 = cultivation only, 1 = distillation only, 2 = 2-level cultivation+distillation

os.system(f'mkdir -p out/qoc/simulation_results/{policy}')

for (i,w) in enumerate(WORKLOADS):
    trace_path = f'benchmarks/bin/{w}'
    stats_output_path = stats_file_path(i, policy, int(reduction*100))

    error_rate_regime = '1e-8' if regime <= 1 else '1e-12' 
    cult_flag = '-cult' if (regime == 0 or regime == 2) else ''

    cmd = f'./build/qs_sim_mem {trace_path} {SIM_INST_COUNT} {TOTAL_INST_COUNT} -p {PRINT_PROGRESS}'\
            + f' --cmp-sc-count {CMP_SC_COUNT}'\
            + f' --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT}'\
            + f' --mem-bb-num-modules {MEM_BB_NUM_MODULES}'\
            + f' --mem-bb-round-ns {MEM_BB_ROUND_NS}'
    if reduction > 0 and reduce_where > 0:
        cmd += f' -qoc {reduce_where} --qoc-reduction-fraction {reduction} -e {error_rate_regime} {cult_flag} -jit &> {stats_output_path}'
    else:
        cmd += f' -e {error_rate_regime} {cult_flag} -jit &> {stats_output_path}'
            
    print(cmd)

##############################################
##############################################
