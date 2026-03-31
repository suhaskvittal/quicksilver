from scripts.common import *
from sys import argv

os.system('mkdir -p benchmarks/bin/rdr')

for w in workload_list():
    name = get_workload_name(w)
    output_file = f'benchmarks/bin/rdr/{name}'
    cmd = f'./build/qs_convert_to_rdr_isa {w} {output_file} -rdr 1 && xz -z -T 8 {output_file}'
    print(cmd)
    os.system(cmd)
