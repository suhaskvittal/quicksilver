import os

##############################
##############################

d_cmp_count = 12
d_epr_freq = 0.034
d_epr_buf = 8
d_slowdown = 1000

os.system('rm commands.out ; touch commands.out')

##############################
##############################

exe = 'python3 scripts/dms/run_all_workloads.py'

# main results:
os.system(f'{exe} baseline {d_cmp_count} 1 viszlai {d_epr_buf} {d_epr_freq*1000} >> commands.out')
os.system(f'{exe} viszlai {d_cmp_count} {d_slowdown} viszlai {d_epr_buf} {d_epr_freq} >> commands.out')
os.system(f'{exe} hint {d_cmp_count} {d_slowdown} hint {d_epr_buf} {d_epr_freq} >> commands.out')
os.system(f'{exe} hint_c {d_cmp_count} {d_slowdown} hint {d_epr_buf} {d_epr_freq} -cs >> commands.out')

# memsys slowdown:
for s in [5,10,100]:
    fr = d_epr_freq*1000/s
    os.system(f'{exe} viszlai_sd{s} {d_cmp_count} {s} viszlai {d_epr_buf} {fr} >> commands.out')
    os.system(f'{exe} hint_sd{s} {d_cmp_count} {s} hint {d_epr_buf} {fr} >> commands.out')
    os.system(f'{exe} hint_c_sd{s} {d_cmp_count} {s} hint {d_epr_buf} {fr} -cs >> commands.out')

# epr buffer capacity
for cap in [4, 16, 32]:
    os.system(f'{exe} viszlai_epr{cap} {d_cmp_count} {d_slowdown} viszlai {cap} {d_epr_freq} >> commands.out')
    os.system(f'{exe} hint_epr{cap} {d_cmp_count} {d_slowdown} hint {cap} {d_epr_freq} >> commands.out')
    os.system(f'{exe} hint_c_epr{cap} {d_cmp_count} {d_slowdown} hint {cap} {d_epr_freq} -cs >> commands.out')

# epr generation frequency
for fr in [0.5*d_epr_freq, 2*d_epr_freq]:
    id = int(fr*1000)
    os.system(f'{exe} viszlai_fr{id} {d_cmp_count} {d_slowdown} viszlai {d_epr_buf} {fr} >> commands.out')
    os.system(f'{exe} hint_fr{id} {d_cmp_count} {d_slowdown} hint {d_epr_buf} {fr} >> commands.out')
    os.system(f'{exe} hint_c_fr{id} {d_cmp_count} {d_slowdown} hint {d_epr_buf} {fr} -cs >> commands.out')

# compute subsystem capacity:
for c in [8, 16]:
    os.system(f'{exe} baseline {c} 1 viszlai {d_epr_buf} {d_epr_freq*1000} >> commands.out')
    os.system(f'{exe} viszlai {c} {d_slowdown} viszlai {d_epr_buf} {d_epr_freq} >> commands.out')
    os.system(f'{exe} hint {c} {d_slowdown} hint {d_epr_buf} {d_epr_freq} >> commands.out')
    os.system(f'{exe} hint_c {c} {d_slowdown} hint {d_epr_buf} {d_epr_freq} -cs >> commands.out')
