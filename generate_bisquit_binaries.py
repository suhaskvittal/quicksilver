import os

files = [f for f in os.listdir('bisquit/qasm') if f.endswith('.qasm') or f.endswith('.qasm.xz')]

for f in files:
    f_part = f.split('.')
    filename = f_part[0]
    output_file = f'benchmarks/bin/BQ_{filename}'
    stats_file = f'benchmarks/stats/BQ_{filename}.txt'
    cmd = f'./build/qs_gen_binary bisquit/qasm/{f} {output_file} -s {stats_file} -t 16 -p 1000000 && xz -z -T 8 {output_file}'
    print(cmd)
    os.system(cmd)