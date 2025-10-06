import os

files = [f for f in os.listdir('bisquit/qasm') if f.endswith('.qasm') or f.endswith('.qasm.xz')]

for f in files:
    if f == 'v_hc3h2cn_288_td_10.qasm.xz':  # too large for now
        continue;

    f_part = f.split('.')
    filename = f_part[0]
    output_file = f'benchmarks/bin/BQ_{filename}'
    stats_file = f'benchmarks/stats/BQ_{filename}.txt'
    cmd = f'./build/qs_gen_binary bisquit/qasm/{f} {output_file} {stats_file} && xz -z -T 8 {output_file}'
    print(cmd)
    os.system(cmd)