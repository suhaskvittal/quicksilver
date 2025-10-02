'''
    author: Suhas Vittal 
    date:   01 October 2025
'''

from hamlib_snippets import *

#################################################################
#################################################################

TROTTER_TIME_DIVISION = 1000

#################################################################
#################################################################

def trotter_expand_pauli_string(ctrl: str, qr: str, term: list[(str, int)], coeff: float, time_division: int) -> str:
    out = ''
    t = term
    c = coeff/time_division
    
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
    ('fermi_hubbard_432', 'FH_D-3.hdf5', '/fh-graph-3D-grid-nonpbc-qubitnodes_Lx-6_Ly-6_Lz-6_U-0_enc-bk', 432),
#   ('fermi_hubbard_2000', 'FH_D-3.hdf5', '/fh-graph-3D-grid-nonpbc-qubitnodes_Lx-10_Ly-10_Lz-10_U-12_enc-bk'),
    ('v_c2h4o_ethylene_oxide_240', 'all-vib-c2h4o_ethylene_oxide.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 240),
    ('v_hc3h2cn_288', 'all-vib-hc3h2cn.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 288),
    ('e_cr2_120', 'Cr2.hdf5', '/ham_BK120', 120),
]
    

if __name__ == '__main__':
    for (output_file_name, input_file, key, num_qubits) in BENCHMARK_LIST[1:]:
        output_path = f'bisquit/qasm/{output_file_name}_td_{TROTTER_TIME_DIVISION}.qasm'

        with open(output_path, 'w') as f:
            f.write(f'OPENQASM 2.0;\n')
            f.write(f'include "qelib1.inc";\n')
            f.write(f'qreg q[{num_qubits}];\n')
            f.write(f'qreg ctrl;\n')

            f.write('h ctrl;\n')

            # implement ipea:
            print(f'reading {input_file}/{key}')
            i = 0
            for (labels, coeffs, max_qubit) in read_pauli_strings_hdf5(f'bisquit/hamlib/{input_file}', key):
                if i % 1_000_000 == 0:
                    print(f'\twriting term {i}')
                i += 1
                trotter_pauli_expansion = trotter_expand_pauli_string('ctrl', 'q', labels, coeffs, TROTTER_TIME_DIVISION)
                if len(trotter_pauli_expansion) > 0:
                    f.write(trotter_pauli_expansion)

            f.write('h ctrl;\n')


#################################################################
#################################################################