/* author: Suhas Vittal date:   12 March 2026
 * */

#include "sim/driver/rdr.h"
#include "sim.h"

namespace sim
{
namespace driver
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = ROTATION_DIRECTED_RUNAHEAD::inst_ptr;

/*
 * Updates the estimated time to a rotation (second parameter). If
 * the given instruction is a rotation and the cost of the rotation
 * is within the current time to rotation for a qubit, then this
 * function returns true.
 * */
bool _update_time_to_rotation(const inst_ptr, std::vector<int>&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_DIRECTED_RUNAHEAD::dag_data::dag_data(const CLIENT* c)
    :dag(c->dag().get()),
    dependencies(c->num_qubits, {}),
    times(c->num_qubits, 0)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<inst_ptr>
ROTATION_DIRECTED_RUNAHEAD::dag_data::do_initial_traversal()
{
    constexpr size_t MAX_LAYER{1024};

    std::vector<inst_ptr> generated_requests;

    std::vector<int> time_to_first_rotation(dag->qubit_count, 0);
    std::vector<bool> done(dag->qubit_count, false);

    dag->for_each_instruction_in_layer_order(
            [this, &generated_requests, &time_to_first_rotation, &done] (auto* inst)
            {
                if (update_time_to_rotation(inst, time_to_first_rotation) && !done[inst->qubits[0]])
                {
                    generated_requests.push_back(inst);
                    done[inst->qubits[0]] = true;
                }
            }, 
            0,
            MAX_LAYER);
    return generated_requests;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

inst_ptr
ROTATION_DIRECTED_RUNAHEAD::dag_data::traverse_on_interrupt(inst_ptr src)
{
    // We want to find an instruction that has a decent amount of time to actually compute, since
    // `src` is at the head of the DAG.
    //
    // A nice factor is that we only need to care about the current dependency path when determining which
    // instruction to select. Here's why:
    //  (1) If this is the slowest (longest time to rotation), then we are ok.
    //  (2) If this is the fastest (shortest time to rotation), then we are beginning the rotation early
    //      anyway, so there is a bit of a head start.
    std::vector<int> time_to_rotation(dag->qubit_count, 0);
    auto [inst, __unused_layer] = dag->find_earliest_dependent_instruction_from_memoized_instruction_such_that(
                                            [&time_to_rotation] (inst_ptr x)
                                            {
                                                if (update_time_to_rotation(inst, time_to_rotation))
                                                    return true;
                                            }, 
                                            GL_RDR_START_LAYER, 
                                            GL_RDR_LOOKAHEAD_DEPTH);
    return inst->rdr_has_been_visited ? nullptr : inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

inst_ptr
ROTATION_DIRECTED_RUNAHEAD::dag_data::traverse_to_successor(inst_ptr src)
{
    auto [inst, __unused_layer] = dag->find_earliest_dependent_instruction_from_memoized_instruction_such_that(
                                            [] (inst_ptr x)
                                            {
                                                return is_rotation_instruction(x->type);
                                            }, 
                                            GL_RDR_START_LAYER, 
                                            GL_RDR_LOOKAHEAD_DEPTH);
    return inst->rdr_has_been_visited ? nullptr : inst;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_DIRECTED_RUNAHEAD::ROTATION_DIRECTED_RUNAHEAD(COMPUTE_SUBSYSTEM* cs, std::vector<CLIENT*> clients)
    :compute_subsystem_(cs)
{
    for (auto* c : clients)
    {
        dag_info_.push_back(dag_data{c});

        // for each DAG, do the initial traversal:
        c->warmup_dag(8192);
    }
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

namespace
{

bool
_update_time_to_rotation(const inst_ptr inst, std::vector<int>& time_to_rotation)
{
    int cost;
    if (is_rotation_instruction(inst->type))
        cost = std::count_if(inst->urotseq.begin(), inst->urotseq.end(), [] (auto t) { return is_t_like_instruction(t); });
    else if (is_toffoli_like_instruction(inst->type))
        cost = 13;
    else if (is_cx_like_instruction(inst->type))
        cost = 1;
    else if (is_t_like_instruction(inst->type))
        cost = 1;
    else
        cost = 0;

    bool good_rotation = is_rotation_instruction(inst->type)
                            && cost < time_to_rotation[inst->qubits[0]];

    int base_time{0};
    // get max time to first rotation for all arguments
    std::for_each(inst->q_begin(), inst->q_end(),
            [&base_time, &time_to_first_rotation] (auto q)
            {
                base_time = std::max(base_time, time_to_rotation[q]);
            });
    time_to_rotation[q] = base_time + cost;

    return good_rotation;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace driver
} // namespace sim
