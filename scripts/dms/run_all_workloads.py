# author: Suhas Vittal

import os
from sys import argv

SIM_INST_COUNT = 100_000_000
TOTAL_INST_COUNT = 1_000_000_000
PRINT_PROGRESS = SIM_INST_COUNT // 10

FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 100

MEM_BB_ROUND_NS = 1300
MEM_BB_NUM_MODULES = 2

##############################################
##############################################

def get_epr_generation_frequency(mem_round_ns: int):
    return 4 * MEM_BB_ROUND_NS / mem_round_ns

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

def trace_file_path(workload: int, compiler_policy: str, cmp_count: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'benchmarks/bin/compiled/{compiler_policy}/{name_base}_c{cmp_count}.gz'

def stats_file_path(workload: int, policy: str, cmp_count: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'out/dms/simulation_results/{policy}/{name_base}_c{cmp_count}_{SIM_INST_COUNT//1_000_000}M.out'

##############################################
##############################################

policy = argv[1]
cmp_count = int(argv[2])
mem_slowdown = int(argv[3])
compiler_policy = argv[4]

os.system(f'mkdir -p out/dms/simulation_results/{policy}')

for (i,w) in enumerate(WORKLOADS):
    trace_path = trace_file_path(i, compiler_policy, cmp_count)
    stats_output_path = stats_file_path(i, policy, cmp_count)

    mem_bb_round_ns = MEM_BB_ROUND_NS * mem_slowdown
    mem_epr_generation_frequency_khz = get_epr_generation_frequency(mem_bb_round_ns)

    cmd = f'./build/qs_sim_mem {trace_path} {SIM_INST_COUNT} {TOTAL_INST_COUNT} -p {PRINT_PROGRESS}'\
            + f' --cmp-sc-count {cmp_count}'\
            + f' --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT}'\
            + f' --mem-bb-num-modules {MEM_BB_NUM_MODULES}'\
            + f' --mem-bb-round-ns {mem_bb_round_ns}'\
            + f' --mem-is-remote'\
            + f' --mem-epr-generation-frequency {mem_epr_generation_frequency_khz} &> {stats_output_path}'
    print(cmd)

##############################################
##############################################
