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

void
ROTATION_SUBSYSTEM::invalidate_rotation(inst_ptr inst)
{
    auto it = rotation_assignment_map_.find(inst);
    assert(it != rotation_assignment_map_.end());
    free_qubits_.push_back(it->second);
    rotation_assignment_map_.erase(it);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
ROTATION_SUBSYSTEM::operate()
{
    bool any_pending_rotations = std::any_of(rotation_assignment_map_.begin(),
                                             rotation_assignment_map_.end(),
                                             [] (const auto& p) { return p.second != nullptr; });
    if (!any_pending_rotations)
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

        auto result = do_rotation_gate_with_teleportation_while_predicate_holds(inst, {q}, 
                        (inst->rpc_is_critical ? GL_T_GATE_TELEPORTATION_MAX : 0), // t_teleport_max
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
            free_qubits_.push_back(q);
            q = nullptr;
            assert(rotation_assignment_map_[inst] == nullptr);
        }
    }

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
