from sys import argv
import os
import math

THREADS = 8
COUNT = 1000

if len(argv) > 1:
    THREADS = int(argv[1])

os.system('rm -rf rotation_data/*')

def run_command(filename: str, low: float, high: float):
    cmd = f'./build/qs_build_lut {low} {high} {COUNT} {filename} -t {THREADS} && xz -T {THREADS} {filename}'
    print(cmd)
    os.system(cmd)

for negative in [False, True]:
    offset = 10 if negative else 0
    for i in range(10):
        filename = f'rotation_data/{i+offset}.bin'

        if i == 0:
            low, high = 1.0, 2*math.pi
        else:
            low, high = 10**-i, 10**(-i+1)
        if negative:
            low, high = -low, -high

        run_command(filename, low, high)
