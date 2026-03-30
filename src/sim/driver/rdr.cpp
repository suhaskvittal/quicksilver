/* author: Suhas Vittal date:   12 March 2026
 * */

#include "sim/driver/rdr.h"
#include "sim.h"

#include <random>

namespace sim
{

extern std::mt19937_64 GL_RNG;

namespace driver
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = ROTATION_DIRECTED_RUNAHEAD::inst_ptr;
using request_type = ROTATION_DIRECTED_RUNAHEAD::request_type;

template <class ITER>
ITER _find_request_for_instruction(ITER begin, ITER end, inst_ptr);

/*
 * These are all functions used to determine which magic states
 * to produce.
 * */
std::optional<request_type> _runahead(CLIENT*, inst_ptr);

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

ROTATION_DIRECTED_RUNAHEAD::ROTATION_DIRECTED_RUNAHEAD(COMPUTE_SUBSYSTEM* cs)
    :compute_subsystem_(cs)
{
    std::copy_if(cs->dedicated_ancilla().begin(), cs->dedicated_ancilla().end(), std::back_inserter(free_qubits_),
            [] (const auto* q) { return q->client_id == RDR_CLIENT_ID; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
ROTATION_DIRECTED_RUNAHEAD::operate()
{
    long progress{0};

    /* 1. allocate any free qubits */

    if (!free_qubits_.empty())
    {
        std::sort(request_queue_.begin(), request_queue_.end(),
                [] (const auto& a, const auto& b) { return a.dag_layer < b.dag_layer; });

        while (!free_qubits_.empty() && !request_queue_.empty())
        {
            // pop a free qubit off the stack and pin it to a request
            auto* q = free_qubits_.back();
            free_qubits_.pop_back();
            allocate_free_qubit(q);
        }
    }

    /* 2. try to execute gates on each active request */

    std::sort(active_requests_.begin(), active_requests_.end(),
            [] (const auto& a, const auto& b)
            {
                if (a.interrupted == b.interrupted)
                    return a.dag_layer < b.dag_layer;
                else
                    return a.interrupted;
            });
    for (auto& r : active_requests_)
    {
        if (r.done)
            continue;
        inst_ptr uop = r.inst->current_uop();
        auto result = compute_subsystem_->execute_instruction(uop, {r.pinned_qubit});
        progress += result.progress;
        if (result.progress == 0)
            break;
        if (r.inst->retire_current_uop())
            retire_request(r);
    }

    /* 3. remove any inactive requests (done and has no pinned qubit) */

    auto req_it = std::remove_if(active_requests_.begin(), active_requests_.end(), 
                                [] (const auto& r) { return r.done && r.pinned_qubit == nullptr; });
    active_requests_.erase(req_it, active_requests_.end());

    /* 4. update cycle-level stats */

    s_completion_buffer_occu_sum += completion_buffer_.size();
    s_completion_buffer_occu_ticks++;

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::do_runahead(CLIENT* c, inst_ptr from)
{
    for (size_t i = 0; i < GL_RDR_DEGREE; i++)
    {
        auto r = _runahead(c, from);
        if (r.has_value())
            enqueue_request(std::move(*r));
        else
            break;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_DIRECTED_RUNAHEAD::is_done(inst_ptr inst)
{
    auto b_it = completion_buffer_.find(inst);
    if (b_it != completion_buffer_.end())
        return true;
    
    // search among the `active_requests_`
    auto a_it = _find_request_for_instruction(active_requests_.begin(), active_requests_.end(), inst);
    if (a_it != active_requests_.end() && a_it->done)
        return true;

    return false;
}

bool
ROTATION_DIRECTED_RUNAHEAD::apply_magic_state(inst_ptr inst, QUBIT* q)
{
    const bool needs_correction = (GL_RNG() & 1) > 0;
    const auto d = compute_subsystem_->code_distance;

    s_requests_used++;

    auto b_it = completion_buffer_.find(inst);
    if (b_it != completion_buffer_.end())
    {
        completion_buffer_.erase(b_it);
        q->cycle_available = compute_subsystem_->current_cycle() + d + 2;
    }
    else
    {
        auto a_it = _find_request_for_instruction(active_requests_.begin(), active_requests_.end(), inst);
        assert(a_it != active_requests_.end() && a_it->done);
        cycle_type end_cycle = compute_subsystem_->current_cycle() + d;
        q->cycle_available = end_cycle;
        a_it->pinned_qubit->cycle_available = end_cycle;

        free_qubits_.push_back(a_it->pinned_qubit);
        active_requests_.erase(a_it);
    }
    return needs_correction;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_DIRECTED_RUNAHEAD::interrupt_and_invalidate_if_necessary(inst_ptr inst)
{
    // first search for request amongst `active_requests_`
    auto a_it = _find_request_for_instruction(active_requests_.begin(), active_requests_.end(), inst);
    if (a_it != active_requests_.end())
    {
        assert(!a_it->done);

        const size_t progress = a_it->inst->uops_retired();
        if (progress < 0.25*a_it->inst->uop_count())
        {
            // kill this request:
            free_qubits_.push_back(a_it->pinned_qubit);
            s_requests_invalidated++;
            active_requests_.erase(a_it);
            return true;
        }
        else
        {
            if (!a_it->interrupted)
            {
                a_it->interrupted = true;
                s_requests_interrupted++;
            }
            return false;
        }
    }
    else
    {
        auto r_it = _find_request_for_instruction(request_queue_.begin(), request_queue_.end(), inst);
        if (r_it != request_queue_.end())
        {
            request_queue_.erase(r_it);
            s_requests_invalidated_before_issue++;
            return true;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::retire_request(request_type& r)
{
    r.inst->reset_uops();
    r.done = true;
    s_requests_completed++;

    if (GL_RDR_ENABLE_PERFECT_COMPLETION_BUFFER)
    {
        free_qubits_.push_back(r.pinned_qubit);
        r.pinned_qubit = nullptr;
        completion_buffer_.insert(r.inst);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::allocate_free_qubit(QUBIT* q)
{
    assert(request_queue_.size() > 0);
    request_type r = std::move(request_queue_.back());
    request_queue_.pop_back();
    r.pinned_qubit = q;
    active_requests_.push_back(r);
    s_requests_started++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::enqueue_request(request_type&& r)
{
    request_queue_.push_back(r);
    r.inst->rdr_is_pending = true;
    s_requests_submitted++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER> ITER
_find_request_for_instruction(ITER begin, ITER end, inst_ptr inst)
{
    return std::find_if(begin, end, [inst] (const auto& r) { return r.inst == inst; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::optional<request_type>
_runahead(CLIENT* c, inst_ptr from)
{
    // We want to find an instruction that has a decent amount of time to actually compute, since
    // `src` is at the head of the DAG.
    //
    // A nice factor is that we only need to care about the current dependency path when determining which
    // instruction to select. Here's why:
    //  (1) If this is the slowest (longest time to rotation), then we are ok.
    //  (2) If this is the fastest (shortest time to rotation), then we are beginning the rotation early
    //      anyway, so there is a bit of a head start.
    std::vector<int> time_to_rotation(c->num_qubits, 0);
    auto [inst, layer] = c->dag()->find_earliest_dependent_instruction_from_memoized_instruction_such_that(
                                            [&time_to_rotation] (inst_ptr x)
                                            {
                                                return _update_time_to_rotation(x, time_to_rotation) 
                                                        && !x->rdr_is_pending
                                                        && !x->rdr_has_been_visited;
                                            },
                                            from,
                                            GL_RDR_START_LAYER, 
                                            GL_RDR_LOOKAHEAD_DEPTH);
    if (inst == nullptr)
        return std::nullopt;
    else
        return std::make_optional(request_type{ .client=c, .inst=inst, .dag_layer=layer });
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

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
            [&base_time, &time_to_rotation] (auto q) { base_time = std::max(base_time, time_to_rotation[q]); });
    std::for_each(inst->q_begin(), inst->q_end(),
            [t=base_time+cost, &time_to_rotation] (auto q) { time_to_rotation[q] = t; });

    return good_rotation;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace driver
} // namespace sim
