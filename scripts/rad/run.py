import os

INST_SIM = 10_000_000
PP = 1_000_000
D = 29
RFIFO_CAP = 4096

UF_TR_MEAN, UF_TR_STD = 44, 0
MWPM_TR_MEAN, MWPM_TR_STD = 939.4, 0.055
#CMWPM_TR_MEAN, CMWPM_TR_STD = 13910.9, 0.043
CMWPM_TR_MEAN, CMWPM_TR_STD = 7313.4, 0.050

UF_LER = 1.1e-13

def uf_opts():
    return f'--reaction-time {UF_TR_MEAN} --reaction-time-std {UF_TR_STD}'

def mwpm_opts():
    return f'--reaction-time {MWPM_TR_MEAN} --reaction-time-std {MWPM_TR_STD}'

def cmwpm_opts():
    return f' --reaction-time {CMWPM_TR_MEAN} --reaction-time-std {CMWPM_TR_STD}'

def ds_opts(l2_tr_mean=CMWPM_TR_MEAN, l2_tr_std=CMWPM_TR_STD, sw_rate=0.000951, sw_cycles=19280):
    return f'--reaction-time {UF_TR_MEAN} --reaction-time-std {UF_TR_STD}'\
            f' --l2-reaction-time-mean {l2_tr_mean} --l2-reaction-time-std {l2_tr_std}'\
            f' --decoder-mode switching --switching-rate {sw_rate} --switching-overhead {sw_cycles}'

def rad_opts(l2_tr_mean=CMWPM_TR_MEAN, l2_tr_std=CMWPM_TR_STD, l1_error_rate=UF_LER, rfifo_cap=RFIFO_CAP):
    return f'--reaction-time {UF_TR_MEAN} --reaction-time-std {UF_TR_STD}'\
            f' --l2-reaction-time-mean {l2_tr_mean} --l2-reaction-time-std {l2_tr_std}'\
            f' --decoder-mode rad --rad-l1-error-rate {l1_error_rate} --rad-rfifo-capacity {rfifo_cap}'

BENCHMARKS = [
    'BQ_bose_hubbard_q_prep.xz',
    'BQ_bose_hubbard_q_sel.xz',
    'BQ_bose_hubbard_t.xz',
    'BQ_chromium_q_prep.xz',
    'BQ_chromium_q_sel.xz',
    'BQ_chromium_t.xz',
    'BQ_gidney25_rsa2048_256_of_20805.xz',
    'BQ_hc3h2cn_q_prep.xz',
    'BQ_hc3h2cn_q_sel.xz',
    'BQ_hc3h2cn_t.xz',
    'BQ_qaoa_random.xz',
    'BQ_qaoa_powerlaw.xz'
]

BENCHMARK_SIZES = [
    # bose hubbard
    2228,
    2228,
    2188,
    # chromium
    167,
    167,
    121,
    # gidney
    1575,
    # hc3h2cn
    341,
    341,
    289,
    # qaoa
    2048,
    2048
]

OUTPUT_DIR = 'out/rad_isca2026'

os.system(f'mkdir -p {OUTPUT_DIR}/ideal_baseline')
os.system(f'mkdir -p {OUTPUT_DIR}/mwpm_only')
os.system(f'mkdir -p {OUTPUT_DIR}/cmwpm_only')
os.system(f'mkdir -p {OUTPUT_DIR}/decoder_switching')
os.system(f'mkdir -p {OUTPUT_DIR}/rad')

SW_CYCLES_SENS = [0, 1, 10, 100, 1000]
def sw_cycles_sens_folder(c):
    return f'{OUTPUT_DIR}/decoder_switching_sw_cycles_sens_{c}'
for c in SW_CYCLES_SENS:
    f = sw_cycles_sens_folder(c)
    os.system(f'mkdir -p {f}')

L1_LER_SENS = [1e-7, 1e-8, 1e-9, 1e-10, 1e-11, 1e-12, UF_LER]
L2_LAT_SENS = [0.01, 0.1, 0.5, 1.0, 2.0]
#DS_SW_RATE_LER_SENS = [0.383, 0.132, 0.0393, 0.01, 0.0036, 0.0024]
def rad_ler_lat_sens_folder(e, t):
    return f'{OUTPUT_DIR}/rad_l1_error_rate_{e}_l2_latency_{t}'
for e in L1_LER_SENS:
    for t in L2_LAT_SENS:
        f = rad_ler_lat_sens_folder(e,t)
        os.system(f'mkdir -p {f}')


RFIFO_SENS = [512, 1024, 2048, 8192, 16*1024, 32*1024]
def rad_rfifo_sens_folder(x):
    return f'{OUTPUT_DIR}/rad_rfifo_{x}'
for r in RFIFO_SENS:
    f = rad_rfifo_sens_folder(r)
    os.system(f'mkdir -p {f}')

for (i,b) in enumerate(BENCHMARKS):
    Q = BENCHMARK_SIZES[i]
    bpath = f'benchmarks/bin/optimized/{b}'
    bname = os.path.splitext(os.path.basename(bpath))[0]

    COMMON = f'./build/quicksilver {bpath} {INST_SIM} -pp {PP} -f 100000 --compute-code-distance {D} -skip 1 -a {Q}'

    ## Experiment 1 -- ideal baseline (helios)
    cmd = f'{COMMON} {uf_opts()} &> {OUTPUT_DIR}/ideal_baseline/{bname}.out'
    print(cmd)

    ## Experiment 2 -- mwpm only and cmwpm only
    cmd = f'{COMMON} {mwpm_opts()} &> {OUTPUT_DIR}/mwpm_only/{bname}.out'
    print(cmd)
#   cmd = f'{COMMON} {cmwpm_opts()} &> {OUTPUT_DIR}/cmwpm_only/{bname}.out'
#   print(cmd)

    ## Experiment 3 -- decoder switching
    cmd = f'{COMMON} {ds_opts()} &> {OUTPUT_DIR}/decoder_switching/{bname}.out'
    print(cmd)

    ## Experiment 4 -- rad
    cmd = f'{COMMON} {rad_opts()} &> {OUTPUT_DIR}/rad/{bname}.out'
    print(cmd)

    ## Experiment 5 -- decoder switching, switching overhead sensitivity
    for c in SW_CYCLES_SENS:
        cmd = f'{COMMON} {ds_opts(sw_cycles=c)} &> {sw_cycles_sens_folder(c)}/{bname}.out'
        print(cmd)

    ## Experiment 6 -- sensitivity to L1 error rate + L2 latency
    for e in L1_LER_SENS:
        for t in L2_LAT_SENS:
            cmd = f'{COMMON} {rad_opts(l1_error_rate=e, l2_tr_mean=t*CMWPM_TR_MEAN)} &> {rad_ler_lat_sens_folder(e,t)}/{bname}.out'
            print(cmd)
    
    ## Experiment 7 -- rfifo sensitivity
#   for r in RFIFO_SENS:
#       cmd = f'{COMMON} {rad_opts(rfifo_cap=r)} &> {rad_rfifo_sens_folder(r)}/{bname}.out'
#       print(cmd)
