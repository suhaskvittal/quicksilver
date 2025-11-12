#!/bin/sh

rm commands.out
touch commands.out

# baseline results:
python3 scripts/dms/run_all_workloads.py baseline 12 1 viszlai 8 18 >> commands.out
python3 scripts/dms/run_all_workloads.py viszlai 12 1000 viszlai 8 0.018 >> commands.out
python3 scripts/dms/run_all_workloads.py hint 12 1000 hint 8 0.018 >> commands.out
python3 scripts/dms/run_all_workloads.py hint_c 12 1000 hint 8 0.018 -cs >> commands.out

# memsys slowdown sensitivity:
for SLOWDOWN in 5 10 100; 
do
    python3 scripts/dms/run_all_workloads.py viszlai_sd${SLOWDOWN} 12 ${SLOWDOWN} viszlai 8 0.018 >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_sd${SLOWDOWN} 12 ${SLOWDOWN} hint 8 0.018 >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_c_sd${SLOWDOWN} 12 ${SLOWDOWN} hint 8 0.018 -cs >> commands.out
done

# epr buffer capacity sensitivity:
for CAPACITY in 4 16 32; 
do
    python3 scripts/dms/run_all_workloads.py baseline_epr${CAPACITY} 12 1 viszlai ${CAPACITY} 0.018 >> commands.out
    python3 scripts/dms/run_all_workloads.py viszlai_epr${CAPACITY} 12 1000 viszlai ${CAPACITY} 0.018 >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_epr${CAPACITY} 12 1000 hint ${CAPACITY} 0.018 >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_c_epr${CAPACITY} 12 1000 hint ${CAPACITY} 0.018 -cs >> commands.out
done

# epr generation frequency sensitivity:
for FREQ in 0.0045 0.009 0.036 0.072;  # 0.25x, 0.5x, 2x, 4x
do
    python3 scripts/dms/run_all_workloads.py viszlai_freq${FREQ} 12 1000 viszlai 8 ${FREQ} >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_freq${FREQ} 12 1000 hint 8 ${FREQ} >> commands.out
    python3 scripts/dms/run_all_workloads.py hint_c_freq${FREQ} 12 1000 hint 8 ${FREQ} -cs >> commands.out
done;

# baseline generation frequency is multiplied by 1000
for FREQ in 4.5 9 36 72;  # 0.25x, 0.5x, 2x, 4x
do
    python3 scripts/dms/run_all_workloads.py baseline_freq${FREQ} 12 1 viszlai 8 ${FREQ} >> commands.out
done;
