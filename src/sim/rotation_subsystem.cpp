/*
 *  author: Suhas Vittal
 *  date:   21 January 2026
 * */

#include "sim/rotation_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_SUBSYSTEM::ROTATION_SUBSYSTEM(double freq_khz,
                                        size_t capacity,
                                        std::vector<T_FACTORY_BASE*> top_level_t_factories,
                                        MEMORY_SUBSYSTEM* m,
                                        double watermark)
    :COMPUTE_BASE("rotation_subsystem", freq_khz, capacity, top_level_t_factories, m),
    watermark_(watermark)
{
    rotation_assignment_map_.reserve(capacity);
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
ROTATION_SUBSYSTEM::submit_rotation_request(inst_ptr inst)
{
    assert(!is_rotation_pending(inst));

    if (free_qubits_.empty())
        return false;
    QUBIT* q = free_qubits_.back();
    free_qubits_.pop_back();
    rotation_assignment_map_[inst] = q;
    return true;
}

bool
ROTATION_SUBSYSTEM::is_rotation_pending(inst_ptr inst) const
{
    return rotation_assignment_map_.find(inst) != rotation_assignment_map_.end();
}

bool
ROTATION_SUBSYSTEM::find_and_delete_rotation_if_done(inst_ptr inst)
{
    auto it = rotation_assignment_map_.find(inst);
    if (it != rotation_assignment_map_.end() && it->second == nullptr)
    {
        rotation_assignment_map_.erase(it);
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
    auto it = rotation_assignment_map_.find(inst);
    if (it != rotation_assignment_map_.end())
        return it->first->uops_retired();
    else
        return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
ROTATION_SUBSYSTEM::operate()
{
    if (rotation_assignment_map_.empty())
        return 1;

    total_magic_state_count_at_cycle_start_ = count_available_magic_states();

    /* Make progress on any pending rotations */
    long progress{0};
    for (auto& [inst, q] : rotation_assignment_map_)
    {
        if (q == nullptr)
            continue;
        if (q->cycle_available > current_cycle())
            continue;
        auto result = execute_instruction(inst->current_uop(), {q});
        if (result.progress)
        {
            progress += result.progress;
            if (inst->retire_current_uop())
            {
                // instruction is done -- reset uop progress for safety
                inst->reset_uops();
                free_qubits_.push_back(q);
                q = nullptr;
                assert(rotation_assignment_map_[inst] == nullptr);
            }
        }
    }

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_SUBSYSTEM::execute_result_type
ROTATION_SUBSYSTEM::do_t_like_gate(inst_ptr inst, QUBIT* q)
{
    const size_t m = count_available_magic_states();
    if (!inst->rpc_is_critical && (m < watermark_*total_magic_state_count_at_cycle_start_ || m <= 1))
        return execute_result_type{};
    else
        return COMPUTE_BASE::do_t_like_gate(inst, q);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
