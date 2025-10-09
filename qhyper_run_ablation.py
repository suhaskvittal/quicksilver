# author: Suhas Vittal

import os

base_workloads = ['BQ_e_cr2_120_d100_t1M_T5M.xz', 'BQ_shor_rsa256_iter_4.xz', 'BQ_v_c2h4o_ethylene_oxide_240_d100_t1M_T15M.xz', 'BQ_v_hc3h2cn_288_d100_t1M_T63M.xz']
compile_to = ['cr2_120', 'shor_RSA256', 'ethylene_240', 'hc3h2cn_288']
total_inst_count = [96848464, 10871852757, 86048253, 92929324]

sim_inst_count = 5_000_000

CMP_COUNT = 12
FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 50
MEM_BB_NUM_MODULES = 8

qht_flag_map = {
    'compute': 1,
    'memory': 2,
    'factory': 4,
    'all_sc': 5,
    'all': 7
}

os.system('mkdir -p out/q_hyperthreading')
for (j, which) in enumerate(['baseline', 'compute', 'memory', 'factory', 'all_sc', 'all']):
    output_folder = f'out/q_hyperthreading/{which}'
    os.system(f'mkdir -p {output_folder}')
    for (i,w) in enumerate(base_workloads):
        compiled_file = f'benchmarks/bin/compiled/{compile_to[i]}_c{CMP_COUNT}.gz'
        if which == 'baseline':
            output_path = f'{output_folder}/{compile_to[i]}.out'
            total_inst = total_inst_count[i]

            compile_cmd = f'./build/qs_mem_compile benchmarks/bin/{w} {compiled_file} -c {CMP_COUNT} -e 0 -i {2*sim_inst_count} -pp 1000000 &> out/q_hyperthreading/{compile_to[i]}_compile.out'
            sim_cmd = f'./build/qs_sim_mem {compiled_file} {sim_inst_count} {total_inst} --cmp-sc-count {CMP_COUNT}' \
                        + f' --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT} --mem-bb-num-modules {MEM_BB_NUM_MODULES} &> {output_path}'

            print(compile_cmd)
            print(sim_cmd)
        else:
            flag = qht_flag_map[which]
            for rf in [0.05, 0.1, 0.2, 0.5]:
                output_path = f'{output_folder}/{compile_to[i]}_{int(rf*100)}.out'
                total_inst = total_inst_count[i]

                sim_cmd = f'./build/qs_sim_mem {compiled_file} {sim_inst_count} {total_inst} --cmp-sc-count {CMP_COUNT}' \
                            + f' --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT} --mem-bb-num-modules {MEM_BB_NUM_MODULES}' \
                            + f' -qht {flag} --qht-reduction-fraction {rf} &> {output_path}'
                print(sim_cmd)



