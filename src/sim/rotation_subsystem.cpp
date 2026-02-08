/*
 *  author: Suhas Vittal
 *  date:   21 January 2026
 * */

#include "sim/compute_subsystem.h"
#include "sim/rotation_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = ROTATION_SUBSYSTEM::inst_ptr;

/*
 * This is a utility function for searching for a request matching the
 * given instruction. We wrote a template function in case the underlying
 * data structure changes.
 * */
template <class ITER> ITER _find_match(ITER begin, ITER end, inst_ptr);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_SUBSYSTEM::ROTATION_SUBSYSTEM(double freq_khz,
                                        size_t capacity,
                                        COMPUTE_SUBSYSTEM* parent,
                                        double watermark)
    :COMPUTE_BASE("rotation_subsystem", 
                    freq_khz, 
                    capacity, 
                    parent->top_level_t_factories(), 
                    parent->memory_hierarchy()),
    watermark_(watermark),
    parent_(parent)
{
    requests_.reserve(capacity);
    free_qubits_.reserve(capacity);

    for (qubit_type i = 0; i < capacity; i++)
    {
        QUBIT* q = new QUBIT{i,-1};
        local_memory_->insert(q);
        free_qubits_.push_back(q);
    }
}

ROTATION_SUBSYSTEM::~ROTATION_SUBSYSTEM()
{
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_SUBSYSTEM::can_accept_rotation_request() const
{
    return !free_qubits_.empty();
}

bool
ROTATION_SUBSYSTEM::submit_rotation_request(inst_ptr inst, size_t layer, inst_ptr triggering_inst)
{
    assert(!is_rotation_pending(inst));
    assert(is_rotation_instruction(inst->type));

    if (free_qubits_.empty())
        return false;
    QUBIT* q = free_qubits_.back();
    free_qubits_.pop_back();

    rotation_request_entry e
    {
        .inst=inst,
        .dag_layer=layer,
        .allocated_qubit=q,
        .triggering_inst_info=triggering_inst->to_string()
    };
    requests_.push_back(e);
    pending_count_++;
    return true;
}

bool
ROTATION_SUBSYSTEM::is_rotation_pending(inst_ptr inst) const
{
    return _find_match(requests_.begin(), requests_.end(), inst) != requests_.end();
}

bool
ROTATION_SUBSYSTEM::find_and_delete_rotation_if_done(inst_ptr inst)
{
    auto it = _find_match(requests_.begin(), requests_.end(), inst);
    if (it != requests_.end() && it->done)
    {
        cycle_type idle_cycles = parent_->current_cycle() - it->cycle_done;
        // update stats:
        s_rotation_service_cycles += it->cycle_done - it->cycle_installed;
        s_rotation_idle_cycles += idle_cycles;
        
        if (idle_cycles > 1000)
        {
            std::cout << "inst: " << *inst 
                        << ", idle cycles = " << idle_cycles 
                        << ", trigger = " << it->triggering_inst_info << "\n";
        }

        s_rotations_completed++;
        free_qubits_.push_back(it->allocated_qubit);
        requests_.erase(it);
        return true;
    }
    else
    {
        return false;
    }
}

size_t
ROTATION_SUBSYSTEM::get_rotation_progress(inst_ptr inst) const
{
    auto it = _find_match(requests_.begin(), requests_.end(), inst);
    return (it != requests_.end()) ? it->inst->uops_retired() : 0;
}

void
ROTATION_SUBSYSTEM::invalidate_rotation(inst_ptr inst)
{
    auto it = _find_match(requests_.begin(), requests_.end(), inst);
    assert(it != requests_.end());
    free_qubits_.push_back(it->allocated_qubit);
    requests_.erase(it);
    pending_count_--;

    s_invalidates++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
ROTATION_SUBSYSTEM::operate()
{
    if (pending_count_ == 0)
        return 1;

    long progress{0};

    auto req_it = std::find_if(requests_.begin(), requests_.end(), [] (const auto& e) { return !e.done; });
    assert(req_it != rotation_assignment_map_.end());
    auto* inst = req_it->inst;
    auto* q = req_it->allocated_qubit;
    if (q->cycle_available > current_cycle())
        return 0;

    req_it->cycle_installed = std::min(parent_->current_cycle(), req_it->cycle_installed);

    const size_t num_teleports = (inst->rpc_is_critical || GL_RPC_RS_ALWAYS_USE_TELEPORTATION) 
                                    ? GL_T_GATE_TELEPORTATION_MAX
                                    : 0;

    auto result = do_rotation_gate_with_teleportation_while_predicate_holds(inst, {q}, num_teleports,
                    [this] (const inst_ptr x, const inst_ptr uop)
                    {
                        const size_t m = count_available_magic_states();
                        const size_t min_t_count = 1;
                        return x->rpc_is_critical || (m > min_t_count);
                    });
    progress += result.progress;
    if (result.progress > 0 && inst->uops_retired() == inst->uop_count())
    {
        // instruction is done -- reset uop progress for safety
        inst->reset_uops();
        req_it->done = true;
        req_it->cycle_done = parent_->current_cycle();
        pending_count_--;
    }

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

template <class ITER> ITER
_find_match(ITER begin, ITER end, inst_ptr inst)
{
    return std::find_if(begin, end, [inst] (const auto& e) { return e.inst == inst; });
}


} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
