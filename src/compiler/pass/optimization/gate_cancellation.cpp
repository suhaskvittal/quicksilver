/*
 *  author: Suhas Vittal
 *  date:   6 March 2026
 * */

#include "compiler/pass/optimization.h"
#include "compiler/pass/util.h"

namespace compiler
{
namespace pass
{
namespace optimization
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr size_t DAG_INST_CAPACITY{8192};

using dag_ptr = std::unique_ptr<DAG>;
using inst_ptr = DAG::inst_ptr;

bool _gates_cancel_out(inst_ptr, inst_ptr);
bool _gates_are_inverses(INSTRUCTION::TYPE, INSTRUCTION::TYPE);
bool _fpa_is_near_zero(const INSTRUCTION::fpa_type&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

result_type
gate_cancellation(generic_strm_type& ostrm, generic_strm_type& istrm)
{
    result_type out{};

    IO_UTILITY io(istrm, ostrm);

    // keep track of the last instruction for each qubit
    std::vector<inst_ptr> last_inst(io.num_qubits, nullptr);
    dag_ptr               dag{new DAG{io.num_qubits}};

    std::vector<inst_ptr> prev_front_layer{};
    while (dag->inst_count() > 0 || !generic_strm_eof(istrm))
    {
        io.read_instructions(dag.get(), DAG_INST_CAPACITY);

        // get all instructions in the front layer:
        auto front_layer = dag->get_front_layer();
        for (auto* inst : front_layer)
        {
            dag->remove_instruction_from_front_layer(inst);

            // check that all operands of `inst` had the same previous instruction:
            inst_ptr prev_inst = last_inst[inst->qubits[0]];
            if (prev_inst == nullptr || prev_inst->deletable) 
                continue;

            bool all_match = std::all_of(inst->q_begin()+1, inst->q_end(),
                                        [&last_inst, prev_inst] (qubit_type q) { return prev_inst == last_inst[q]; });
            if (all_match && _gates_cancel_out(prev_inst, inst))
            {
                prev_inst->deletable = true;
                inst->deletable = true;

                out.progress += 2;
                out.s_gates_removed += 2;
            }
        }

        // commit all non-deletable instructions in the previous layer
        for (auto* inst : prev_front_layer)
            if (!inst->deletable)
                io.write_instruction(inst);

        // update `prev_front_layer` and `last_inst`
        std::fill(last_inst.begin(), last_inst.end(), nullptr);
        for (auto* inst : front_layer)
            for (auto q_it = inst->q_begin(); q_it != inst->q_end(); q_it++)
                last_inst[*q_it] = inst;
        prev_front_layer = std::move(front_layer);
    }

    // final write (since `prev_front_layer` is not committed yet):
    for (auto* inst : prev_front_layer)
        if (!inst->deletable)
            io.write_instruction(inst);

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_gates_cancel_out(inst_ptr a, inst_ptr b)
{
    if (_gates_are_inverses(a->type, b->type))
        return true;

    // if `a` and `b` are rotations, check if their angles sum to 0
    if (is_rotation_instruction(a->type) && is_rotation_instruction(b->type) && (a->type == b->type))
    {
        auto s = fpa::add(a->angle, b->angle);
        if (_fpa_is_near_zero(s))
            return true;
    }

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_gates_are_inverses(INSTRUCTION::TYPE x, INSTRUCTION::TYPE y)
{
    switch (x)
    {
    // check for self inverses:
    case INSTRUCTION::TYPE::H:
    case INSTRUCTION::TYPE::X:
    case INSTRUCTION::TYPE::Y:
    case INSTRUCTION::TYPE::Z:
    case INSTRUCTION::TYPE::CX:
    case INSTRUCTION::TYPE::CZ:
    case INSTRUCTION::TYPE::CCX:
    case INSTRUCTION::TYPE::CCZ:
    case INSTRUCTION::TYPE::SWAP:
        return (x == y);

    // other unitaries:
    case INSTRUCTION::TYPE::S:
        return y == INSTRUCTION::TYPE::SDG;
    case INSTRUCTION::TYPE::SDG:
        return y == INSTRUCTION::TYPE::S;

    case INSTRUCTION::TYPE::SX:
        return y == INSTRUCTION::TYPE::SXDG;
    case INSTRUCTION::TYPE::SXDG:
        return y == INSTRUCTION::TYPE::SX;

    case INSTRUCTION::TYPE::T:
        return y == INSTRUCTION::TYPE::TDG;
    case INSTRUCTION::TYPE::TDG:
        return y == INSTRUCTION::TYPE::T;

    case INSTRUCTION::TYPE::TX:
        return y == INSTRUCTION::TYPE::TXDG;
    case INSTRUCTION::TYPE::TXDG:
        return y == INSTRUCTION::TYPE::TX;
    }

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_fpa_is_near_zero(const INSTRUCTION::fpa_type& x)
{
    constexpr int TOL_IDX = 8;  // so, assume any angle less than 2**(-W + TOL_IDX) is about 0.
                                // for example, W = 64 and TOL_IDX = 4, then ignore all angles
                                // less than 2**-60

    if (x.popcount() == 0)
        return true;

    /*
    // now the hard part: need to handle the case where the angle is very
    // close to 0.
    int msb = x.join_word_and_bit_idx(x.msb());
    if (msb < TOL_IDX)
        return true;

    // also check negative:
    msb = x.join_word_and_bit_idx(fpa::negate(x).msb());
    if (msb < TOL_IDX)
        return true;
    */

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace optimization
}  // namespace pass
}  // namespace compiler
