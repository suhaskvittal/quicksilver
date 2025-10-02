import numpy as np

from qiskit_nature.units import DistanceUnit
from qiskit_nature.second_q.drivers import PySCFDriver

driver = PySCFDriver(
    atom="H .0 .0 .0; H .0 .0 0.735",
    unit=DistanceUnit.ANGSTROM,
    basis="sto3g"
)
problem = driver.run()

# setup qubit mapper:
from qiskit_nature.second_q.mappers import ParityMapper

mapper = ParityMapper(num_particles=problem.num_particles)

# setup ansatz
from qiskit_nature.second_q.circuit.library import HartreeFock, UCCSD

ansatz = UCCSD(
    problem.num_spatial_orbitals,
    problem.num_particles,
    mapper,
    initial_state=HartreeFock(
        problem.num_spatial_orbitals,
        problem.num_particles,
        mapper,
    )
)

from qiskit.compiler import transpile

uccsd_circuit = transpile(ansatz, basis_gates=["h", "x", "y", "z", "s", "sdg", "t", "tdg", "rx", "ry", "rz", "cx", "cz"])
print(uccsd_circuit)