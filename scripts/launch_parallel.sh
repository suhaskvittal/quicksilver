SLF=~/.parallel/sshloginfile
RT=3
JOBLOG=joblog.out
COMMANDS=$1

parallel --sshloginfile ${SLF} --sshdelay 0.1 --joblog ${JOBLOG} --retries ${RT} --workdir $(pwd) --eta --bar < ${COMMANDS}
