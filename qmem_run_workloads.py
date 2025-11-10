# author: Suhas Vittal

import os
from sys import argv

FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 50
MEM_BB_NUM_MODULES = 2

pol = argv[1]
exec_mode = argv[2] 

# determine compilation method:
if pol == 'viszlai' or pol == 'viszlai_near_memory':
    compiler_impl = 0
    compiled_trace_ext = 'viszlai'
    compiled_results_folder = f'out/qmem/compiled_results/viszlai'
elif pol == 'dpt' or pol == 'dpt_near_memory':
    compiler_impl = 1
    compiled_trace_ext = 'dpt'
    compiled_results_folder = f'out/qmem/compiled_results/dpt'

if pol == 'viszlai' or pol == 'dpt':
    mem_opts = '--mem-is-remote --mem-epr-buffer-capacity 4 --mem-epr-generation-frequency 1024 --mem-bb-round-ns 1800000'
else:
    mem_opts = '--mem-bb-round-ns 1800'

simulation_results_folder = f'out/qmem/simulation_results/{pol}'

base_workloads = ['BQ_e_cr2_120_d100_t1M_T5M.xz', 'BQ_shor_rsa256_iter_4.xz', 'BQ_v_c2h4o_ethylene_oxide_240_d100_t1M_T15M.xz', 'BQ_v_hc3h2cn_288_d100_t1M_T63M.xz']
compile_to = ['cr2_120', 'shor_RSA256', 'ethylene_240', 'hc3h2cn_288']
total_inst_count = [96848464, 10871852757, 86048253, 92929324]

sim_inst_count = 10_000_000

if exec_mode == 'compile':
    if pol == 'baseline':
        exit(0)

    os.system(f'mkdir -p {compiled_results_folder}')
    # COMPILATION
    for (i,w) in enumerate(base_workloads):    
        for cmp_count in [4, 8, 12, 16, 24]:
            base_name = f'{compile_to[i]}_c{cmp_count}'

            # run compile
            compiled_trace_path = f'benchmarks/bin/compiled/{base_name}_{compiled_trace_ext}.gz'
            output_stats_path = f'{compiled_results_folder}/{base_name}.out'
            cmd = f'./build/qs_mem_compile benchmarks/bin/{w} {compiled_trace_path} -c {cmp_count} -e {compiler_impl} -i {int(1.2*sim_inst_count)} -pp 1000000 &> {output_stats_path}'
            print(cmd)
else:
    os.system(f'mkdir -p {simulation_results_folder}')
    # SIMULATION
    for (i,w) in enumerate(base_workloads):
        inst_total = total_inst_count[i]
        if pol == 'baseline':
            cmd = f'./build/qs_sim_mem benchmarks/bin/{w} {sim_inst_count} {total_inst_count[i]} --cmp-sc-count 1000'\
                    + f' --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT}'\
                    + f' --mem-bb-num-modules {MEM_BB_NUM_MODULES} &> {simulation_results_folder}/{compile_to[i]}.out'
            print(cmd)
        else:
            for cmp_count in [4, 8, 12, 16, 24]:
                base_name = f'{compile_to[i]}_c{cmp_count}'
                compiled_trace_path = f'benchmarks/bin/compiled/{base_name}_{compiled_trace_ext}.gz'
                output_stats_path = f'{simulation_results_folder}/{base_name}.out'
                cmd = f'./build/qs_sim_mem {compiled_trace_path} {sim_inst_count} {inst_total}'\
                        + f' --cmp-sc-count {cmp_count}'\
                        + f' --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT}'\
                        + f' --mem-bb-num-modules {MEM_BB_NUM_MODULES}'\
                        + f' {mem_opts} &> {output_stats_path}'
                print(cmd)
