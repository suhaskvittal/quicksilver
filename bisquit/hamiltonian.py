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

def trotter_step(ctrl: str, qr: str, terms: list[str], coeffs: list[float], time_division: int) -> str:
    out = ''
    for i in range(len(terms)):
        t = terms[i]
        c = coeffs[i]/time_division
        
        active_qubits = [q for q in range(len(t)) if t[q] != 'I']
        # do basis transformations:
        for q in active_qubits:
            if t[q] == 'X':
                out += f'h {qr}[{q}];\n'
            elif t[q] == 'Y':
                out += f'sxdg {qr}[{q}];\n'
        # now do two qubit ladder -- do from all qubits to final qubit (can be implemented with one multi-target CX)
        lq = active_qubits[-1]
        for q in active_qubits[:-1]:
            out += f'cx {qr}[{q}], {qr}[{lq}];\n'
        # do RZ from control to final qubit here: 
        out += f'crz({c}) {ctrl}, {qr}[{lq}];\n'
        # undo any basis transformations:
        for q in active_qubits:
            if t[q] == 'X':
                out += f'h {qr}[{q}];\n'
            elif t[q] == 'Y':
                out += f'sxdg {qr}[{q}];\n'
    return out

def ipea(ctrl: str, qr: str, terms: list[str], coeffs: list[float]) -> str:
    out = ''
    trotter_step(ctrl, qr, terms_coeffs, TROTTER_TIME_DIVISION, previous_bits)
    U = ''.join(trotter_step(ctrl, qr, terms_coeffs, TROTTER_TIME_DIVISION, previous_bits) for _ in range(time_division))

    out += f'h {ctrl};\n'
    out += U
    out += f'h {ctrl};\n'
    return out

#################################################################
#################################################################

BENCHMARK_LIST = [
    ('fermi_hubbard_432', 'FH_D-3.hdf5', '/fh-graph-3D-grid-nonpbc-qubitnodes_Lx-6_Ly-6_Lz-6_U-0_enc-bk')
    ('fermi_hubbard_2000', 'FH_D-3.hdf5', '/fh-graph-3D-grid-nonpbc-qubitnodes_Lx-10_Ly-10_Lz-10_U-12_enc-bk'),
    ('v_c2h4o_ethylene_oxide_240', 'all-vib-c2h4o_ethylene_oxide.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16'),
    ('v_hc3h2cn_288', 'all-vib-hc3h2cn.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16'),
    ('e_cr2_120', 'Cr2.hdf5', '/ham_BK120')
]
    

if __name__ == '__main__':
    pass
#################################################################
#################################################################