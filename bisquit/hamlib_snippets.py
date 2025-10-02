import numpy as np
import networkx as nx
import openfermion as of
import h5py
import re
from qiskit.quantum_info import SparsePauliOp


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


def read_graph_hdf5(fname_hdf5: str, key: str):
    """Read networkx graphs from appropriate hdf5 file.
    Returns a single graph, with specified key
    """
    with h5py.File(fname_hdf5, 'r') as f:
        G = nx.Graph(list(np.array(f[key])))

    return G


def read_gridpositions_hdf5(fname_hdf5: str, key: str):
    """Read grid positions, stored as attribute of each networkx graph from appropriate hdf5 file
    Returns grid positions of nodes associated with a single graph, with specified key
    """
    with h5py.File(fname_hdf5, 'r') as f:
        dataset = f[key]
        gridpositions_dict = dict(dataset.attrs.items()) 

    return gridpositions_dict


def read_openfermion_hdf5(fname_hdf5: str, key: str, optype=of.QubitOperator):
    """
    Read any openfermion operator object from HDF5 file at specified key.
    'optype' is the op class, can be of.QubitOperator or of.FermionOperator.
    """

    with h5py.File(fname_hdf5, 'r', libver='latest') as f:
        op = optype(f[key][()].decode("utf-8"))
    return op


def read_qiskit_hdf5(fname_hdf5: str, key: str):
    """
    Read the operator object from HDF5 at specified key to qiskit SparsePauliOp
    format.
    """
    def _generate_string(term):
        # change X0 Z3 to XIIZ
        indices = [
            (m.group(1), int(m.group(2))) 
            for m in re.finditer(r'([A-Z])(\d+)', term)
        ]
        return ''.join(
            [next((char for char, idx in indices if idx == i), 'I')
             for i in range(max(idx for _, idx in indices) + 1)]
        )

    def _append_ids(pstrings):
        # append Ids to strings
        return [p + 'I' * (max(map(len, pstrings)) - len(p)) for p in pstrings]

    with h5py.File(fname_hdf5, 'r', libver='latest') as f:
        pattern = r'([\d.]+) \[([^\]]+)\]'
        matches = re.findall(pattern, f[key][()].decode("utf-8"))

        labels = [_generate_string(m[1]) for m in matches]
        coeffs = [float(match[0]) for match in matches]
        op = SparsePauliOp(_append_ids(labels), coeffs)
    return op


def read_clause_list_hdf5(fname_hdf5: str, key: str):
    """read clause list from appropriate hdf5 file

    Returns clause list in DIMACS format
    """
    clause_list = []
    with h5py.File(fname_hdf5, 'r') as f:
        for clause in list(np.array(f[key])):
            clause_list.append([v for v in clause])

    return clause_list
