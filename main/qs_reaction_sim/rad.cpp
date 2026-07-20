/*
 *  author: Suhas Vittal
 *  date:   15 July 2026
 * */

#include "qs_reaction_sim/rad.h"

#include <algorithm>
#include <limits>

#define RAD_VERBOSE

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
            size_t _code_distance,
            DecoderTraits _slow_decoder_traits,
            double _fast_decoder_error_probability)
    :fast_decoder_error_probability(_fast_decoder_error_probability),
    slow_decoder_traits(_slow_decoder_traits),
    program_qubits(_program_qubits),
    decoder_count(_decoder_count),
    code_distance(_code_distance),
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

    // 1. If we are resolving the wrong path, then check if the syndrome history
    // only contains idle events. If so, then unset this flag and populate
    // `wrong_path_dag_`
    if (is_resolving_wrong_path()
        && retired_dag_->inst_count() == 0
        && syndrome_history_->only_contains_idles())
    {
        initialize_wrong_path();
    }

    // 2. Wrong-path instructions are retired by the Driver (in
    // `Driver::retire_instruction`, which routes `is_non_program_instruction`
    // retires out of `wrong_path_dag_` into `retired_dag_`). Once the wrong path
    // has fully drained, unblock and end wrong-path execution.
    if (is_executing_wrong_path() && wrong_path_dag_->inst_count() == 0)
    {
        wrong_path_blocked_qubits_.clear();
        wrong_path_state_ = WrongPathState::INVALID;
#if defined(RAD_VERBOSE)
        std::cout << "wrong path execution complete\n";
#endif
    }

    // 3. commit instructions and handle any detected errors on non-cliffords
    progress += handle_commit();

    // 4. decode syndrome history if able
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
    bool any_removed{false};
    do
    {
        any_removed = false;
        for (auto* inst : retired_dag_->get_front_layer())
        {
            bool ok_to_commit{false};
            if (is_t_like_instruction(inst->type))
            {
                ok_to_commit = inst->rx.verified;

                // handle decoding error: only commit if we
                // can start resolving wrong path
                if (inst->rx.erroneous)
                    ok_to_commit &= handle_decoding_error(inst);
            }
            else
            {
                ok_to_commit = true;
            }

            if (ok_to_commit)
            {
                if (is_resolving_wrong_path())
                    try_and_add_to_wrong_path(inst);
                commit_instruction(inst);
                progress++;
                any_removed = true;
            }
        }
    }
    while (any_removed);
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
    auto deallocated_ancilla = syndrome_history_->decode(c, max_windows*code_distance);
    for (auto a : deallocated_ancilla)
        anc_available_cycle_.erase(a);
    slow_decoder_next_available_cycle_ = c + slow_decoder_traits.pwd_reaction_time();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
RAD::handle_decoding_error(inst_ptr tainted_inst)
{
    // if we are executing the wrong path, then handle this case (need to stop wrong path execution)
    if (is_executing_wrong_path() || is_waiting_for_fast_decoder_before_resolution())
    {
        if (wrong_path_incomplete_.size() > 0)
            std::cerr << "RAD::handle_decoding_error: expected wrong_path_incomplete_ to be empty" << _die{};
        // move all pending instructions in `wrong_path_dag_` into `wrong_path_incomplete_`
        bool any_unretired{false};
        wrong_path_incomplete_.reserve(wrong_path_dag_->inst_count());
        wrong_path_dag_->for_each_instruction_in_layer_order(
                                    0,
                                    std::numeric_limits<size_t>::max(),
                                    [this, &any_unretired] (auto* inst, auto)
                                    {
                                        wrong_path_incomplete_.push_back(inst);
                                        any_unretired |= inst->rx.executed && !inst->rx.retireable;
                                    });
        if (any_unretired)
        {
            wrong_path_state_ = WrongPathState::TRANSIENT;
            wrong_path_incomplete_.clear();
            return false;
        }

#if defined(RAD_VERBOSE)
        std::cout << "got error while executing wrong path (inst left = " << wrong_path_dag_->inst_count() << ")\n"; 
#endif

        // then clear `wrong_path_dag_` (do not free instructions)
        wrong_path_dag_->clear(false);
    }

    // 1. set wrong path resolution signal
#if defined(RAD_VERBOSE)
    if (!is_resolving_wrong_path())
        std::cout << "starting wrong path resolution\n";;
#endif
    wrong_path_state_ = WrongPathState::RESOLVING;

    // 2. add all qubit arguments in `tainted_inst` to `wrong_path_blocked_qubits_`
    wrong_path_blocked_qubits_.insert(tainted_inst->q_begin(), tainted_inst->q_end());
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RAD::try_and_add_to_wrong_path(inst_ptr inst)
{
    const bool add_to_wp = std::any_of(inst->q_begin(), inst->q_end(),
                                    [this] (auto q) { return wrong_path_blocked_qubits_.count(q) > 0; });
    if (add_to_wp)
    {
        // create copy of instruction:
        wrong_path_recomp_.push_back( new Instruction(inst->type, inst->q_begin(), inst->q_end()) );
        wrong_path_uncomp_.push_back( new Instruction(_get_inverse_type(inst->type), inst->q_begin(), inst->q_end()) );
        wrong_path_blocked_qubits_.insert(inst->q_begin(), inst->q_end());
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RAD::initialize_wrong_path()
{
    wrong_path_state_ = WrongPathState::EXECUTING;

    std::vector<inst_ptr> wp_inst;
    wp_inst.reserve(wrong_path_uncomp_.size() + wrong_path_recomp_.size());
    std::move(wrong_path_uncomp_.rbegin(), wrong_path_uncomp_.rend(), std::back_inserter(wp_inst));
    std::move(wrong_path_recomp_.begin(), wrong_path_recomp_.end(), std::back_inserter(wp_inst));
    wrong_path_uncomp_.clear();
    wrong_path_recomp_.clear();

    // after adding the uncomputation + recomputation, add all of `wrong_path_incomplete_`
    std::move(wrong_path_incomplete_.begin(), wrong_path_incomplete_.end(), std::back_inserter(wp_inst));
    wrong_path_incomplete_.clear();

    for (auto* inst : wp_inst)
    {
        // signal that this is from the wrong path so we update stats
        inst->rx.is_non_program_instruction = true;
        wrong_path_dag_->add_instruction(inst);
    }

#if defined(RAD_VERBOSE)
    std::cout << "entered wrong path execution, inst in wrong path = " << wrong_path_dag_->inst_count() 
                << ", blocked = " << wrong_path_blocked_qubits_.size() 
                << "\n";
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs
