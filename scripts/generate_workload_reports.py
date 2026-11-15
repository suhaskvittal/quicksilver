import os

os.system('mkdir -p benchmarks/workload_reports')
for f in os.listdir('benchmarks/bin/optimized'):
    if not f.endswith('.xz'):
        continue
    workload_name = f.split('.')[0]
    cmd = f'./build/qs_report benchmarks/bin/optimized/{f} -pp 100000000000 > benchmarks/workload_reports/{workload_name}.out'
    print(cmd)
    os.system(cmd)
