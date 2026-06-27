/*
 *  author: Suhas Vittal
 *  date:   6 March 2026
 * */

#include "compiler/pass/optimization.h"
#include "compiler/pass/util.h"
#include "compiler/program/rotation_manager.h"
#include "globals.h"

#include <cassert>
#include <iostream>

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
using fpa_type = Instruction::fpa_type;
using coalesce_output_type = std::array<std::optional<Instruction>, 2>;
using discrete_rotation_type = std::pair<Instruction::Type, Instruction::Type>;

/*
 * `LAST_INST` is the last instruction for a given qubit.
 * `PREV_FRONT_LAYER` is the previous front layer of the processed DAG.
 * */
static thread_local std::vector<inst_ptr> LAST_INST;
static thread_local std::vector<inst_ptr> PREV_FRONT_LAYER;

static thread_local uint64_t INST_COUNT{0};

/*
 * Subroutines
 * */
void _init(IOUtility&);
bool _loop(Result& out, DAG*, IOUtility&);
void _cleanup(IOUtility&);

/*
 * Merge the two instructions. The first instruction's data is modified,
 * and the second should be deleted.
 *
 * The output of this function is a 2D-array. The first entry will always
 * have a instruction, which indicates the new instruction after transformation
 * The second entry may not be null, which occurs for discrete rotations.
 * */
coalesce_output_type _coalesce(inst_ptr, inst_ptr prev);

/*
 * Helper functions:
 * */
bool _is_z_basis(Instruction::Type);
bool _is_coalescable(Instruction::Type, Instruction::Type);
bool _gates_cancel_out(inst_ptr, inst_ptr);
bool _gates_are_inverses(Instruction::Type, Instruction::Type);
bool _fpa_is_near_zero(const Instruction::fpa_type&);

/*
 * Converts S and T like gates to an integer representing how
 * far they rotate a qubit.
 * */
int8_t _discretize(Instruction::Type);

/*
 * Computes the instruction types for a given discrete value (0 thru 7).
 * */
discrete_rotation_type _reverse_discretization(int8_t, bool use_z_basis);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Result
cancel_and_coalesce(generic_strm_type& ostrm, generic_strm_type& istrm)
{
    std::cout << "\tcancel_and_coalesce: ";
    std::cout.flush();
        
    return run(ostrm, istrm, _init, _loop, _cleanup, DAG_INST_CAPACITY);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_init(IOUtility& io)
{
    LAST_INST.resize(io.num_qubits);
    std::fill(LAST_INST.begin(), LAST_INST.end(), nullptr);

    PREV_FRONT_LAYER.clear();
    PREV_FRONT_LAYER.reserve(io.num_qubits);

    INST_COUNT = 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_loop(Result& out, DAG* dag, IOUtility& io)
{
    const uint64_t prev_gates_removed{out.s_gates_removed};

    auto front_layer = dag->get_front_layer();
    for (auto* inst : front_layer)
    {
        dag->remove_instruction_from_front_layer(inst);

        // check that all operands of `inst` had the same previous instruction:
        inst_ptr prev_inst = LAST_INST[inst->qubits[0]];
        if (prev_inst == nullptr 
                || prev_inst->deletable 
                || prev_inst->qubit_count != inst->qubit_count)
        {
            continue;
        }

        const bool are_cancellable = _gates_are_inverses(inst->type, prev_inst->type)
                                        || (inst->type == prev_inst->type && is_rotation_instruction(inst->type));
        const bool are_coalescable = _is_coalescable(inst->type, prev_inst->type);
        if (!are_cancellable && !are_coalescable)
            continue;

        bool all_match = std::all_of(inst->q_begin()+1, inst->q_end(),
                                    [prev_inst] (qubit_type q) { return prev_inst == LAST_INST[q]; });

        if (all_match)
        {
            if (_gates_cancel_out(prev_inst, inst))
            {
                // first check for a gate cancellation opportunity:
                prev_inst->deletable = true;
                inst->deletable = true;
                out.s_gates_removed += 2;
            }
            else
            {
                auto [i1, i2] = _coalesce(inst, prev_inst);

                // replace data of `prev_inst` and `inst` completely:
                inst->~Instruction();
                new (inst) Instruction(*i1);
                if (i2.has_value()) 
                {
                    prev_inst->~Instruction();
                    new (prev_inst) Instruction(*i2);
                } else 
                {
                    prev_inst->deletable = true;
                }
                out.s_gates_removed += !i2.has_value() ? 1 : 0;
            }
        }
    }

    /* print progress */
    constexpr uint64_t PRINT_PROGRESS{1'000'000};
    uint64_t before_mod = INST_COUNT % PRINT_PROGRESS,
             after_mod = (INST_COUNT+front_layer.size()) % PRINT_PROGRESS;
    if (before_mod > after_mod)
        (std::cout << ".").flush();
    INST_COUNT += front_layer.size();

    // commit all non-deletable instructions in the previous layer
    for (auto* inst : PREV_FRONT_LAYER)
        if (!inst->deletable)
            io.write_instruction(inst);

    // update `PREV_FRONT_LAYER` and `LAST_INST`
    std::fill(LAST_INST.begin(), LAST_INST.end(), nullptr);
    for (auto* inst : front_layer)
        for (auto q_it = inst->q_begin(); q_it != inst->q_end(); q_it++)
            LAST_INST[*q_it] = inst;
    PREV_FRONT_LAYER = std::move(front_layer);

    out.progress += out.s_gates_removed - prev_gates_removed;

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_cleanup(IOUtility& io)
{
    for (auto* inst : PREV_FRONT_LAYER)
        if (!inst->deletable)
            io.write_instruction(inst);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

coalesce_output_type
_coalesce(inst_ptr curr, inst_ptr prev)
{
    constexpr double ANALOG_ANGLE_TRUNC_TOL{1e-12};

    assert(_is_coalescable(curr->type, prev->type));

    coalesce_output_type out{};

    // first check if `curr` and `prev` are rotations
    if (is_rotation_instruction(curr->type))
    {
        auto s = fpa::add(curr->angle, prev->angle);
        // `s` should never be a zero angle, since this
        // would've been caught earlier.
        assert(!_fpa_is_near_zero(s));

        // check if `s` corresponds to some discrete angle:
        double d = M_PI / convert_fpa_to_float(s);  // i.e., pi/4 --> 4
        if (std::abs(d - std::round(d)) < ANALOG_ANGLE_TRUNC_TOL)
        {
            int8_t a = static_cast<int8_t>(std::round(d));
            auto [t1, t2] = _reverse_discretization(a, _is_z_basis(curr->type));
            out[0].emplace(t1, curr->q_begin(), curr->q_end());
            if (t2 != Instruction::Type::NIL)
                out[1].emplace(t2, curr->q_begin(), curr->q_end());
        }
        else
        {
            auto urotseq = compiler::prog::rotation_manager_lookup(s);
            out[0].emplace(curr->type, curr->q_begin(), curr->q_end(), s, urotseq.begin(), urotseq.end());
        }
    }
    else
    {
        int8_t a = _discretize(curr->type),
               b = _discretize(prev->type);
        int8_t s = (a+b)&7;  // mod 8
        assert(s != 0);  // should be canceled earlier

        auto [t1, t2] = _reverse_discretization(s, _is_z_basis(curr->type));
        out[0].emplace(t1, curr->q_begin(), curr->q_end());
        if (t2 != Instruction::Type::NIL)
            out[1].emplace(t2, prev->q_begin(), prev->q_end());
    }
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_is_z_basis(Instruction::Type t)
{
    switch (t)
    {
    case Instruction::Type::Z:
    case Instruction::Type::S:
    case Instruction::Type::SDG:
    case Instruction::Type::T:
    case Instruction::Type::TDG:
    case Instruction::Type::RZ:
        return true;
    default:
        return false;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_is_coalescable(Instruction::Type t1, Instruction::Type t2)
{
    if (is_rotation_instruction(t1) && is_rotation_instruction(t2) && t1 == t2)
        return true;

    const bool t1_is_discrete = is_t_like_instruction(t1) || is_s_like_instruction(t1),
               t2_is_discrete = is_t_like_instruction(t2) || is_s_like_instruction(t2);
    if (t1_is_discrete && t2_is_discrete && _is_z_basis(t1) == _is_z_basis(t2))
        return true;

    return false;
}

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
_gates_are_inverses(Instruction::Type x, Instruction::Type y)
{
    switch (x)
    {
    // check for self inverses:
    case Instruction::Type::H:
    case Instruction::Type::X:
    case Instruction::Type::Y:
    case Instruction::Type::Z:
    case Instruction::Type::CX:
    case Instruction::Type::CZ:
    case Instruction::Type::CCX:
    case Instruction::Type::CCZ:
    case Instruction::Type::SWAP:
        return (x == y);

    // other unitaries:
    case Instruction::Type::S:
        return y == Instruction::Type::SDG;
    case Instruction::Type::SDG:
        return y == Instruction::Type::S;

    case Instruction::Type::SX:
        return y == Instruction::Type::SXDG;
    case Instruction::Type::SXDG:
        return y == Instruction::Type::SX;

    case Instruction::Type::T:
        return y == Instruction::Type::TDG;
    case Instruction::Type::TDG:
        return y == Instruction::Type::T;

    case Instruction::Type::TX:
        return y == Instruction::Type::TXDG;
    case Instruction::Type::TXDG:
        return y == Instruction::Type::TX;
    }

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_fpa_is_near_zero(const Instruction::fpa_type& x)
{
    constexpr int TOL_IDX = 8;  // so, assume any angle less than 2**(-W + TOL_IDX) is about 0.
                                // for example, W = 64 and TOL_IDX = 4, then ignore all angles
                                // less than 2**-60

    if (x.popcount() == 0)
        return true;

    // now the hard part: need to handle the case where the angle is very
    // close to 0.
    int msb = x.join_word_and_bit_idx(x.msb());
    if (msb < TOL_IDX)
        return true;

    // also check negative:
    msb = x.join_word_and_bit_idx(fpa::negate(x).msb());
    if (msb < TOL_IDX)
        return true;

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int8_t
_discretize(Instruction::Type t)
{
    switch (t)
    {
    case Instruction::Type::T:
    case Instruction::Type::TX:
        return 1;

    case Instruction::Type::S:
    case Instruction::Type::SX:
        return 2;

    case Instruction::Type::Z:
    case Instruction::Type::X:
        return 4;

    case Instruction::Type::SDG:
    case Instruction::Type::SXDG:
        return 6;

    case Instruction::Type::TDG:
    case Instruction::Type::TXDG:
        return 7;
    }

    std::cerr << "_discretize: received invalid instruction \"" << BASIS_GATES[static_cast<int>(t)]
                << " for discretization" << _die{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

discrete_rotation_type
_reverse_discretization(int8_t s, bool is_z)
{
    Instruction::Type t1{Instruction::Type::NIL}, t2{Instruction::Type::NIL};
    if (s == 1)
        t1 = is_z ? Instruction::Type::T : Instruction::Type::TX;
    else if (s == 2)
        t1 = is_z ? Instruction::Type::S : Instruction::Type::SX;
    else if (s >= 3 && s <= 5)
        t1 = is_z ? Instruction::Type::Z : Instruction::Type::X;
    else if (s == 6)
        t1 = is_z ? Instruction::Type::SDG : Instruction::Type::SXDG;
    else if (s == 7)
        t1 = is_z ? Instruction::Type::TDG : Instruction::Type::TXDG;

    // in some cases, `prev->type` must be set
    if (s == 3)
        t2 = is_z ? Instruction::Type::TDG : Instruction::Type::TXDG;
    else if (s == 5)
        t2 = is_z ? Instruction::Type::T : Instruction::Type::TX;
    assert(t1 != Instruction::Type::NIL);
    return std::make_pair(t1,t2);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace optimization
}  // namespace pass
}  // namespace compiler
