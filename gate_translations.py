# author: Suhas Vittal
# 
# File that contains translations of various common gates used in quantum programs.
# Requires qiskit.

from qiskit.compiler import transpile
from qiskit.circuit import Parameter, QuantumCircuit
from qiskit.circuit.library import *

BASIS_GATES = [
    "h", "x", "y", "z", 
    "s", "sdg", "sx",
    "t", "tdg", 
    "rx", "rz", 
    "cx", "cz", 
    "measure"]

def translate(circ: QuantumCircuit) -> QuantumCircuit:
    circ = transpile(circ, basis_gates=BASIS_GATES)
    return circ

X = Parameter("X")
Y = Parameter("Y")
Z = Parameter("Z")
W = Parameter("W")
U = Parameter("U")

def show_gate_xla(gate, num_params: int, num_args: int, name: str):
    print(f'------------------------ {name} ------------------------')
    if num_params == 0:
        gate_impl = gate()
    elif num_params == 1:
        gate_impl = gate(X)
    elif num_params == 2: 
        gate_impl = gate(X, Y)
    elif num_params == 3:
        gate_impl = gate(X, Y, Z)
    elif num_params == 4:
        gate_impl = gate(X, Y, Z, W)
    elif num_params == 5:
        gate_impl = gate(X, Y, Z, W, U)
    else:
        raise ValueError(f"Invalid number of parameters for {name}: {num_params}")

    circ = QuantumCircuit(num_args)
    circ.append(gate_impl, range(num_args))
    circ = translate(circ)
    print(circ)

show_gate_xla(CSwapGate, 0, 3, "CSWAP")
show_gate_xla(U1Gate, 1, 1, "U1")
show_gate_xla(U2Gate, 2, 1, "U2")
show_gate_xla(U3Gate, 3, 1, "U3")
show_gate_xla(UGate, 3, 1, "U")

show_gate_xla(CUGate, 4, 2, "CU")
show_gate_xla(CU1Gate, 1, 2, "CU1")
show_gate_xla(CU3Gate, 3, 2, "CU3")

show_gate_xla(RYGate, 1, 1, "RY")
show_gate_xla(CRXGate, 1, 2, "CRX")
show_gate_xla(CRYGate, 1, 2, "CRY")
show_gate_xla(CRZGate, 1, 2, "CRZ")

show_gate_xla(PhaseGate, 1, 1, "Phase")
show_gate_xla(CPhaseGate, 1, 2, "CPhase")

show_gate_xla(RZZGate, 1, 2, "RZZ")
show_gate_xla(RZXGate, 1, 2, "RZX")
show_gate_xla(RYYGate, 1, 2, "RYY")
show_gate_xla(RXXGate, 1, 2, "RXX")

show_gate_xla(CCXGate, 0, 3, "CCX")
show_gate_xla(CCZGate, 0, 3, "CCZ")