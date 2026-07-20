'''
    author: Suhas Vittal 
    date:   01 October 2025
'''

from hamlib_snippets import *
from common import *

#################################################################
#################################################################

def _trotterization_write_pauli_string_ops(term: list[tuple[str,int]],
                                            coeff: float,
                                            main_register: str,
                                            ctrl: str
) -> str:
    '''
        Generates a QASM string corresponding to the operations required
        to implement a term of the Hamiltonian in Trotterization.
    '''
    if len(term) == 0:
        return ''

    # do basis transformation:
    fwd_basis_xform, bck_basis_xform, cx_slide = '', '', ''
    _, lq = term[-1]
    for (p,q) in term:
        if p == 'I':
            continue
        if p == 'X':
            fwd_basis_xform += str(GATE('h').operand(main_register, q))
            bck_basis_xform += str(GATE('h').operand(main_register, q))
        elif p == 'Y':
            fwd_basis_xform += str(GATE('h').operand(main_register, q)) \
                                + str(GATE('sxdg').operand(main_register, q))
            bck_basis_xform += str(GATE('sx').operand(main_register, q)) \
                                + str(GATE('h').operand(main_register, q))
        if q != lq:
            cx_slide += str(GATE('cx').operand(main_register, q, lq))
    
    out = fwd_basis_xform
    out += cx_slide
    out += str(GATE('crz').arg(coeff).operand(ctrl).operand(main_register, lq))
    out += cx_slide
    out += bck_basis_xform
    return out

def build_trotterization(output_file: str, 
                         input_file: str, 
                         hamlib_key: str,
                         num_qubits: int,
                         one_norm: float,
                         trotter_steps=10
):
    ''' 
        Writes QASM for QPE + trotterization to the given output file.
        `input_file` is expected to be an hdf5 file from Hamlib.
    '''
    CTRL = 'ctrl'
    MAIN_REGISTER ='q'

    with open(output_file, 'w') as f:
        # write the preamble:
        f.write(f'''OPENQASM 2.0;
`include "qelib1.inc";
qreg {MAIN_REGISTER}[{num_qubits}];
qreg {CTRL};

h {CTRL};
''')
        normalization_factor = math.pi / (one_norm*trotter_steps)
        term_count = count_terms_hdf5(input_file, hamlib_key)
        threshold = 0.01 * one_norm / term_count
        term_number = 0

        trotter_layers = []
        trotter_layer_qubits = []
        for (labels, coeff, _) in read_pauli_strings_hdf5(input_file, hamlib_key):
            if abs(coeff) < threshold:
                continue

            if term_number % 100_000 == 0:
                print(f'\twriting term {term_number}')
            term_number += 1
            
            c = coeff * normalization_factor

            # Add to `trotter_layers`:
            qubits = [q for (p,q) in labels]
            j = 0
            while j < len(trotter_layers):
                if any(q in trotter_layer_qubits for q in qubits):
                    j += 1
                    continue
                else:
                    break
            if j >= len(trotter_layers):
                trotter_layers.append([])
                trotter_layer_qubits.append(set())
            trotter_layers[j].append((labels, c))
            for q in qubits:
                trotter_layer_qubits[j].add(q)

        print(f'layers: {len(trotter_layers)}')
        for layer in trotter_layers:
            for (labels, c) in layer:
                txt = _trotterization_write_pauli_string_ops(labels, c, MAIN_REGISTER, CTRL)
                f.write(txt)
        f.write(f'h {CTRL};\n')

#################################################################
#################################################################

def _qubitization_build_prepare_ry_tree(input_file: str, hamlib_key: str, num_qubits: int) -> list[list]:
    '''
        Prepares a tree containing angles needed for the PREPARE oracle in qubitization.
    '''

    term_count = 2**num_qubits
    levels = num_qubits
    tree = [[(0,0) for _ in range(2**i)] for i in range(levels)] # each entry is numerator, denominator

    # read thru the terms
    term_number = 0
    for (labels, coeff, _) in read_pauli_strings_hdf5(input_file, hamlib_key):
        # update each level of the tree:
        for i in range(levels):
            j = (term_number * (2**i)) // (term_count)
            n, d = tree[i][j]
            # `j` places `term_number` into a bucket. Now, if `term_number` is in the left half
            # of that bucket, update `n`
            bucket_width = term_count / (2**i)
            bucket_min = j * bucket_width
            bucket_max = bucket_min + bucket_width
            if term_number < (bucket_min+bucket_max)//2:
                n += abs(coeff)
            # d is always updated
            d += abs(coeff)
            tree[i][j] = (n,d)
        term_number += 1

    # finally, replace each entry in the tree, currently a tuple `(n,d)`, 
    # which an angle `A` such that `cos(A/2)**2 = n/d`
    # coefficient
    for i in range(levels):
        for j in range(len(tree[i])):
            n, d = tree[i][j]
            if n == 0 or d == 0:
                A = 0
            else:
                A = 2*math.acos(math.sqrt(n/d))
            tree[i][j] = A
    return tree

def _qubitization_ry_prepare_unary_iteration_helper(contents: list[float], 
                                                    left_idx: int,
                                                    right_idx: int,
                                                    d: int,
                                                    ctrl: str,
                                                    phase_register: str,
                                                    ancilla: str
) -> str:
    '''
        Recursive helper function for `_qubitization_ry_prepare_unary_iteration()`

        `contents`: contents of the tree level we are doing unary iteration for
        `left_idx`: left pointer for contents
        `right_idx`: right pointer for contents
        `d`: recursion depth, used to index ancilla and phase registers
        `ctrl`: previous control qubit
    '''
    out = ''
    if left_idx == right_idx-1:
        x = contents[left_idx]
        out += str(GATE('cry').arg(x).operand(ctrl).operand(phase_register, d+1))
    else:
        middle_idx = (right_idx + left_idx)//2
        out += str(GATE('x').operand(phase_register, d+1)) \
                + str(GATE('ccx').operand(ctrl).operand(phase_register, d+1).operand(ancilla, d)) \
                + str(GATE('x').operand(phase_register, d+1))
        out += _qubitization_ry_prepare_unary_iteration_helper(contents,
                                                               left_idx,
                                                               middle_idx,
                                                               d+1, 
                                                               f'{ancilla}[{d}]',
                                                               phase_register,
                                                               ancilla)
        out += str(GATE('cx').operand(ctrl).operand(ancilla, d))
        out += _qubitization_ry_prepare_unary_iteration_helper(contents,
                                                               middle_idx,
                                                               right_idx,
                                                               d+1, 
                                                               f'{ancilla}[{d}]',
                                                               phase_register,
                                                               ancilla)
        out += str(GATE('mx').operand(ancilla, d))
    return out

def _qubitization_ry_prepare_unary_iteration(tree: list[list], level: int, phase_register: str, ancilla: str) -> str:
    '''
        Performs unary iteration to apply rotations in a given level of `tree`. This is T-efficient.
        There are |phase_register| - 2 ancilla qubits required in the worst case.

        `tree` is the RY tree
        `level` is the level of the RY tree we should operate on
        `phase_register` and `ancilla` are the names of the respective registers.
    '''

    out = str(GATE('x').operand(phase_register, 0))
    out += _qubitization_ry_prepare_unary_iteration_helper(tree[level],
                                                            0,  # left
                                                            len(tree[level])//2, # right
                                                            0,
                                                            f'{phase_register}[0]',
                                                            phase_register,
                                                            ancilla)
    out += str(GATE('x').operand(phase_register, 0))
    out += _qubitization_ry_prepare_unary_iteration_helper(tree[level],
                                                            len(tree[level])//2,  # left
                                                            len(tree[level]),
                                                            0,
                                                            f'{phase_register}[0]',
                                                            phase_register,
                                                            ancilla)
    return out

def _qubitization_ry_prepare(input_file: str, 
                             hamlib_key: str, 
                             num_phase_qubits: int,
                             phase_register: str, 
                             ancilla_register: str
) -> str:
    ry_tree = _qubitization_build_prepare_ry_tree(input_file, hamlib_key, num_phase_qubits)

    # first level of the tree is rather easy to translate (just an unconditional RY)
    out = str(GATE("ry").arg(ry_tree[0][0]).operand(phase_register, 0))
    
    # second level of the tree is also simple -- just CRY gates:
    for j in [0,1]:
        if j == 0:
            out += str(GATE("x").operand(phase_register, 0))
        out += str(GATE("cry").arg(ry_tree[1][j]).operand(phase_register, 0, 1))
        if j == 0:
            out += str(GATE("x").operand(phase_register, 0))

    # third level onward will need unary iteration to avoid high T count
    for i in range(2, num_phase_qubits):  # `i` is the level
        out += _qubitization_ry_prepare_unary_iteration(ry_tree, i, phase_register, ancilla_register)
    return out

def _qubitization_select_unary_iteration_helper(gen_pauli_term,
                                                d: int,
                                                num_phase_qubits: int,
                                                system_register: str,
                                                phase_register: str,
                                                ancilla: str,
                                                ctrl: str
) -> str:
    '''
        Recursive helper function for `_qubitization_select_unary_iteration()`
    '''

    out = ''
    if d == num_phase_qubits:
        # then perform the controlled operations (only need single control due to unary iteration).
        term_data = next(gen_pauli_term, None)
        if term_data is None:
            return out
        labels, _, _ = term_data
        for (p,q) in labels:
            if p == 'X':
                out += str(GATE('cx').operand(ctrl).operand(system_register, q))
            elif p == 'Y':
                out += str(GATE('cy').operand(ctrl).operand(system_register, q))
            elif p == 'Z':
                out += str(GATE('cz').operand(ctrl).operand(system_register, q))
    else:
        out += str(GATE('x').operand(phase_register, d)) \
                + str(GATE('ccx').operand(ctrl).operand(phase_register, d).operand(ancilla, d))
        out += _qubitization_select_unary_iteration_helper(gen_pauli_term,
                                                           d+1,
                                                           num_phase_qubits,
                                                           system_register,
                                                           phase_register,
                                                           ancilla,
                                                           f'{ancilla}[{d}]')
        out += str(GATE('cx').operand(ctrl).operand(ancilla, d))
        out += _qubitization_select_unary_iteration_helper(gen_pauli_term,
                                                           d+1,
                                                           num_phase_qubits,
                                                           system_register,
                                                           phase_register,
                                                           ancilla,
                                                           f'{ancilla}[{d}]')
        out += str(GATE('mx').operand(ancilla, d))
    return out

def _qubitization_select_unary_iteration(gen_pauli_term, 
                                         num_phase_qubits: int,
                                         system_register: str,
                                         phase_register: str,
                                         ancilla: str,
                                         ctrl: str
) -> str:
    '''
        Performs unary iteration to apply the controlled operations that implement each Hamiltonian term.
        Needs `num_phase_qubits` ancilla qubits.
        
        `gen_pauli_term` is a generator for the Pauli terms.
        `num_phase_qubits` is the width of the phase register and determines the depth of recursion required.
        `system_register`, `phase_register`, `ancilla`, and `ctrl` are the names of the respective registers.

        Note `ctrl` is the control qubit for phase estimation.
    '''
    return _qubitization_select_unary_iteration_helper(gen_pauli_term,
                                                       0,
                                                       num_phase_qubits,
                                                       system_register,
                                                       phase_register,
                                                       ancilla,
                                                       ctrl)

def _qubitization_select(input_file: str,
                         hamlib_key: str,
                         num_phase_qubits: int,
                         system_register: str,
                         phase_register: str,
                         ancilla: str,
                         ctrl: str
) -> str:
    gen_pauli_term = read_pauli_strings_hdf5(input_file, hamlib_key)
    return _qubitization_select_unary_iteration(gen_pauli_term, 
                                                num_phase_qubits,
                                                system_register,
                                                phase_register,
                                                ancilla,
                                                ctrl)

def build_qubitization(output_file: str, 
                       input_file: str,
                       hamlib_key: str,
                       num_qubits: str
):
    CTRL = 'ctrl'
    MAIN_REGISTER = 'q'
    PHASE_REGISTER = 'psi'
    ANCILLA = 'a'
    
    with open(output_file, 'w') as f:
        term_count = count_terms_hdf5(input_file, hamlib_key)
        # width of phase register is log2 of term count
        num_phase_qubits = log2_round_up(term_count)
        num_ancilla = num_phase_qubits

        # write preamble:
        f.write(f'''OPENQASM 2.0;
`include "qelib1.inc";
qreg {MAIN_REGISTER}[{num_qubits}];
qreg {PHASE_REGISTER}[{num_phase_qubits}];
qreg {ANCILLA}[{num_ancilla}];
qreg {CTRL};

h {CTRL};
''')
        # We'll do one iteration of PREPARE*SELECT*PREPAREdg
        # for simplicity, just copy PREPARE for its inverse
        print('generating prepare...')
        prepare = _qubitization_ry_prepare(input_file, hamlib_key, num_phase_qubits, PHASE_REGISTER, ANCILLA) 
        print('generating select...')
        select = _qubitization_select(input_file, hamlib_key, num_phase_qubits, MAIN_REGISTER, PHASE_REGISTER, ANCILLA, CTRL)
        f.write(prepare + select + prepare)

#################################################################
#################################################################

BENCHMARK_LIST = [
#   ('boron', 'B2.hdf5', '/ham_BK-52', 52, 508.65)
    ('chromium', 'Cr2.hdf5', '/ham_BK120', 120, 5796.98),
    ('manganese_nitride', 'MnN.hdf5', '/ham_BK88', 88, 3091.48),
    ('hc3h2cn', 'all-vib-hc3h2cn.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 288, 3850401385.76),
    ('c2h4o_ethylene_oxide', 'all-vib-c2h4o_ethylene_oxide.hdf5', '/enc_unary_dvalues_16-16-16-16-16-16-16-16-16-16-16-16-16-16-16', 240, 1448164628.78),
    ('bose_hubbard', 'BH_D-3_d-8.hdf5', '/bh_graph-3D-grid-pbc-qubitnodes_Lx-9_Ly-9_Lz-9_U-90_enc-gray_d-8', 2187, 1492968.02)
]
    
if __name__ == '__main__':
    def make_output_file_path(filename, suffix):
        return f'bisquit/qasm/{filename}_{suffix}.qasm'

    for (output_filename, input_file, key, num_qubits, one_norm) in BENCHMARK_LIST:
        trotterization_output_path = make_output_file_path(output_filename, 't')
        qubitization_output_path = make_output_file_path(output_filename, 'q')

        input_file = f'bisquit/hamlib/{input_file}'
    
        print(f'Now building: {output_filename}')
        print('TROTTERIZATION --------------------------------------------------')
        build_trotterization(trotterization_output_path, input_file, key, num_qubits, one_norm)

        print('QUBITIZATION ----------------------------------------------------')
        build_qubitization(qubitization_output_path, input_file, key, num_qubits)




#################################################################
#################################################################
