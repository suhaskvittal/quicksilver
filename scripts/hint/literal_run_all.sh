#!/bin/sh

./scripts/hint/run_all.sh compile
./scripts/launch_parallel.sh commands.out
./scripts/hint/run_all.sh sim
./scripts/launch_parallel.sh commands.out
