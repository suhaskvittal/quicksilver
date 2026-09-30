import os
from sys import argv

for a in [4,8,16,32]:
    INPUT_FOLDER = f'benchmarks/bin/compiled/rdr_hpca2026/eif_a{a}'
    OUTPUT_FOLDER = f'benchmarks/bin/compiled/rdr_hpca2026/eif_a{a}_rdr'
    os.system(f'mkdir -p {OUTPUT_FOLDER}')

    for w in os.listdir(INPUT_FOLDER):
        if 'qaoa' not in w:
            continue
        input_file = f'{INPUT_FOLDER}/{w}'
        output_file = f'{OUTPUT_FOLDER}/{w}'
        cmd = f'./build/qs_convert_to_rdr_isa {input_file} {output_file} -rdr 1'
        print(cmd)
        os.system(cmd)
