SLF=~/.parallel/sshloginfile
RT=3
JOBLOG=joblog.out
COMMANDS=commands.out

parallel --sshloginfile ${SLF} --sshdelay 0.1 --joblog ${JOBLOG} --retries ${RT} --workdir $(pwd) --eta --bar < ${COMMANDS}
