# author: Suhas Vittal

import os

workloads = ['QB_L_bwt_n177', 'BQ_shor_rsa16']

os.system('mkdir out')
for w in workloads:
    trace_path = f'benchmarks/bin/{w}.gz'
    
    for cmp_sc_fraction in [0.1, 0.2, 0.5, 1.0]:
        for fact_prog_fraction in [0.3, 0.6, 1.0, 1.2]:
            output_path = f'out/{w}_c{100*cmp_sc_fraction}_f{100*fact_prog_fraction}.out'
            os.system(f'./build/qs_sim_mem_search {trace_path} --cmp_surface_code_fraction {cmp_sc_fraction} --fact_prog_fraction {fact_prog_fraction} > {output_path}')
            