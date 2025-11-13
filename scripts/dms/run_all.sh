#!/bin/sh

rm commands.out
touch commands.out

DEFAULT_CC=12
DEFAULT_FREQ=0.036
DEFAULT_BUFCAP=8
DEFAULT_SLOWDOWN=1000

# baseline results:
python3 scripts/dms/run_all_workloads.py baseline $DEFAULT_CC 1 viszlai $DEFAULT_BUFCAP 36 >> commands.out
python3 scripts/dms/run_all_workloads.py viszlai $DEFAULT_CC $DEFAULT_SLOWDOWN viszlai $DEFAULT_BUFCAP $DEFAULT_FREQ >> commands.out
python3 scripts/dms/run_all_workloads.py hint $DEFAULT_CC $DEFAULT_SLOWDOWN hint $DEFAULT_BUFCAP $DEFAULT_FREQ >> commands.out
python3 scripts/dms/run_all_workloads.py hint_c $DEFAULT_CC $DEFAULT_SLOWDOWN hint $DEFAULT_BUFCAP $DEFAULT_FREQ -cs >> commands.out

# memsys slowdown sensitivity:
for SLOWDOWN in 5 10 100; 
do
    python3 scripts/dms/run_all_workloads.py viszlai_sd${SLOWDOWN} $DEFAULT_CC ${SLOWDOWN} viszlai $DEFAULT_BUFCAP $DEFAULT_FREQ >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_sd${SLOWDOWN} $DEFAULT_CC ${SLOWDOWN} hint $DEFAULT_BUFCAP $DEFAULT_FREQ >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_c_sd${SLOWDOWN} $DEFAULT_CC ${SLOWDOWN} hint $DEFAULT_BUFCAP $DEFAULT_FREQ -cs >> commands.out
done

# epr buffer capacity sensitivity:
for CAPACITY in 4 16 32; 
do
    python3 scripts/dms/run_all_workloads.py baseline_epr${CAPACITY} $DEFAULT_CC 1 viszlai ${CAPACITY} $DEFAULT_FREQ >> commands.out
    python3 scripts/dms/run_all_workloads.py viszlai_epr${CAPACITY} $DEFAULT_CC $DEFAULT_SLOWDOWN viszlai ${CAPACITY} $DEFAULT_FREQ >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_epr${CAPACITY} $DEFAULT_CC $DEFAULT_SLOWDOWN hint ${CAPACITY} $DEFAULT_FREQ >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_c_epr${CAPACITY} $DEFAULT_CC $DEFAULT_SLOWDOWN hint ${CAPACITY} $DEFAULT_FREQ -cs >> commands.out
done

# epr generation frequency sensitivity:
for FREQ in 0.0045 0.009 0.036 0.072;  # 0.25x, 0.5x, 2x, 4x
do
    python3 scripts/dms/run_all_workloads.py viszlai_freq${FREQ} $DEFAULT_CC $DEFAULT_SLOWDOWN viszlai $DEFAULT_BUFCAP ${FREQ} >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_freq${FREQ} $DEFAULT_CC $DEFAULT_SLOWDOWN hint $DEFAULT_BUFCAP ${FREQ} >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_c_freq${FREQ} $DEFAULT_CC $DEFAULT_SLOWDOWN hint $DEFAULT_BUFCAP ${FREQ} -cs >> commands.out
done;

# baseline generation frequency is multiplied by 1000
for FREQ in 4.5 9 36 72;  # 0.25x, 0.5x, 2x, 4x
do
    python3 scripts/dms/run_all_workloads.py baseline_freq${FREQ} $DEFAULT_CC 1 viszlai $DEFAULT_BUFCAP ${FREQ} >> commands.out
done;
