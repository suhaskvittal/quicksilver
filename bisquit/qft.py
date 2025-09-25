'''
    author: Suhas Vittal
    date:   2025 September 22
'''

from common import *

NUM_BITS = 512

if __name__ == '__main__':
    output_file = f'bisquit/qasm/qft_{NUM_BITS}.qasm'

    print('AQFT max denominator: ', calculate_aqft_max_denom(NUM_BITS))

    with open(output_file, 'w') as ostrm:
        ostrm.write('OPENQASM 2.0;\n')
        ostrm.write('include "qelib1.inc";\n\n')
        ostrm.write(f'qreg q[{NUM_BITS}];\n')
        ostrm.write(qft('q', NUM_BITS, calculate_aqft_max_denom(NUM_BITS)))

