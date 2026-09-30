#include "sim/driver/topols_rz.h"
#include "sim.h"

#include <algorithm>
#include <cassert>

namespace sim::driver
{

TopolsRz::TopolsRz(ComputeSubsystem* compute)
    :compute_(compute)
{
    for (auto* q : compute_->dedicated_ancilla())
        free_ancilla_.push_back(q);
}

bool
TopolsRz::start(Instruction* inst, Qubit* data)
{
    assert(is_rotation_instruction(inst->type));
    if (free_ancilla_.empty())
        return false;

    Qubit* ancilla = free_ancilla_.back();
    if (!compute_->topols_begin_rotation(data, ancilla))
        return false;
    free_ancilla_.pop_back();
    active_.push_back({inst, data, ancilla});
    return true;
}

bool
TopolsRz::contains(Instruction* inst) const
{
    return std::any_of(active_.begin(), active_.end(),
                       [inst](const auto& r) { return r.inst == inst; });
}

bool
TopolsRz::is_done(Instruction* inst) const
{
    auto it = std::find_if(active_.begin(), active_.end(),
                           [inst](const auto& r) { return r.inst == inst; });
    return it != active_.end() && it->readout_started
           && compute_->current_cycle() >= it->readout_ready;
}

void
TopolsRz::retire(Instruction* inst)
{
    auto it = std::find_if(active_.begin(), active_.end(),
                           [inst](const auto& r) { return r.inst == inst; });
    assert(it != active_.end() && is_done(inst));
    free_ancilla_.push_back(it->ancilla);
    active_.erase(it);
}

long
TopolsRz::operate()
{
    long progress{0};
    for (auto& r : active_)
    {
        if (r.readout_started || compute_->current_cycle() < r.ancilla->cycle_available)
            continue;

        Instruction* inst = r.inst;
        if (inst->uops_retired() < inst->uop_count())
        {
            auto result = compute_->execute_instruction(inst->current_uop(), {r.ancilla});
            if (result.progress == 0)
                continue;
            inst->retire_current_uop();
            progress += result.progress;
            continue;
        }

        // The ISA carries successively doubled corrective sequences. Each
        // injection attempt has a 50% chance of requiring the next one.
        // Traces without such sequences use deterministic injection here.
        if (!inst->corr_urotseq_array.empty() && (GL_RNG() & 1) == 0)
        {
            inst->urotseq = std::move(inst->corr_urotseq_array.front());
            inst->corr_urotseq_array.pop_front();
            inst->reset_uops();
            progress++;
            continue;
        }

        // X readout is one cycle; decoder feedforward is represented by the
        // reaction-time delay before dependent data instructions may start.
        r.readout_ready = compute_->current_cycle() + 1 + GL_REACTION_TIME;
        r.ancilla->cycle_available = r.readout_ready;
        r.data->cycle_available = r.readout_ready;
        r.readout_started = true;
        progress++;
    }
    return progress;
}

} // namespace sim::driver
