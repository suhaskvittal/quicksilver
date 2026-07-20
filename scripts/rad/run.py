import os

INST_SIM = 1_000_000
PP = 1_000_000
D = 23

CC_TR = 0.240*23 # collision clustering decoder latency at d = 23
HELIOS_TR = 0.0195*23 # helios latency at d = 23
AQ2_TR = 30*23
AQ2_ND = 1024

BENCHMARKS = [
    'BQ_t1B.gz',
    'BQ_bose_hubbard_q.xz',
    'BQ_bose_hubbard_t.xz',
    'BQ_hc3h2cn_q.xz',
    'BQ_hc3h2cn_t.xz',
    'BQ_chromium_q.xz',
    'BQ_chromium_t.xz',
    'BQ_qaoa_3regular.xz'
]

OUTPUT_DIR = 'out/rad_hpca2026'

os.system(f'mkdir -p {OUTPUT_DIR}/mwpm_baseline')
os.system(f'mkdir -p {OUTPUT_DIR}/ideal_baseline')

for b in BENCHMARKS:
    bpath = f'benchmarks/bin/{b}'
    bname = os.path.splitext(os.path.basename(bpath))[0]

    ## Experiment 1 -- best MWPM asic
    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {CC_TR} -nd 1024 -pp {PP} &> {OUTPUT_DIR}/mwpm_baseline/{bname}.out'
    print(cmd)

    ## Experiment 2 -- ideal baseline (helios)
    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 1024 -pp {PP} &> {OUTPUT_DIR}/ideal_baseline/{bname}.out'
    print(cmd)

    ## Experiment 3 -- rad (helios + aq2)
    for p in [1e-4, 1e-5, 1e-6]:
        os.system(f'mkdir -p {OUTPUT_DIR}/rad_p{p}')
        cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 1 -pp {PP} -rad --rad-decoder-count {AQ2_ND} --rad-reaction-time {AQ2_TR} --rad-error-rate {p} &> {OUTPUT_DIR}/rad_p{p}/{bname}.out'
        print(cmd)

