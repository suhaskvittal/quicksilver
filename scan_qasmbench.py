# author: Suhas Vittal
# scans QASMBench and checks each program can be compiled:

import os

executable = 'build/oq2bf'

#for subdir in ['small', 'medium', 'large']:
for subdir in ['large']:
    benchmarks = os.listdir(f'QASMBench/{subdir}')
    for b in benchmarks:
        folder = f'QASMBench/{subdir}/{b}'
        if os.path.isdir(folder):
            print(f'RUNNING {subdir}/{b}')
            file_path = f'{folder}/{b}.qasm'
            retcode = os.system(f'./{executable} {file_path}')
            if retcode != 0:
                exit(1)