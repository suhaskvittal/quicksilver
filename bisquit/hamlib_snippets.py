import numpy as np
import networkx as nx
import openfermion as of
import h5py
import re
from qiskit.quantum_info import SparsePauliOp

#################################################################
#################################################################


def parse_through_hdf5(func):
    """
    Decorator function that iterates through an HDF5 file and performs
    the action specified by ‘ func ‘ on the internal and leaf nodes in the
    HDF5 file.
    """

    def wrapper(obj, path='/', key=None):
        if type(obj) in [h5py._hl.group.Group, h5py._hl.files.File]:
            for ky in obj.keys():
                func(obj, path, key=ky, leaf=False)
                wrapper(obj=obj[ky], path=path + ky + '/', key=ky)
        elif type(obj) is h5py._hl.dataset.Dataset:
            func(obj, path, key=None, leaf=True)
    return wrapper


def print_hdf5_structure(fname_hdf5: str):
    """
    Print the path structure of the HDF5 file.

    Args
    ----
    fname_hdf5 ( str ) : full path where HDF5 file is stored
    """

    @parse_through_hdf5
    def action(obj, path='/', key=None, leaf=False):
        if key is not None:
            print(
                (path.count('/') - 1) * '\t', '-', key, ':', path + key + '/'
            )
        if leaf:
            print((path.count('/') - 1) * '\t', '[^^ DATASET ^^]')

    with h5py.File(fname_hdf5, 'r') as f:
        action(f['/'])


def get_hdf5_keys(fname_hdf5: str):
    """ Get a list of keys to all datasets stored in the HDF5 file.

    Args
    ----
    fname_hdf5 ( str ) : full path where HDF5 file is stored
    """

    all_keys = []

    @parse_through_hdf5
    def action(obj, path='/', key=None, leaf=False):
        if leaf is True:
            all_keys.append(path[:-1])

    with h5py.File(fname_hdf5, 'r') as f:
        action(f['/'])
    return all_keys


#################################################################
#################################################################

def read_pauli_string_text(text: str):
    """
    Split the text by '+' between each term and parse the Pauli strings.
    
    Args:
        text (str): The text containing Pauli string terms separated by '+'
        
    Returns:
        tuple: (labels, coeffs) where labels is a list of Pauli string labels
               and coeffs is a list of corresponding coefficients
    """
    import re
    
    labels = []
    coeffs = []

    # Use regex to split on '+' that are not inside parentheses
    # This pattern matches '+' that are followed by whitespace and not inside parentheses
    pattern = r'\+\s*(?![^()]*\))'
    terms = re.split(pattern, text)
    
    num_qubits = 0
    for term in terms:
        term = term.strip()
        if not term:  # Skip empty terms
            continue
            
        # Split coefficient and Pauli string
        parts = term.split()
        if len(parts) < 2:
            continue
            
        # Extract coefficient (remove parentheses and convert to float)
        coeff_str = parts[0]
        if coeff_str.startswith('(') and coeff_str.endswith('j)'):
            # Handle complex numbers like (-1761.1833868195836+0j)
            coeff_str = coeff_str[1:-2]  # Remove ( and j)
            if '+' in coeff_str:
                coeff_str = coeff_str.split('+')[0]  # Take real part
            elif '-' in coeff_str and coeff_str[0] != '-':
                coeff_str = coeff_str.split('-')[0]  # Take real part
        elif coeff_str.startswith('(') and coeff_str.endswith(')'):
            coeff_str = coeff_str[1:-1]  # Remove parentheses
            
        try:
            coeff = float(coeff_str)
        except ValueError:
            continue
            
        # Extract Pauli string from brackets
        pauli_str = ' '.join(parts[1:])  # Join remaining parts
        lbracket_idx = pauli_str.find('[')
        rbracket_idx = pauli_str.find(']')
        
        if lbracket_idx != -1 and rbracket_idx != -1:
            pstring = pauli_str[lbracket_idx+1:rbracket_idx]
            # Parse Pauli string into list of (operator, qubit) tuples
            # Empty Pauli string represents identity operator
            pauli_parts = pstring.split()
            label = []
            for part in pauli_parts:
                if len(part) >= 2:
                    operator = part[0]  # X, Y, or Z
                    qubit = int(part[1:])  # qubit index
                    label.append((operator, qubit))
                    num_qubits = max(num_qubits, qubit+1)
            
            # Add the term (empty label represents identity operator)
            labels.append(label)
            coeffs.append(coeff)
    
    return labels, coeffs, num_qubits

#################################################################
#################################################################


def split_text_by_plus(text: str):
    """
    Simple function to split text by '+' between each term.
    Handles complex numbers properly by not splitting on '+' within parentheses.
    
    Args:
        text (str): The text to split
        
    Returns:
        list: List of terms split by '+'
    """
    import re
    
    # Use regex to split on '+' that are not inside parentheses
    # This pattern matches '+' that are followed by whitespace and not inside parentheses
    pattern = r'\+\s*(?![^()]*\))'
    terms = re.split(pattern, text)
    
    # Clean up whitespace and filter out empty terms
    cleaned_terms = [term.strip() for term in terms if term.strip()]
    return cleaned_terms


def test_text_splitting():
    """
    Test function to demonstrate text splitting functionality.
    """
    # Example text similar to what you provided
    sample_text = """(-1761.1833868195836+0j) [] +
(0.0010415347124766948+0j) [X0 X1 X2] +
(-0.008882286586090955+0j) [X0 X1 X2 X3 X7 X15 X31 Y63 Y65 X67 X71 X79 X95] +
(8.135782795566303e-08+0j) [X0 X1 X2 X3 X7 X15 X31 Y63 Y67 X71 X79 X95] +
(1.2246777992850317e-07+0j) [X0 X1 X2 X3 X7 X15 X31 Y63 Z67 Y69 X71 X79 X95] +
(-1.8467282650794203e-05+0j) [X0 X1 X2 X3 X7 X15 X31 Y63 Y71 X79 X95]"""
    
    print("Original text:")
    print(sample_text)
    print("\n" + "="*50 + "\n")
    
    # Test simple splitting
    print("Simple splitting by '+':")
    terms = split_text_by_plus(sample_text)
    for i, term in enumerate(terms):
        print(f"Term {i+1}: {term}")
    
    print("\n" + "="*50 + "\n")
    
    # Test Pauli string parsing
    print("Pauli string parsing:")
    labels, coeffs = read_pauli_string_text(sample_text)
    for i, (label, coeff) in enumerate(zip(labels, coeffs)):
        if label:
            print(f"Term {i+1}: coeff={coeff}, pauli_string={label}")
        else:
            print(f"Term {i+1}: coeff={coeff}, pauli_string=[] (identity operator)")

#################################################################
#################################################################

def read_pauli_strings_hdf5(fname_hdf5: str, key: str):
    """
    Read the operator object from HDF5 at specified key to qiskit SparsePauliOp
    format.
    """

    with h5py.File(fname_hdf5, 'r', libver='latest') as f:
        labels, coeffs, num_qubits = read_pauli_string_text(f[key][()].decode("utf-8"))
    return labels, coeffs, num_qubits

#################################################################
#################################################################


if __name__ == "__main__":
    test_text_splitting()

#################################################################
#################################################################
