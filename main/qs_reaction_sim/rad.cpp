/*
 *  author: Suhas Vittal
 *  date:   15 July 2026
 * */

#include "qs_reaction_sim/rad.h"

#include <algorithm>
#include <limits>

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = RAD::inst_ptr;
using dag_ptr = RAD::dag_ptr;

/*
 * AI-GENERATED
 *
 * Returns the inverse gate type used to uncompute an instruction on the wrong
 * path. Cliffords like H/X/Y/Z/CX/CZ/SWAP (and the Toffolis) are self-inverse;
 * the phase gates pair with their daggers. Anything without a defined inverse
 * (e.g. measurements) maps to itself.
 * */
Instruction::Type
_get_inverse_type(Instruction::Type t)
{
    using T = Instruction::Type;
    switch (t)
    {
    case T::S:    return T::SDG;
    case T::SDG:  return T::S;
    case T::SX:   return T::SXDG;
    case T::SXDG: return T::SX;
    case T::T:    return T::TDG;
    case T::TDG:  return T::T;
    case T::TX:   return T::TXDG;
    case T::TXDG: return T::TX;
    default:      return t;
    }
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

RAD::RAD(size_t _program_qubits,
            size_t _decoder_count,
            DecoderTraits _slow_decoder_traits,
            double _fast_decoder_error_probability)
    :fast_decoder_error_probability(_fast_decoder_error_probability),
    slow_decoder_traits(_slow_decoder_traits),
    program_qubits(_program_qubits),
    decoder_count(_decoder_count),
    retired_dag_{new DAG(_program_qubits)},
    wrong_path_dag_{new DAG(_program_qubits)},
    syndrome_history_{new History(_program_qubits,
                                    _slow_decoder_traits.code_distance,
                                    HistoryRole::Verification,
                                    _fast_decoder_error_probability)},
    slow_decoder_next_available_cycle_(0)
{
    const double tp = slow_decoder_traits.pwd_throughput(decoder_count);
    if (tp < 1.0)
        std::cerr << "RAD: slow decoder has insufficient throughput: tp = " << tp << " < 1" << _die{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
RAD::operate(cycle_type c)
{
    long progress{0};

    // 1. if wrong path dag is non-empty, then try and retire
    // instructions from it. Note that we cannot commit as
    // we must account for errors on wrong path.
    if (wrong_path_dag_->inst_count() > 0)
    {
        progress += handle_wrong_path_retires();
    }
    else if (wrong_path_originators_.size() > 0)
    {
        // 1a. if wrong path is empty but we have wrong path originators, then
        //      we have completed the wrong path recently:
        for (auto* inst : wrong_path_originators_)
            inst->rx.waiting_on_wrong_path = false;
        wrong_path_originators_.clear();
    }

    // 2. commit instructions and handle any detected errors on non-cliffords
    progress += handle_commit();

    // 3. decode syndrome history if able
    if (c >= slow_decoder_next_available_cycle_)
        decode_history(c);

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RAD::register_instruction_in_history(inst_ptr inst, cycle_type current_cycle, cycle_type avail_cycle)
{
    auto result = syndrome_history_->add_events_for_instructions(inst, current_cycle);
    for (auto q : result.ancilla)
        anc_available_cycle_[q] = avail_cycle;
}

void
RAD::add_idle_cycle(cycle_type current_cycle, 
                    const std::vector<cycle_type>& program_qubit_avail_cycle)
{
    add_idle_cycle_array(syndrome_history_, current_cycle, program_qubit_avail_cycle);
    add_idle_cycle_map(syndrome_history_, current_cycle, anc_available_cycle_);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RAD::retire_instruction(inst_ptr inst)
{
    // So keep in mind that `Driver::retire_instruction()` will be calling
    // this function.  Our only job is to add the instruction to `retired_dag_`.
    //
    // We only care about the uops. So assert that `inst` is not a macro-op.
    if (is_rotation_instruction(inst->type) || is_toffoli_like_instruction(inst->type))
        std::cerr << "RAD::retire_instruction: received macro-op: \"" << *inst << "\"" << _die{};
    retired_dag_->add_instruction(inst);
}

void
RAD::commit_instruction(inst_ptr inst)
{
    // we can now delete `inst`. It should also be in the front layer
    // of `retired_dag_`
    retired_dag_->remove_instruction_from_front_layer(inst);
    delete inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
RAD::handle_commit()
{
    long progress{0};
    // remove all cliffords any non-erroneous non-cliffords from the front layer
    bool any_removed{false};
    do
    {
        auto inst_to_remove = retired_dag_->get_front_layer_if(
                                    [] (const auto* inst)
                                    {
                                        return !is_t_like_instruction(inst->type)
                                                || (inst->rx.verified && !inst->rx.erroneous && !inst->rx.waiting_on_wrong_path);
                                    });
        for (auto* inst : inst_to_remove)
            commit_instruction(inst);
        any_removed = (inst_to_remove.size() > 0);
        progress += inst_to_remove.size();
    }
    while (any_removed);

    // now handle any erroneous non-cliffords in the front layer:
    auto tainted_inst = retired_dag_->get_front_layer_if(
                                    [] (const auto* inst) { return inst->rx.verified && inst->rx.erroneous; });
    initialize_wrong_path_on_error(tainted_inst);
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
RAD::handle_wrong_path_retires()
{
    long progress{0};
    auto inst_to_retire = wrong_path_dag_->get_front_layer_if(
                                [] (const auto* inst ) { return inst->rx.retireable; });
    for (auto* inst : inst_to_retire)
    {
        wrong_path_dag_->remove_instruction_from_front_layer(inst);
        retire_instruction(inst);
    }
    progress += inst_to_retire.size();
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RAD::decode_history(cycle_type c)
{
    // The slow decoder verifies syndrome volume; any freed ancillas are its own
    // pool's concern (unlike the Driver, RAD has no `anc_available_cycle_`), so
    // the returned list is discarded.
    const size_t max_windows = 2*decoder_count;
    auto deallocated_ancilla = syndrome_history_->decode(c, max_windows);
    for (auto a : deallocated_ancilla)
        anc_available_cycle_.erase(a);
    slow_decoder_next_available_cycle_ = c + slow_decoder_traits.pwd_reaction_time();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RAD::initialize_wrong_path_on_error(std::vector<inst_ptr> tainted_inst)
{
    // Nothing flagged this cycle -- no wrong path to build.
    if (tainted_inst.empty())
        return;

    if (wrong_path_dag_->inst_count() > 0)
        std::cerr << "unimplemented: got new error while on wrong path" << _die{};

    // now that we have a list of affected t gates, compute the wrong path
    // we will treat this as a stack and push back instructions from the
    // stack onto `wrong_path_dag_`
    std::vector<inst_ptr> wrong_path(tainted_inst);
    wrong_path.reserve(256);

    blocked_set_type tainted_qubits;
    for (auto* inst : tainted_inst)
        tainted_qubits.insert(inst->q_begin(), inst->q_end());

    retired_dag_->for_each_instruction_in_layer_order(1, std::numeric_limits<size_t>::max(),
            [&] (auto* inst, size_t)
            {
                const bool is_tainted = std::any_of(inst->q_begin(), inst->q_end(),
                                            [&] (qubit_type q) { return tainted_qubits.count(q) > 0; });
                if (is_tainted)
                {
                    tainted_qubits.insert(inst->q_begin(), inst->q_end());
                    wrong_path.push_back(inst);
                }
            });

    // clear erroneous flag for all instructions on the wrong path:
    for (auto* inst : wrong_path)
        inst->rx.erroneous = false;
    // mark `tainted_inst` as waiting on wrong path:
    for (auto* inst : tainted_inst)
        inst->rx.waiting_on_wrong_path = true;

    // create two copies of each instruction in `wrong_path` and give those to `wrong_path_dag_`:
    //  --> `uncomp` inverts each instruction, applied in reverse order,
    //  --> `recovery` re-applies each instruction in the original order.
    std::vector<inst_ptr> uncomp(wrong_path.size()),
                            recovery(wrong_path.size());
    for (size_t i = 0; i < wrong_path.size(); i++)
    {
        const size_t j = wrong_path.size() - i - 1;
        auto* orig = wrong_path[i];
        uncomp[j] = new Instruction(_get_inverse_type(orig->type),
                                            orig->q_begin(),
                                            orig->q_end());
        recovery[i] = new Instruction(orig->type,
                                            orig->q_begin(),
                                            orig->q_end());

        // flag the uncomputation + recovery instructions as non-program instructions
        uncomp[j]->rx.is_non_program_instruction = true;
        recovery[i]->rx.is_non_program_instruction = true;
    }
    for (auto* inst : uncomp)
        wrong_path_dag_->add_instruction(inst);
    for (auto* inst : recovery)
        wrong_path_dag_->add_instruction(inst);
    wrong_path_originators_ = tainted_inst;
    wrong_path_blocked_qubits_ = tainted_qubits;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs
