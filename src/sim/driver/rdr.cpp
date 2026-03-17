/* author: Suhas Vittal date:   12 March 2026
 * */

#include "sim/driver/rotation_directed_runahead.h"
#include "sim.h"

namespace sim
{
namespace driver
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using request_type = ROTATION_DIRECTED_RUNAHEAD::request_type;

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_DIRECTED_RUNAHEAD::request_priority_comparator::operator()(const request_type* a, const request_type* b) const
{
    return a->dag_layer > b->dag_layer;
//  return a->inst->number > b->inst->number;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_DIRECTED_RUNAHEAD::ROTATION_DIRECTED_RUNAHEAD(COMPUTE_SUBSYSTEM* cs)
    :compute_subsystem_(cs)
{
    // get the qubit pointers from the compute subsystem
    std::copy_if(cs->dedicated_ancilla().begin(), cs->dedicated_ancilla().end(), std::back_inserter(free_qubits_),
            [] (const auto* q) { return q->client_id == RDR_CLIENT_ID; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
ROTATION_DIRECTED_RUNAHEAD::execute()
{
    long progress{0};

    // allocate free qubits:
    while (free_qubits_.size() > 0)
    {
        auto* e = pop_next_valid_pending_request();
        if (e == nullptr)
            break;
        e->pinned_qubit = free_qubits_.back();
        e->cycle_start = compute_subsystem_->current_cycle();
        s_requests++;
        free_qubits_.pop_back();
    }

    // select between the active requests
    std::vector<request_type*> active_requests;
    active_requests.reserve(GL_RDR_CAPACITY);
    for (const auto& [inst, e] : request_table_)
        if (e->pinned_qubit != nullptr)
            active_requests.push_back(e);
    std::sort(active_requests.begin(), active_requests.end(),
            [] (const auto* a, const auto* b) { return a->dag_layer < b->dag_layer; });

    for (auto* e : active_requests)
    {
        if (e->done)
            continue;
        inst_ptr inst = e->inst;
        inst_ptr uop = inst->current_uop();
        auto result = compute_subsystem_->execute_instruction(uop, {e->pinned_qubit});
        progress += result.progress;
        if (result.progress == 0)
            break;
        if (result.progress > 0 && inst->retire_current_uop())
        {
            inst->reset_uops();
            e->done = true;
            e->cycle_done = compute_subsystem_->current_cycle();
            s_requests_completed++;

            if (GL_RDR_ENABLE_PERFECT_COMPLETION_BUFFER)
            {
                free_qubits_.push_back(e->pinned_qubit);
                e->pinned_qubit = nullptr;
                completion_buffer_.insert(inst);
            }
        }
    }

    s_completion_buffer_occupancy_sum += completion_buffer_.size();
    s_completion_buffer_occupancy_ticks++;

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_DIRECTED_RUNAHEAD::submit_request(inst_ptr inst, size_t dag_layer, inst_ptr _triggering_inst)
{
    if (is_request_pending(inst))
        return false;

    request_type* e = new request_type
                                {
                                    .inst=inst,
                                    .dag_layer=dag_layer,
                                    .triggering_inst=INSTRUCTION(*_triggering_inst)
                                };
    pending_queue_.push(e);
    request_table_[inst] = e;
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

request_type*
ROTATION_DIRECTED_RUNAHEAD::find_request_and_only_return_if_complete(inst_ptr inst)
{
    auto it = request_table_.find(inst);
    if (it != request_table_.end() && it->second->done)
        return it->second;
    else
        return nullptr;
}

void
ROTATION_DIRECTED_RUNAHEAD::delete_request(inst_ptr inst)
{
    auto it = request_table_.find(inst);
    delete_request(it->second);
    request_table_.erase(it);
    completion_buffer_.erase(inst);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_DIRECTED_RUNAHEAD::is_request_pending(inst_ptr inst) const
{
    return request_table_.find(inst) != request_table_.end();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
ROTATION_DIRECTED_RUNAHEAD::get_request_progress(inst_ptr inst) const
{
    auto* e = request_table_.at(inst);
    return e->done ? inst->uop_count() : inst->uops_retired();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::mark_request_as_critical(inst_ptr inst)
{
    if (!is_request_pending(inst))
        return;
    auto* e = request_table_.at(inst);
    e->critical = true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::invalidate_request(inst_ptr inst)
{
    auto it = request_table_.find(inst);
    if (it == request_table_.end())
        return;

    auto* e = it->second;
    e->invalidated = true;
    request_table_.erase(it);
    if (e->pinned_qubit != nullptr)
    {
        s_requests_invalidated++;
        delete_request(e);
    }
    else
    {
        auto q_it = completion_buffer_.find(inst);
        if (q_it != completion_buffer_.end())
            completion_buffer_.erase(q_it);
        else
            s_invalidated_in_queue++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::delete_request(request_type* e)
{
    if (e->pinned_qubit != nullptr)
        free_qubits_.push_back(e->pinned_qubit);
    delete e;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

request_type*
ROTATION_DIRECTED_RUNAHEAD::pop_next_valid_pending_request()
{
    while (!pending_queue_.empty())
    {
        auto* e = pending_queue_.top();
        pending_queue_.pop();
        if (e->invalidated)
        {
            delete e;
            continue;
        }
        return e;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace driver
} // namespace sim
