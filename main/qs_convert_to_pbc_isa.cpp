/*
 *  author: Suhas Vittal
 *  date:   9 March 2026
 * */

#include "instruction.h"
#include "dag.h"
#include "compiler/pass/util.h"

#include <memory>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using dag_ptr = std::unique_ptr<DAG>;
using inst_ptr = INSTRUCTION*;
using active_set_type = std::unordered_set<qubit_type>;

/*
 * Basis of rotation
 * */
enum class PAULI { X, Y, Z };

/*
 * Generic clifford operator for PBC. If this is a multi-qubit
 * Clifford, then the first argument controls the second.
 * */
struct CLIFFORD_OPERATOR
{
    std::vector<qubit_type> qubits;
    std::vector<qubit_type> bases;

    size_t width() const { return qubits.size(); }
};

/*
 * The actual function that does the converesion.
 * */
void run(IO_UTILITY&, size_t active_set_capacity, size_t dag_inst_capacity);

/*
 * Returns the clifford operator for the given instruction.
 * */
CLIFFORD_OPERATOR convert_instruction_to_operator(inst_ptr);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string input_file,
                output_file;
    size_t active_set_capacity;
    size_t dag_inst_capacity;

    ARGPARSE()
        .required("input-file", "input binary file", input_file)
        .required("output-file", "output binary file", output_file)
        .optional("-a", "--active-set-capacity", "Size of the FTQC active set", active_set_capacity, 12)
        .optional("", "--dag-inst-capacity", "DAG instruction capacity", dag_inst_capacity, 8192)
        .parse(argc, argv);

    generic_strm_type istrm, ostrm;
    generic_strm_open(istrm, input_file, "r");
    generic_strm_open(ostrm, output_file, "w");

    compiler::pass::IO_UTILITY io(istrm, ostrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
run(IO_UTILITY& io, size_t active_set_capacity, size_t dag_inst_capacity)
{
    dag_ptr dag{new DAG{io.num_qubits}};

    active_set_type active_set;
    for (qubit_type i = 0; i < active_set_capacity)
        active_set.insert(i);

    std::vector<inst_ptr> clifford_pbc_buffer{};
    clifford_pbc_buffer.reserve(8);
    while (dag->inst_count() > 0 && !generic_strm_eof(istrm))
    {
        io.read_instructions(dag.get(), dag_inst_capacity);

        // get 
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
