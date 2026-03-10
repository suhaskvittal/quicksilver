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
using layer_type = std::vector<inst_ptr>;

using PAULI = INSTRUCTION::PAULI;

/*
 * The actual function that does the converesion.
 * */
void run(IO_UTILITY&, size_t active_set_capacity, size_t dag_inst_capacity);

/*
 * Propagates the given instruction through the Clifford. The
 * instruction is modified.
 * */
void propagate_non_clifford_through_clifford_1q(inst_ptr non_clifford, inst_ptr clifford);
void propagate_non_clifford_through_clifford_2q(inst_ptr non_clifford, inst_ptr clifford);

/*
 * This cleans the instruction by handling any negative Pauli terms in the Pauli product rotation
 * and converting them to positive terms. If there are any identity terms, the corresponding qubits
 * are also deleted.
 * */
void clean_pauli_rotation(inst_ptr);

/*
 * Reorders the Pauli-product rotations passed into the function to
 * maximize parallelism.
 * */
void reorder_instructions(std::vector<layer_type>&);

/*
 * Returns the product of the Paulis.
 * */
PAULI pauli_multiply(PAULI, PAULI);

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
    clifford_pbc_buffer.reserve(64);
    while (dag->inst_count() > 0 && !generic_strm_eof(istrm))
    {
        io.read_instructions(dag.get(), dag_inst_capacity);

        // get 
    }
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
propagate_non_clifford_through_clifford_1q(inst_ptr inst, inst_ptr clifford)
{
    assert(is_pauli_rotation(inst->type) && clifford->qubit_count() == 1);

    // check if qubit of `op` is in the support of `inst`
    qubit_type q = clifford->qubits[0];
    auto q_it = std::find(inst->q_begin(), inst->q_end(), q);
    if (q_it == inst->q_end())
        return;  // non-Clifford commutes thru Clifford
    size_t idx = std::distance(inst->q_begin(), q_it);

    // get `p` -- we may modify depending on what `clifford` is:
    PAULI& p = inst->pauli_rotation_axes[idx];
    if (p == PAULI::I)
        return;

    if (is_s_like_instruction(clifford->type))
    {
        PAULI _p;
        if (inst->type == INSTRUCTION::TYPE::S)
            _p = PAULI::Z;
        else if (inst->type == INSTRUCTION::TYPE::SDG)
            _p = PAULI::nZ;
        else if (inst->type == INSTRUCTION::TYPE::SX)
            _p = PAULI::X;
        else
            _p = PAULI::nX;
        p = pauli_multiply(_p, p);
    }
    else if (clifford->type == INSTRUCTION::TYPE::X || clifford->type == INSTRUCTION::TYPE::Z)
    {
        const bool is_x = (inst->type == INSTRUCTION::TYPE::X);
        const PAULI _p = is_x ? PAULI::X : PAULI::Z;
        p = pauli_multiply(_p, pauli_multiply(_p, p));
    }
    else if (clifford->type == INSTRUCTION::TYPE::H)
    {
        // H = S * SX * S
        p = pauli_multiply(PAULI::Z, p);
        p = pauli_multiply(PAULI::X, p);
        p = pauli_multiply(PAULI::Z, p);
    }
    else
    {
        std::cerr << "propagate_non_clifford_through_clifford_1q: unknown operation: " << *clifford << _die{};
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
propagate_non_clifford_through_clifford_2q(inst_ptr inst, inst_ptr clifford)
{
    assert(is_pauli_rotation(inst->type) && is_cx_like_instruction(clifford->type));

    const PAULI _p1 = PAULI::Z,
                _p2 = (clifford->type == INSTRUCTION::TYPE::CZ) ? PAULI::Z : PAULI::X;
    qubit_type q1 = clifford->qubits[0],
               q2 = clifford->qubits[1];
    
    auto q1_it = std::find(inst->q_begin(), inst->q_end(), q1);
    auto q2_it = std::find(inst->q_begin(), inst->q_end(), q2);

    // get original Paulis for `q1` and `q2`
    PAULI p1_orig{PAULI::I}, 
          p2_orig{PAULI::I};
    if (q1_it != inst->q_end())
        p1_orig = inst->pauli_rotation_axes[ std::distance(inst->q_begin(), q1_it) ];
    if (q2_it != inst->q_end())
        p2_orig = inst->pauli_rotation_axes[ std::distance(inst->q_begin(), q2_it) ];

    // compute new products based on rules:
    PAULI p1 = pauli_multiply(p1_orig, _p1);
    PAULI p2 = pauli_multiply(p2_orig, _p2);

    // update `inst`:
    auto f_update = [inst] (auto q_it, qubit_type q, PAULI p)
                    {
                        if (q_it == inst->q_end() && p != PAULI::I)
                        {
                            inst->qubits.push_back(q);
                            inst->pauli_rotation_axes.push_back(p);
                        }
                        else
                        {
                            size_t idx = std::distance(inst->q_begin(), q_it);
                            inst->pauli_rotation_axes[idx] = p;
                        }
                    };
    f_update(q1_it, q1, p1);
    f_update(q2_it, q2, p2);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
clean_pauli_rotation(inst_ptr inst)
{
    constexpr qubit_type DELETE_QUBIT{-1};

    // delete any identity gates
    //   -- mark qubits for deletion by setting them to `-1`
    assert(inst->qubits.size() == inst->pauli_rotation_axes.size());
    for (size_t i = 0; i < inst->pauli_rotation_axes.size(); i++)
        if (inst->pauli_rotation_axes[i] == PAULI::I)
            inst->qubits = DELETE_QUBIT;
    auto q_it = std::remove(inst->qubits.begin(), inst->qubits.end(), DELETE_QUBIT);
    auto p_it = std::remove(inst->pauli_rotation_axes.begin(), inst->pauli_rotation_axes.end(), PAULI::I);
    inst->qubits.erase(q_it, inst->qubits.end());
    inst->pauli_rotation_axes.erase(p_it, inst->pauli_rotation_axes.end());

    // change the type of the instruction depending on the signs
    // of the Pauli axes
    int sgn{0};
    for (auto& p : inst->pauli_rotation_axes)
    {
        if (p == PAULI::nX || p == PAULI::nY || p == PAULI::nZ)
            sgn++;
        if (p == PAULI::nX)
            p = PAULI::X;
        else if (p == PAULI::nY)
            p = PAULI::Y;
        else if (p == PAULI::nZ)
            p = PAULI::Z;
    }

    if (sgn & 1)
        inst->type = INSTRUCTION::TYPE::PAULI_ROTATION_Q_PI_DAG;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PAULI
pauli_multiply(PAULI a, PAULI b)
{
    // Y = iXZ, X = iZY, Z = iYX
    if (a == PAULI::I)
        return b;
    if (b == PAULI::I)
        return a;
    if (a == b)
        return PAULI::I;

    if (a == PAULI::X)
    {
        if (b == PAULI::Y)  return PAULI::nZ;
        if (b == PAULI::nY) return PAULI::Z;
        if (b == PAULI::Z)  return PAULI::Y;
        if (b == PAULI::nZ) return PAULI::nY;
    }
    else if (a == PAULI::Y)
    {
        if (b == PAULI::X)  return PAULI::Z;
        if (b == PAULI::nX) return PAULI::nZ;
        if (b == PAULI::Z)  return PAULI::nX;
        if (b == PAULI::nZ) return PAULI::X;
    }
    else  // `a == PAULI::Z`
    {
        if (b == PAULI::X)  return PAULI::nY;
        if (b == PAULI::nX) return PAULI::Y;
        if (b == PAULI::Y)  return PAULI::X;
        if (b == PAULI::nY) return PAULI::nX;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
