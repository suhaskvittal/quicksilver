import os

LZMA_THREADS = 16

def get_workload_name(f: str):
    basename = os.path.basename(f)
    return basename.split('.')[0]

def build_binaries(extra_options=''):
    files = [f for f in os.listdir('bisquit/qasm') if f.endswith('.qasm') or f.endswith('.qasm.xz')]
    for f in files:
        if '_t' not in f or 'boron' in f:
            continue
        filename = get_workload_name(f)
        output_file = f'benchmarks/bin/BQ_{filename}'
        stats_file = f'benchmarks/stats/BQ_{filename}.txt'

        cmd = f'./build/qs_gen_binary bisquit/qasm/{f} {output_file} -s {stats_file} -p 10000000 {extra_options} && xz -z -T {LZMA_THREADS} {output_file}'
        print(cmd)
        os.system(cmd)

def optimize_binaries(extra_options=''):
    os.system('mkdir -p benchmarks/bin/optimized')
    files = [f for f in os.listdir('benchmarks/bin') if f.endswith('.xz')]
    for f in files:
        if 'shor' not in f:
            continue
        filename = get_workload_name(f)
        output_file = f'benchmarks/bin/optimized/{filename}'
        cmd = f'./build/qs_optimizer benchmarks/bin/{f} {output_file} && xz -z -T {LZMA_THREADS} {output_file}'
        print(cmd)
        os.system(cmd)

build_binaries()
#optimize_binaries()
