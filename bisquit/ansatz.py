'''
    author: Suhas Vittal 
    date:   01 October 2025
'''

from hamlib_snippets import *
import qiskit

input_file = 'bisquit/chemistry/electronic/LiH.hdf5'

print(get_hdf5_keys(input_file))

op = read_qiskit_hdf5(input_file, 'ham_parity-4')
suzuki_trotter = qiskit.synthesis.SuzukiTrotter(order=2, reps=10)
time_evolution_op = qiskit.circuit.library.PauliEvolutionGate(op, 1.0, synthesis=suzuki_trotter)

print(op)

circuit = qiskit.QuantumCircuit(op.num_qubits)
circuit.append(time_evolution_op, range(op.num_qubits))
circuit = qiskit.compiler.transpile(circuit, basis_gates=["h", "x", "y", "z", "s", "sdg", "t", "tdg", "rx", "ry", "rz", "cx", "cz"])
print(circuit)
print(circuit.depth(), circuit.width(), circuit.count_ops())