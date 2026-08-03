import os

INST_SIM = 1_000_000
PP = 100_000
D = 23

HELIOS_TR = 0.0195*23 # helios latency at d = 23
P_HELIOS = 1e-7

MWPM_TR = 50
P_MWPM = 1e-8
MWPM_ND = 460*4

AQ2_TR = 30*23
AQ2_ND = 4*1380

DSWITCH_TR = 0.02*AQ2_TR + 0.98*HELIOS_TR
DSWITCH_ND = 4*29

RFIFO_CAP = 4096

BENCHMARKS = [
#   'BQ_bose_hubbard_q_prep.xz',
#   'BQ_bose_hubbard_q_sel.xz',
#   'BQ_bose_hubbard_t.xz',
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

OUTPUT_DIR = 'out/rad_hpca2026'
LER_SENS_LIST = [1e-6, 7.5e-7, 5e-7, 2.5e-7, 7.5e-8, 5e-8, 2.5e-8, 1e-8]
TR_SENS_LIST = [0.1, 0.5, 1.5, 2]
RFIFO_SENS_LIST = [512, 1024, 2048, 8192, 16384]
RFIFO_SENS_P_LIST = [1e-7, 1e-8]

os.system(f'mkdir -p {OUTPUT_DIR}/ideal_baseline')
os.system(f'mkdir -p {OUTPUT_DIR}/rad')
os.system(f'mkdir -p {OUTPUT_DIR}/aq2_only')
os.system(f'mkdir -p {OUTPUT_DIR}/mwpm_only')
os.system(f'mkdir -p {OUTPUT_DIR}/decoder_switching')

for p in LER_SENS_LIST:
    os.system(f'mkdir -p {OUTPUT_DIR}/ler_sens_{p}')
for trmul in TR_SENS_LIST:
    os.system(f'mkdir -p {OUTPUT_DIR}/tr_sens_{trmul}')
for rfifo in RFIFO_SENS_LIST:
    for p in RFIFO_SENS_P_LIST:
        os.system(f'mkdir -p {OUTPUT_DIR}/rfifo_sens_{rfifo}_p{p}')

for b in BENCHMARKS:
    bpath = f'benchmarks/bin/optimized/{b}'
    bname = os.path.splitext(os.path.basename(bpath))[0]

    ## Experiment 1 -- ideal baseline (helios)
    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 4 -pp {PP} &> {OUTPUT_DIR}/ideal_baseline/{bname}.out'
#   print(cmd)

    ## Experiment 2 -- rad (helios + aq2)
    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 4 -pp {PP}'\
            f' -rad --rad-reaction-time {AQ2_TR}'\
            f' --rad-error-rate {P_HELIOS} --rad-rfifo-capacity {RFIFO_CAP}'\
            f' &> {OUTPUT_DIR}/rad/{bname}.out'
    print(cmd)

    ## Experiment 3 -- AQ2 only and MWPM only
    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {AQ2_TR} -nd {AQ2_ND} -pp {PP} &> {OUTPUT_DIR}/aq2_only/{bname}.out'
#   print(cmd)

    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {MWPM_TR} -nd {MWPM_ND} -pp {PP} &> {OUTPUT_DIR}/mwpm_only/{bname}.out'
#   print(cmd)

    ## Experiment 4 -- Decoder Switching Comparison
    cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {DSWITCH_TR} -nd {DSWITCH_ND} -pp {PP} &> {OUTPUT_DIR}/decoder_switching/{bname}.out'
#   print(cmd)

    ## Experiment 5 -- LER sensitivity
    for p in LER_SENS_LIST:
        cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 4 -pp {PP}'\
                f' -rad --rad-reaction-time {AQ2_TR}'\
                f' --rad-error-rate {p} --rad-rfifo-capacity {RFIFO_CAP}'\
                f' &> {OUTPUT_DIR}/ler_sens_{p}/{bname}.out'
        print(cmd)

    # Experiment 6  -- Reaction time sensitivity
    for trmul in TR_SENS_LIST:
        cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 4 -pp {PP}'\
                f' -rad --rad-reaction-time {AQ2_TR*trmul}'\
                f' --rad-error-rate {P_HELIOS} --rad-rfifo-capacity {RFIFO_CAP}'\
                f' &> {OUTPUT_DIR}/tr_sens_{trmul}/{bname}.out'
        print(cmd)

    # Experiment 7 -- RFIFO sensitivity
    for rfifo in RFIFO_SENS_LIST:
        for p in RFIFO_SENS_P_LIST:
            cmd = f'./build/qs_reaction_sim {bpath} {INST_SIM} -d {D} -tr {HELIOS_TR} -nd 4 -pp {PP}'\
                    f' -rad --rad-reaction-time {AQ2_TR}'\
                    f' --rad-error-rate {p} --rad-rfifo-capacity {rfifo}'\
                    f' &> {OUTPUT_DIR}/rfifo_sens_{rfifo}_p{p}/{bname}.out'
            print(cmd)
