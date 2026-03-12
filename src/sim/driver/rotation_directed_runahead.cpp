/*
 *  author: Suhas Vittal
 *  date:   12 March 2026
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
    if (free_qubits_.size() >= GL_RDR_CAPACITY)
        return 1;

    long progress{0};

    // select between the active requests
    std::vector<request_type*> active_requests;
    active_requests.reserve(GL_RDR_CAPACITY);
    for (const auto& [inst, e] : request_table_)
        if (e->pinned_qubit != nullptr)
            active_requests.push_back(e);

    // decide between the active requests:
    request_type* best_request{nullptr};
    for (auto* e : active_requests)
    {
        if (e->done)
            continue;

        if (best_request == nullptr)
        {
            best_request = e;
        }
        else
        {
            auto* q = e->pinned_qubit;
            bool is_available = (q->cycle_available < compute_subsystem_->current_cycle());
            if (is_available && e->dag_layer < best_request->dag_layer)
                best_request = e;
        }
    }

    if (best_request == nullptr)
        return 1;

    // execute the next gate for this request:
    inst_ptr inst = best_request->inst;
    inst_ptr uop = inst->current_uop();
    auto result = compute_subsystem_->execute_instruction(uop, {best_request->pinned_qubit});
    progress += result.progress;
    if (result.progress > 0 && inst->retire_current_uop())
    {
        inst->reset_uops();
        best_request->done = true;
    }

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
    if (!free_qubits_.empty())
    {
        QUBIT* q = free_qubits_.back();
        free_qubits_.pop_back();
        e->pinned_qubit = q;
    }
    else
    {
        pending_queue_.push(e);
    }
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
    return inst->uops_retired();
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
        delete_request(e);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::delete_request(request_type* e)
{
    auto* q = e->pinned_qubit;
    request_type* f = pop_next_valid_pending_request();
    if (f == nullptr)
        free_qubits_.push_back(q);
    else
        f->pinned_qubit = q;
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
