import os

##############################
##############################

os.system('rm commands.out ; touch commands.out')

##############################
##############################

exe = 'python3 scripts/qoc/run_all_workloads.py'

# baseline:
os.system(f'{exe} baseline_l1cult 0 0 0 >> commands.out')
os.system(f'{exe} baseline_l1dist 0 0 1 >> commands.out')
os.system(f'{exe} baseline_l2 0 0 2 >> commands.out')

# motivational results:
for red in [0.1, 0.2, 0.5]:
    os.system(f'{exe} m_reduce_all 7 {red} 2 >> commands.out')
    os.system(f'{exe} m_reduce_cmp 1 {red} 2 >> commands.out')
    os.system(f'{exe} m_reduce_mem 2 {red} 2 >> commands.out')
    os.system(f'{exe} m_reduce_fact 4 {red} 2 >> commands.out')

# results
for red in [0.2, 0.3, 0.4, 0.5]:
    os.system(f'{exe} l1cult 4 {red} 0 >> commands.out')
    os.system(f'{exe} l1dist 4 {red} 1 >> commands.out')
    os.system(f'{exe} l2 4 {red} 2 >> commands.out')
