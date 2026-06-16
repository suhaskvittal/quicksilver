# author: Suhas Vittal

from scripts.common import *
from sys import argv

import lzma

PROJECT = 'oclk_micro2026'

'''
    Simulation settings:
'''
SIM_INST_COUNT = 10_000_000
PRINT_PROGRESS = SIM_INST_COUNT
COMPILE_INST_COUNT = 5*SIM_INST_COUNT

'''
    System settings shared by every experiment in this study:
        - gigaquop (G) error-rate regime
        - 4 active qubits in the compute subsystem
        - reaction time of 10 compute cycles
        - 5000 physical qubits for the magic-state footprint (baseline)
'''
REGIME = 'G'
COMPUTE_CAPACITY = 4
REACTION_TIME = 10
BASELINE_FOOTPRINT = 5000

'''
    CaT = reduced factory readout latency. The readout-latency fraction is the
    portion of the readout latency that is retained, so fractions of
    0.8/0.7/0.6/0.5 correspond to readout-latency reductions of 20/30/40/50%.
'''
CAT_FRACTIONS = [0.8, 0.7, 0.6, 0.5]

'''
    Magic-state footprint multipliers for the footprint-sensitivity sweep.
'''
FOOTPRINT_MULTIPLIERS = [0.5, 1.5, 2.0]

'''
    Benchmarks targeted by this study: shor, grover, hc3h2cn, ethylene, cr2.
    The chemistry benchmarks use the `_t` trace variant.
'''
OCLK_WORKLOADS = [
    'benchmarks/bin/BQ_shor_rsa256_iter_4.xz',
    'benchmarks/bin/BQ_grover_3sat_schoning_1710.xz',
    'benchmarks/bin/BQ_hc3h2cn_t.xz',
    'benchmarks/bin/BQ_c2h4o_ethylene_oxide_t.xz',
    'benchmarks/bin/BQ_chromium_t.xz',
]

##############################################
##############################################

def reduction_tag(frac: float) -> str:
    # 0.8 -> 'r20', 0.7 -> 'r30', ...
    return f'r{round((1.0-frac)*100)}'

def get_num_program_qubits(workload_file_path: str) -> int:
    # the trace header stores the program qubit count as a little-endian uint32.
    with lzma.open(workload_file_path, 'rb') as f:
        return int.from_bytes(f.read(4), 'little')

def total_inst(workload_file_path: str) -> int:
    # only used for resource estimates (does not affect performance). Some
    # workloads (e.g. archived shor) ship without a stats file -- fall back to
    # the simulator default in that case.
    try:
        return get_total_inst_count_for_workload(workload_file_path)
    except FileNotFoundError:
        return 1_000_000_000

def make_kwargs(readout_fraction=None) -> dict:
    kw = {
        '--regime': REGIME,
        '--reaction-time': REACTION_TIME,
    }
    if readout_fraction is not None:
        kw['--factory-readout-latency-fraction'] = readout_fraction
    return kw

##############################################
##############################################

experiment = argv[1]

if experiment == 'compile_eif':
    for w in OCLK_WORKLOADS:
        run_memory_scheduler(w, PROJECT, 'eif',
                             active_set_capacity=COMPUTE_CAPACITY,
                             inst_limit=COMPILE_INST_COUNT,
                             scheduler_id=0)

# Set 1: baseline system.
if experiment == 'sim_baseline':
    for w in OCLK_WORKLOADS:
        run_quicksilver(w, PROJECT, 'baseline', 'eif',
                        inst_limit=SIM_INST_COUNT,
                        active_set_capacity=COMPUTE_CAPACITY,
                        total_program_inst=total_inst(w),
                        print_progress=PRINT_PROGRESS,
                        factory_budget=BASELINE_FOOTPRINT,
                        kwargs=make_kwargs())

# Set 2: CaT system (reduced factory readout latency).
if experiment == 'sim_cat':
    for w in OCLK_WORKLOADS:
        for frac in CAT_FRACTIONS:
            run_quicksilver(w, PROJECT, f'cat_{reduction_tag(frac)}', 'eif',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=COMPUTE_CAPACITY,
                            total_program_inst=total_inst(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=BASELINE_FOOTPRINT,
                            kwargs=make_kwargs(readout_fraction=frac))

# Set 3: magic-state footprint sensitivity (baseline + all CaT configs).
if experiment == 'sim_footprint':
    for w in OCLK_WORKLOADS:
        for mult in FOOTPRINT_MULTIPLIERS:
            footprint = int(BASELINE_FOOTPRINT*mult)  # 2500, 7500, 10000
            run_quicksilver(w, PROJECT, f'baseline_msf{footprint}', 'eif',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=COMPUTE_CAPACITY,
                            total_program_inst=total_inst(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=footprint,
                            kwargs=make_kwargs())
            for frac in CAT_FRACTIONS:
                run_quicksilver(w, PROJECT, f'cat_{reduction_tag(frac)}_msf{footprint}', 'eif',
                                inst_limit=SIM_INST_COUNT,
                                active_set_capacity=COMPUTE_CAPACITY,
                                total_program_inst=total_inst(w),
                                print_progress=PRINT_PROGRESS,
                                factory_budget=footprint,
                                kwargs=make_kwargs(readout_fraction=frac))

# Set 4: no memory subsystem (compute subsystem = number of program qubits).
# At full capacity the schedule has no load/store traffic, so we simulate the
# uncompiled trace directly.
if experiment == 'sim_nomem':
    for w in OCLK_WORKLOADS:
        nq = get_num_program_qubits(w)
        run_quicksilver(w, PROJECT, 'baseline_nomem', 'eif',
                        inst_limit=SIM_INST_COUNT,
                        active_set_capacity=nq,
                        total_program_inst=total_inst(w),
                        print_progress=PRINT_PROGRESS,
                        factory_budget=BASELINE_FOOTPRINT,
                        use_compiled_binary=False,
                        kwargs=make_kwargs())
        for frac in CAT_FRACTIONS:
            run_quicksilver(w, PROJECT, f'cat_{reduction_tag(frac)}_nomem', 'eif',
                            inst_limit=SIM_INST_COUNT,
                            active_set_capacity=nq,
                            total_program_inst=total_inst(w),
                            print_progress=PRINT_PROGRESS,
                            factory_budget=BASELINE_FOOTPRINT,
                            use_compiled_binary=False,
                            kwargs=make_kwargs(readout_fraction=frac))
