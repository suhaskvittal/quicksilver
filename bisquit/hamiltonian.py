'''
    author: Suhas Vittal 
    date:   01 October 2025
'''

from hamlib_snippets import *

#################################################################
#################################################################

TROTTER_TIME_DIVISION = 5
TERM_LIMIT = 1_000_000

#################################################################
#################################################################

def trotter_expand_pauli_string(ctrl: str, qr: str, term: list[(str, int)], c: float, time_division: int) -> str:
    out = ''
    t = term
    if len(t) == 0:
        return ''
    
    # do basis transformations:
    for (p,q) in t:
        if p == 'X':
            out += f'h {qr}[{q}];\n'
        elif p == 'Y':
            out += f'sxdg {qr}[{q}];\n'
    # now do two qubit ladder -- do from all qubits to final qubit (can be implemented with one multi-target CX)
    _, lq = t[-1]
    for (_,q) in t[:-1]:
        out += f'cx {qr}[{q}], {qr}[{lq}];\n'
    # do RZ from control to final qubit here: 
    out += f'crz({c}) {ctrl}, {qr}[{lq}];\n'
    _, lq = t[-1]
    for (_,q) in t[:-1][::-1]:
        out += f'cx {qr}[{q}], {qr}[{lq}];\n'
    # undo any basis transformations:
    for (p,q) in t:
        if p == 'X':
            out += f'h {qr}[{q}];\n'
        elif p == 'Y':
            out += f'sxdg {qr}[{q}];\n'
    return out

#################################################################
#################################################################

BENCHMARK_LIST = [
    ('v_c2h4o_ethylene_oxide_240', 'all-vib-c2h4o_ethylene_oxide.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 240),
    ('v_hc3h2cn_288', 'all-vib-hc3h2cn.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 288),
    ('e_cr2_120', 'Cr2.hdf5', '/ham_BK120', 120),
]
    
if __name__ == '__main__':
    for (output_file_name, input_file, key, num_qubits) in BENCHMARK_LIST:
        term_count = count_terms_hdf5(f'bisquit/hamlib/{input_file}', key)
        output_path = f'bisquit/qasm/{output_file_name}_trotter.qasm'
        print(output_path)

        with open(output_path, 'w') as f:
            f.write(f'OPENQASM 2.0;\n')
            f.write(f'include "qelib1.inc";\n')
            f.write(f'qreg q[{num_qubits}];\n')
            f.write(f'qreg ctrl;\n')

            f.write('h ctrl;\n')

            # compute normalization constant:
            n = 0
            approx_lambda_max = 0.0
            threshold = 0.0
            for (labels, coeff, _) in read_pauli_strings_hdf5(f'bisquit/hamlib/{input_file}', key):
                approx_lambda_max += abs(coeff)
                threshold += abs(coeff)
                n += 1

            # implement ipea:
            threshold = threshold / n
            print(f'reading {input_file}/{key}, threshold = {threshold}')
            i = 0
            for (labels, coeff, _) in read_pauli_strings_hdf5(f'bisquit/hamlib/{input_file}', key):
                if abs(coeff) < threshold:
                    continue

                if i % 100_000 == 0:
                    print(f'\twriting term {i}')
                if i >= TERM_LIMIT:
                    break

                coeff *= 1/(2*approx_lambda_max)
                
                i += 1
                trotter_pauli_expansion = trotter_expand_pauli_string('ctrl', 'q', labels, coeff, TROTTER_TIME_DIVISION)
                if len(trotter_pauli_expansion) > 0:
                    f.write(trotter_pauli_expansion)

            f.write('h ctrl;\n')


#################################################################
#################################################################
