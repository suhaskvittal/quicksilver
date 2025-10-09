# author: Suhas Vittal

import os

base_workloads = ['BQ_e_cr2_120_d100_t1M_T5M.xz', 'BQ_shor_rsa256_iter_4.xz', 'BQ_v_c2h4o_ethylene_oxide_240_d100_t1M_T15M.xz', 'BQ_v_hc3h2cn_288_d100_t1M_T63M.xz']
compile_to = ['cr2_120', 'shor_RSA256', 'ethylene_240', 'hc3h2cn_288']
total_inst_count = [96848464, 10871852757, 86048253, 92929324]

sim_inst_count = 10_000_000

CMP_COUNT = 12
FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 100
MEM_BB_NUM_MODULES = 8

os.system('mkdir -p out/q_hyperthreading')
for (j, which) in enumerate(['baseline', 'compute', 'memory', 'factory_all_levels', 'factory_only_l1']):
    output_folder = f'out/q_hyperthreading/{which}'
    os.system(f'mkdir -p {output_folder}')
    for (i,w) in enumerate(base_workloads):
        if which == 'baseline':
            output_path = f'{output_folder}/{compile_to[i]}.out'
            total_inst = total_inst_count[i]
            cmd = f'./build/qs_sim_mem benchmarks/bin/{w} {sim_inst_count} {total_inst} -jit --cmp-sc-count {CMP_COUNT} --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT} --mem-bb-num-modules {MEM_BB_NUM_MODULES} &> {output_path}'
            print(cmd)
        else:
            for rf in [0.05, 0.1, 0.2]:
                output_path = f'{output_folder}/{compile_to[i]}_{int(rf*100)}.out'
                total_inst = total_inst_count[i]
                cmd = f'./build/qs_sim_mem benchmarks/bin/{w} {sim_inst_count} {total_inst} -jit --cmp-sc-count {CMP_COUNT} --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT} --mem-bb-num-modules {MEM_BB_NUM_MODULES} -qht {j} --qht-reduction-fraction {rf} &> {output_path}'
                print(cmd)



