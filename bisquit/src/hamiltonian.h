/*
 *  author: Suhas Vittal
 *  date:   29 March 2026
 * */

#ifndef BISQUIT_HAMILTONIAN_h
#define BISQUIT_HAMILTONIAN_h

namespace ham
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `build_trotterization` writes QASM to `output_file` which
 * implements one trotter step of the Hamiltonian in `hdf5_file`
 * at the key `dataset_key`.
 *
 * The inputs to this function are data files from Hamlib:
 *  `https://portal.nersc.gov/cfs/m888/dcamps/hamlib/`
 *
 * `num_qubits` is the number of qubits in the Hamiltonian.
 * `trotter_steps` is used to divide by the coefficients of each term.
 * `coeff_sum` is used to compute the amount of time to simulate
 *      the Hamiltonian for (which, in term, affects the coefficients).
 *
 * If `qpe` is set, then the QASM writes a controlled Trotterization
 * where each RZ(*) in the algorithm is now a CRZ(*) controlled by
 * the output qubit.
 * */

void build_trotterization(std::string output_file, 
                            std::string hdf5_file,
                            std::string dataset_key,
                            size_t num_qubits,
                            size_t trotter_steps,
                            double coeff_sum,
                            bool qpe=false);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace ham

#endif // BISQUIT_HAMILTONIAN_h
