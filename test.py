import qiskit
from qiskit.circuit import QuantumCircuit
from qiskit_aer import StatevectorSimulator

circ = QuantumCircuit(7,3)

for i in range(4):
    circ.x(i)

for _ in range(3):
    # XXXX
    circ.h(4)
    for i in range(4):
        circ.cx(4,i)
    circ.h(4)
    circ.measure(4,0)
    circ.reset(4)

    circ.barrier()

    # ZZ__
    for i in range(2):
        circ.cx(i,5)
    circ.measure(5,1)
    circ.reset(5)

    circ.barrier()

    # __ZZ
    for i in range(2,4):
        circ.cx(i,6)
    circ.measure(6,2)
    circ.reset(6)

    circ.barrier()

print(circ)

sim = StatevectorSimulator()
result = sim.run(circ).result()

for i in range(16):
    print(f'{bin(i)}:\t{result.get_statevector()[i]}')