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
using inst_ptr = Instruction*;
using active_set_type = std::unordered_set<qubit_type>;
using layer_type = std::vector<inst_ptr>;

using Pauli = Instruction::Pauli;

/*
 * The actual function that does the converesion.
 * */
void run(IOUtility&, size_t active_set_capacity, size_t dag_inst_capacity);

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
Pauli pauli_multiply(Pauli, Pauli);

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

    compiler::pass::IOUtility io(istrm, ostrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
run(IOUtility& io, size_t active_set_capacity, size_t dag_inst_capacity)
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
    Pauli& p = inst->pauli_rotation_axes[idx];
    if (p == Pauli::I)
        return;

    if (is_s_like_instruction(clifford->type))
    {
        Pauli _p;
        if (inst->type == Instruction::Type::S)
            _p = Pauli::Z;
        else if (inst->type == Instruction::Type::SDG)
            _p = Pauli::nZ;
        else if (inst->type == Instruction::Type::SX)
            _p = Pauli::X;
        else
            _p = Pauli::nX;
        p = pauli_multiply(_p, p);
    }
    else if (clifford->type == Instruction::Type::X || clifford->type == Instruction::Type::Z)
    {
        const bool is_x = (inst->type == Instruction::Type::X);
        const Pauli _p = is_x ? Pauli::X : Pauli::Z;
        p = pauli_multiply(_p, pauli_multiply(_p, p));
    }
    else if (clifford->type == Instruction::Type::H)
    {
        // H = S * SX * S
        p = pauli_multiply(Pauli::Z, p);
        p = pauli_multiply(Pauli::X, p);
        p = pauli_multiply(Pauli::Z, p);
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

    const Pauli _p1 = Pauli::Z,
                _p2 = (clifford->type == Instruction::Type::CZ) ? Pauli::Z : Pauli::X;
    qubit_type q1 = clifford->qubits[0],
               q2 = clifford->qubits[1];
    
    auto q1_it = std::find(inst->q_begin(), inst->q_end(), q1);
    auto q2_it = std::find(inst->q_begin(), inst->q_end(), q2);

    // get original Paulis for `q1` and `q2`
    Pauli p1_orig{Pauli::I}, 
          p2_orig{Pauli::I};
    if (q1_it != inst->q_end())
        p1_orig = inst->pauli_rotation_axes[ std::distance(inst->q_begin(), q1_it) ];
    if (q2_it != inst->q_end())
        p2_orig = inst->pauli_rotation_axes[ std::distance(inst->q_begin(), q2_it) ];

    // compute new products based on rules:
    Pauli p1 = pauli_multiply(p1_orig, _p1);
    Pauli p2 = pauli_multiply(p2_orig, _p2);

    // update `inst`:
    auto f_update = [inst] (auto q_it, qubit_type q, Pauli p)
                    {
                        if (q_it == inst->q_end() && p != Pauli::I)
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
        if (inst->pauli_rotation_axes[i] == Pauli::I)
            inst->qubits = DELETE_QUBIT;
    auto q_it = std::remove(inst->qubits.begin(), inst->qubits.end(), DELETE_QUBIT);
    auto p_it = std::remove(inst->pauli_rotation_axes.begin(), inst->pauli_rotation_axes.end(), Pauli::I);
    inst->qubits.erase(q_it, inst->qubits.end());
    inst->pauli_rotation_axes.erase(p_it, inst->pauli_rotation_axes.end());

    // change the type of the instruction depending on the signs
    // of the Pauli axes
    int sgn{0};
    for (auto& p : inst->pauli_rotation_axes)
    {
        if (p == Pauli::nX || p == Pauli::nY || p == Pauli::nZ)
            sgn++;
        if (p == Pauli::nX)
            p = Pauli::X;
        else if (p == Pauli::nY)
            p = Pauli::Y;
        else if (p == Pauli::nZ)
            p = Pauli::Z;
    }

    if (sgn & 1)
        inst->type = Instruction::Type::PAULI_ROTATION_Q_PI_DAG;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Pauli
pauli_multiply(Pauli a, Pauli b)
{
    // Y = iXZ, X = iZY, Z = iYX
    if (a == Pauli::I)
        return b;
    if (b == Pauli::I)
        return a;
    if (a == b)
        return Pauli::I;

    if (a == Pauli::X)
    {
        if (b == Pauli::Y)  return Pauli::nZ;
        if (b == Pauli::nY) return Pauli::Z;
        if (b == Pauli::Z)  return Pauli::Y;
        if (b == Pauli::nZ) return Pauli::nY;
    }
    else if (a == Pauli::Y)
    {
        if (b == Pauli::X)  return Pauli::Z;
        if (b == Pauli::nX) return Pauli::nZ;
        if (b == Pauli::Z)  return Pauli::nX;
        if (b == Pauli::nZ) return Pauli::X;
    }
    else  // `a == Pauli::Z`
    {
        if (b == Pauli::X)  return Pauli::nY;
        if (b == Pauli::nX) return Pauli::Y;
        if (b == Pauli::Y)  return Pauli::X;
        if (b == Pauli::nY) return Pauli::nX;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
