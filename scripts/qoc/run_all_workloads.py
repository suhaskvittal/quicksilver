# author: Suhas Vittal

import os
from sys import argv

SIM_INST_COUNT = 10_000_000
TOTAL_INST_COUNT = 1_000_000_000
PRINT_PROGRESS = SIM_INST_COUNT // 10

CMP_SC_COUNT = 12

FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 50

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

def trace_file_path(workload: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'benchmarks/bin/compiled/hint/{name_base}_c12.gz'

def stats_file_path(workload: int, policy: str, reduction: int) -> str:
    name_base = COMPILE_TO[workload]
    return f'out/qoc/simulation_results/{policy}/{name_base}_r{reduction}_{SIM_INST_COUNT//1_000_000}M.out'

##############################################
##############################################


def print_commands(policy: str, reduce_where: int, fact_phys_qubits_per_program_qubit: int, reduction: float, regime: int):
    os.system(f'mkdir -p out/qoc/simulation_results/{policy}')

    for (i,w) in enumerate(WORKLOADS):
        trace_path = trace_file_path(i)
        stats_output_path = stats_file_path(i, policy, int(reduction*100))

        error_rate_regime = '1e-8' if regime <= 1 else '1e-12' 
        cult_flag = '-cult' if (regime == 0 or regime == 2) else ''

        cmd = f'./build/qs_sim_mem {trace_path} {SIM_INST_COUNT} {TOTAL_INST_COUNT} -p {PRINT_PROGRESS}'\
                + f' --cmp-sc-count {CMP_SC_COUNT}'\
                + f' --fact-phys-qubits-per-program-qubit {fact_phys_qubits_per_program_qubit}'\
                + f' --mem-bb-num-modules {MEM_BB_NUM_MODULES}'\
                + f' --mem-bb-round-ns {MEM_BB_ROUND_NS}'
        if reduction > 0 and reduce_where > 0:
            cmd += f' -qoc {reduce_where} --qoc-reduction-fraction {reduction} -e {error_rate_regime} {cult_flag} &> {stats_output_path}'
        else:
            cmd += f' -e {error_rate_regime} {cult_flag} &> {stats_output_path}'
                
        print(cmd)

##############################################
##############################################

# baseline results:
print_commands('baseline_l2', 0, FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT, 0, 2)

# motivational results:
for red in [0.1, 0.25, 0.5]:
   print_commands('m_reduce_all', 7, FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT, red, 2)
   print_commands('m_reduce_cmp', 1, FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT, red, 2)
   print_commands('m_reduce_mem', 2, FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT, red, 2)
   print_commands('m_reduce_fact', 4, FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT, red, 2)

# main results:
for red in [0.2, 0.3, 0.4, 0.5]:
    print_commands('l2', 4, FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT, red, 2)

# sensitivity:
for f in [25, 75, 100, 200]:
    print_commands(f'baseline_fact{f}', 0, f, 0, 2)
    print_commands(f'l2_fact{f}', 4, f, 0.3, 2)

##############################################
##############################################
