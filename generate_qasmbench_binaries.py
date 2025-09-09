# author: Suhas Vittal

import os

EXECUTABLE = 'qs_gen_binary'
QB_FOLDER = 'QASMBench/large'
B_BIN_FOLDER = 'benchmarks/bin'
B_STATS_FOLDER = 'benchmarks/stats'

# clean the output folders:
os.system(f'rm -rf {B_BIN_FOLDER}/QB_L_*')
os.system(f'rm -rf {B_STATS_FOLDER}/QB_L_*')

os.system(f'mkdir -p {B_BIN_FOLDER}')
os.system(f'mkdir -p {B_STATS_FOLDER}')

SKIP = [
    'adder',
    'bv',
    'cat',
    'cc',
    'ghz',
    'ising',
    'knn',
    'QAOA',
    'telecloning',
    'qft',
    'QV',
    'swap_test',
    'wstate'
]

b_count = 0
for f in os.listdir(QB_FOLDER):
    # skip certain benchmarks (badly made, compressed, uninteresting, etc.)
    if any(f'{s}_' in f for s in SKIP):
        continue

    file_path = f'{QB_FOLDER}/{f}/{f}.qasm'
    output_path = f'{B_BIN_FOLDER}/QB_L_{f}.gz'
    stats_path = f'{B_STATS_FOLDER}/QB_L_{f}.txt'

    print(f'{f}: Generating {output_path} and {stats_path}...')
    os.system(f'./build/{EXECUTABLE} {file_path} {output_path} {stats_path}')

    b_count += 1
print(f'Generated {b_count} binaries')