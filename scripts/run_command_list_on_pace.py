import os
import time

rd = open('commands.out')

jobs = 0
lines = rd.readlines()
for line in lines:
    line_parts = line.split('&>')

    cmd = f'sbatch -N1 --ntasks-per-node=1 --mem-per-cpu=4G -t4:00:00 --account=gts-mqureshi4-rg -o{line_parts[1].strip()} --wrap=\"{line_parts[0].strip()}\"'

    print(line_parts[1])
    os.system(cmd)

    jobs += 1

print(f'jobs = {jobs}')
rd.close()
