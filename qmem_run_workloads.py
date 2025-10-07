# author: Suhas Vittal

import os


base_workloads = ['BQ_e_cr2_120_d100_t1M_T5M.xz', 'BQ_shor_rsa256_iter_4.xz', 'BQ_v_c2h4o_ethylene_oxide_240_d100_t1M_T15M.xz', 'BQ_v_hc3h2cn_288_d100_t1M_T63M.xz']
compile_to = ['cr2_120', 'shor_RSA256', 'ethylene_240', 'hc3h2cn_288']
total_inst_count = [96848464, 10871852757, 86048253, 92929324]

sim_inst_count = 10_000_000

CMP_COUNT = 4
FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT = 50
MEM_BB_NUM_MODULES = 2

os.system('mkdir out')
for (j,suffix) in enumerate(['viszlai', 'dpt', 'baseline']):
    os.system(f'mkdir -p out/compiled_results/{suffix}')
    os.system(f'mkdir -p out/simulation_results/{suffix}')
    for (i,w) in enumerate(base_workloads):
        # run compile
        base_name = f'{compile_to[i]}'
        if suffix != 'baseline':
            name = f'{compile_to[i]}_{suffix}'
            output_trace_path = f'benchmarks/bin/{name}.gz'
            output_stats_path = f'out/compiled_results/{suffix}/{base_name}.out'
            cmd1 = f'./build/qs_mem_compile benchmarks/bin/{w} {output_trace_path} -c {CMP_COUNT} -e {j} -i {1_000_000} &> {output_stats_path} && '
        else:
            cmd1 = ''

        # now do simulation:
        if suffix != 'baseline':
            input_trace_path = output_trace_path
            cmp_count = CMP_COUNT
        else:
            input_trace_path = f'benchmarks/bin/{w}'
            cmp_count = cmp_count
        inst_total = total_inst_count[i]
        output_stats_path = f'out/simulation_results/{suffix}/{base_name}.out'
        opts = f'--cmp-sc-count {cmp_count} --fact-phys-qubits-per-program-qubit {FACT_PHYS_QUBITS_PER_PROGRAM_QUBIT} --mem-bb-num-modules {MEM_BB_NUM_MODULES}'
        if suffix == 'dpt':
            opts = f'{opts} --mem-is-remote --mem-epr-buffer-capacity 2 --mem-epr-generation-frequency 1024 --mem-bb-round-ns 1800000'
        cmd2 = f'./build/qs_sim_mem {input_trace_path} {sim_inst_count} {inst_total} {opts} &> {output_stats_path}'
        print(cmd2)
