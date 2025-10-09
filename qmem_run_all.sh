#!/bin/bash

rm compile_commands.out
rm sim_commands.out
touch compile_commands.out
touch sim_commands.out

python3 qmem_run_workloads.py viszlai compile >> compile_commands.out
python3 qmem_run_workloads.py dpt compile >> compile_commands.out

python3 qmem_run_workloads.py baseline sim >> sim_commands.out
python3 qmem_run_workloads.py viszlai sim >> sim_commands.out
python3 qmem_run_workloads.py viszlai_near_memory sim >> sim_commands.out
python3 qmem_run_workloads.py dpt sim >> sim_commands.out
python3 qmem_run_workloads.py dpt_near_memory sim >> sim_commands.out
