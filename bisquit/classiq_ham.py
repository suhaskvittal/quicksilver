'''
    author: Suhas Vittal
    date:   2025 September 22
'''

from classiq import *
from classiq.qmod.symbolic import ceiling as ceiling_qmod, pi
from common import *
from hamlib_snippets import *

import math

#############################################
#############################################

ORDER = 2
REPS = 10
QPE_SIZE = 3
TERM_LIMIT = 10000

#############################################
#############################################

from sys import argv

BENCHMARK_LIST = [
    ('v_c2h4o_ethylene_oxide_240', 'all-vib-c2h4o_ethylene_oxide.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 240),
    ('v_hc3h2cn_288', 'all-vib-hc3h2cn.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 288),
    ('e_cr2_120', 'Cr2.hdf5', '/ham_BK120', 120),
]

if __name__ == '__main__':
    prefs = Preferences(optimization_level=0, timeout_seconds=60*60)

    for (output_file_name, input_file, key, num_qubits) in BENCHMARK_LIST:
        term_count = count_terms_hdf5(f'bisquit/hamlib/{input_file}', key)
        output_path = f'bisquit/qasm/{output_file_name}_st{ORDER}_r{REPS}.qasm'

        HAM = []
        approx_lambda_max = 0.0

        # implement ipea:
        print(f'reading {input_file}/{key}')
        i = 0
        for (labels, coeff, _) in read_pauli_strings_hdf5(f'bisquit/hamlib/{input_file}', key):
            # Trim small terms
            if coeff < 0.01:
                continue

            if i % (TERM_LIMIT//10) == 0:
                print(f'\twriting term {i}')
            if i >= TERM_LIMIT:
                break
            
            term = []
            for (p,q) in labels:
                if p == 'X':
                    term.append(IndexedPauli(Pauli.X, q))
                elif p == 'Y':
                    term.append(IndexedPauli(Pauli.Y, q))
                elif p == 'Z':
                    term.append(IndexedPauli(Pauli.Z, q))
            HAM.append(SparsePauliTerm(term, coefficient=coeff))
            approx_lambda_max += abs(coeff)

            i += 1

        HAM_SPARSE = SparsePauliOp(HAM, num_qubits) * (1/(2*approx_lambda_max))

        @qfunc
        def evo(p: CInt, state: QArray[QBit]):
            suzuki_trotter(pauli_operator=HAM_SPARSE,
                           evolution_coefficient=2*math.pi*p,
                           order=ORDER,
                           repetitions=ceiling_qmod(0.05 * p ** (3/2)),
                           qbv=state)

        @qfunc
        def main(state: Output[QArray[QBit, num_qubits]], phase: Output[QNum[3, SIGNED, 3]]):
            allocate(num_qubits, state)
            allocate(phase)
            qpe_flexible(lambda p: evo(p, state), phase)

        print('compiling quantum circuit...')
        qmod = create_model(main, preferences=prefs, out_file=f'bisquit/classiq/{output_file_name}.qmod')
        qcirc = synthesize(qmod)
        qcirc.save(f'bisquit/qasm/{output_file_name}.qasm')

#############################################
#############################################
