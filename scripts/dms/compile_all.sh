#!/bin/sh

rm commands.out
touch commands.out

#python3 scripts/dms/compile.py viszlai 12 >> commands.out
#python3 scripts/dms/compile.py hint 12 >> commands.out


# sensitivity study:
python3 scripts/dms/compile.py viszlai 8 >> commands.out
python3 scripts/dms/compile.py viszlai 16 >> commands.out
python3 scripts/dms/compile.py viszlai 24 >> commands.out
python3 scripts/dms/compile.py viszlai 32 >> commands.out

python3 scripts/dms/compile.py hint 8 >> commands.out
python3 scripts/dms/compile.py hint 16 >> commands.out
python3 scripts/dms/compile.py hint 24 >> commands.out
python3 scripts/dms/compile.py hint 32 >> commands.out
