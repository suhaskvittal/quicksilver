import os
import subprocess
from sys import argv

TPEST = './build/qs_throughput_estimator'
OUTPUT_FILE = 'out/oclk/data.csv'

def call_and_write(wr, reduction, footprint, regime):
    cycle_time = int(400 + (1-reduction)*800)
    result = subprocess.run(
        [TPEST, regime, str(cycle_time), str(footprint)],
        capture_output=True, text=True)
    stats = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            stats[parts[0]] = parts[-1]
    wr.write(f'{regime},{reduction},{cycle_time},{stats['FOOTPRINT']},{stats['THROUGHPUT']}\n')



with open(OUTPUT_FILE, 'w') as wr:
    wr.write('Regime,Reduction,Cycle Time,Footprint,Throughput\n')

    for reduction in [0.0, 0.1, 0.2, 0.25, 0.3, 0.4, 0.5]:
        for footprint in range(1000, 20000+1, 1000):
            call_and_write(wr, reduction, footprint, 'G')
        for footprint in range(5000, 50000+1, 2500):
            call_and_write(wr, reduction, footprint, 'T')

