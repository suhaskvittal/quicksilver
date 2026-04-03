/* 
 *  author: Suhas Vittal 
 *  date:   12 March 2026
 * */

#include "sim/driver/rdr.h"
#include "sim.h"

#include <algorithm>
#include <random>

//#define RDR_DEBUG

namespace sim
{

extern std::mt19937_64 GL_RNG;

namespace driver
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr ssize_t MAX_LOOKAHEAD{1024};

using inst_ptr = ROTATION_DIRECTED_RUNAHEAD::inst_ptr;
using request_type = ROTATION_DIRECTED_RUNAHEAD::request_type;

template <class ITER>
ITER _find_request_for_instruction(ITER begin, ITER end, inst_ptr);

/*
 * Updates the estimated time to a rotation (second parameter). If
 * the given instruction is a rotation and the cost of the rotation
 * is within the current time to rotation for a qubit, then this
 * function returns true.
 * */
bool _update_time_to_rotation(const inst_ptr, std::vector<int>&, size_t layer, double coverage);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_DIRECTED_RUNAHEAD::ROTATION_DIRECTED_RUNAHEAD(COMPUTE_SUBSYSTEM* cs)
    :compute_subsystem_(cs),
    lookahead_depth_(GL_RDR_LOOKAHEAD_DEPTH)
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

    /* 1. check if any pinned qubits can be deallocated and placed into the completion buffer */
    
    if (completion_buffer_.size() < GL_RDR_COMPLETION_BUFFER_CAPACITY)
    {
        // searhcing for entry in completion buffer:
        auto req_it = std::find_if(active_requests_.begin(), active_requests_.end(),
                            [c=current_cycle()] (const auto& r)
                            { 
                                return r.done && r.pinned_qubit->cycle_available <= c;
                            });
        if (req_it != active_requests_.end())
        {
            auto* q = req_it->pinned_qubit;
            if (compute_subsystem_->rdr_simulate_store(q))
            {
                free_qubits_.push_back(q);
                completion_buffer_.insert(req_it->inst);
            }
            active_requests_.erase(req_it);
        }
    }

    /* 2. allocate any free qubits */

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

    /* 3. try to execute gates on each active request */

    std::sort(active_requests_.begin(), active_requests_.end(),
            [] (const auto& a, const auto& b)
            {
                if (a.interrupted == b.interrupted)
                    return a.dag_layer < b.dag_layer;
                else
                    return a.interrupted;
            });
    
    size_t magic_states_avail = compute_subsystem_->count_available_magic_states();

    for (auto& r : active_requests_)
    {
        if (magic_states_avail <= 1)
            break;
        if (r.done)
            continue;
        inst_ptr uop = r.inst->current_uop();
        auto result = compute_subsystem_->execute_instruction(uop, {r.pinned_qubit});
        progress += result.progress;
        if (result.progress > 0)
        {
            if (is_t_like_instruction(uop->type))
                magic_states_avail--;
            if (r.inst->retire_current_uop())
                retire_request(r);
        }
    }

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
    if (from->rdr_has_been_visited)
        return;

    const double cov = (s_requests_started < 100) ? 1.0 : mean(s_requests_completed, s_requests_started);
    size_t start_layer = 0,
           end_layer = GL_RDR_START_LAYER + lookahead_depth_;//GL_RDR_LOOKAHEAD_DEPTH;
    //
    // We want to find an instruction that has a decent amount of time to actually compute, since
    // `src` is at the head of the DAG.
    //
    // A nice factor is that we only need to care about the current dependency path when determining which
    // instruction to select. Here's why:
    //  (1) If this is the slowest (longest time to rotation), then we are ok.
    //  (2) If this is the fastest (shortest time to rotation), then we are beginning the rotation early
    //      anyway, so there is a bit of a head start.
    std::vector<int> time_to_rotation(c->num_qubits, 0);
    bool any_install{false};
    for (size_t i = 0; i < GL_RDR_DEGREE; i++)
    {
#if defined(RDR_DEBUG)
        std::cout << "--------------------------------------------------\n";
#endif
        auto [inst, layer] = c->dag()->find_earliest_dependent_instruction_from_memoized_instruction_such_that(
                                                [cov, &time_to_rotation] (inst_ptr x, size_t layer)
                                                {
                                                    bool good = _update_time_to_rotation(x, time_to_rotation, layer, cov);
                                                    return layer >= GL_RDR_START_LAYER
                                                            && good
                                                            && !x->rdr_is_pending
                                                            && !x->rdr_has_been_visited;
                                                },
                                                from,
                                                start_layer,
                                                end_layer);
        if (inst != nullptr)
        {
            request_type req{ .client=c, .inst=inst, .dag_layer=layer, .cycle_installed=current_cycle() };
            enqueue_request(std::move(req));
            any_install = true;
        }
        else
        {
            break;
        }

        start_layer = layer+1;
        end_layer = start_layer + lookahead_depth_;//GL_RDR_LOOKAHEAD_DEPTH;
    }

    if (!any_install && !GL_RDR_FIXED_LOOKAHEAD)
        lookahead_depth_ = std::clamp(lookahead_depth_+32, ssize_t{0}, ssize_t{MAX_LOOKAHEAD});
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

ROTATION_DIRECTED_RUNAHEAD::APPLY_MAGIC_STATE_RESULT
ROTATION_DIRECTED_RUNAHEAD::apply_magic_state(inst_ptr inst, QUBIT* q)
{
    const bool needs_correction = (GL_RNG() & 1) > 0;

    bool could_apply{false};
    auto b_it = completion_buffer_.find(inst);
    if (b_it != completion_buffer_.end())
    {
        if (compute_subsystem_->rdr_apply_rotation_magic_state_from_memory(q))
        {
            completion_buffer_.erase(b_it);
            could_apply = true;
        }
    }
    else
    {
        auto a_it = _find_request_for_instruction(active_requests_.begin(), active_requests_.end(), inst);
        assert(a_it != active_requests_.end() && a_it->done);
        if (compute_subsystem_->rdr_apply_rotation_magic_state_from_surface_code(q, a_it->pinned_qubit))
        {
            free_qubits_.push_back(a_it->pinned_qubit);
            active_requests_.erase(a_it);
            could_apply = true;
        }
    }

    if (could_apply)
    {
        s_requests_used++;
        return needs_correction ? APPLY_MAGIC_STATE_RESULT::NEEDS_CORRECTION : APPLY_MAGIC_STATE_RESULT::GOOD;
    }
    else
    {
        return APPLY_MAGIC_STATE_RESULT::ROUTING_CONTENTION;
    }
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
#if defined(RDR_DEBUG)
            std::cout << *r_it->inst << ": time in request queue " 
                        << (current_cycle() - r_it->cycle_installed) 
                        << ", dag_layer = " << r_it->dag_layer
                        << "\n";
#endif

            request_queue_.erase(r_it);
            s_requests_invalidated_before_issue++;

            // reduce lookahead depth since we are overfetching:
            if (!GL_RDR_FIXED_LOOKAHEAD)
                lookahead_depth_ = std::clamp(lookahead_depth_/2, ssize_t{0}, ssize_t{MAX_LOOKAHEAD});

            return true;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ssize_t
ROTATION_DIRECTED_RUNAHEAD::lookahead_depth() const
{
    return lookahead_depth_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_DIRECTED_RUNAHEAD::retire_request(request_type& r)
{
    r.inst->reset_uops();
    r.done = true;
    s_requests_completed++;
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

cycle_type
ROTATION_DIRECTED_RUNAHEAD::current_cycle() const
{
    return compute_subsystem_->current_cycle();
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

bool
_update_time_to_rotation(const inst_ptr inst, std::vector<int>& time_to_rotation, size_t layer, double cov)
{
    int cost;
    if (is_rotation_instruction(inst->type))
        cost = inst->uop_count();
    else if (is_toffoli_like_instruction(inst->type))
        cost = 13;
    else
        cost = 1;

    bool good_rotation = is_rotation_instruction(inst->type)
                            && !inst->rdr_is_pending
                            && !inst->rdr_has_been_visited;
    good_rotation &= (cost < time_to_rotation[inst->qubits[0]]);
#if defined(RDR_DEBUG)
    if (is_rotation_instruction(inst->type))
    {
        std::cout << "inst = " << *inst 
                    << ", cov = " << cov 
                    << ", cost = " << cost 
                    << ", t = " << time_to_rotation[inst->qubits[0]] 
                    << ", L = " << layer
                    << "\n";
    }
#endif

    if (is_rotation_instruction(inst->type) && sim::GL_RLTP_DEGREE > 0)
        cost = cost / (0.5 * sim::GL_RLTP_DEGREE); 
#if defined(RDR_DEBUG)
    if (is_rotation_instruction(inst->type) && sim::GL_RLTP_DEGREE > 0)
        std::cout << "\trltp adjusted cost: " << cost << "\n";
#endif

    int base_time{0};
    if (inst->qubit_count > 1)
    {
        // get max time to first rotation for all arguments
        std::for_each(inst->q_begin(), inst->q_end(),
                [&base_time, &time_to_rotation] (auto q) { base_time = std::max(base_time, time_to_rotation[q]); });
    }
    else
    {
        base_time = time_to_rotation[inst->qubits[0]];
    }
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
