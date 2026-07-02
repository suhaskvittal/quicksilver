/* 
 *  author: Suhas Vittal 
 *  date:   12 March 2026
 * */

#include "perf_sim/driver/rdr.h"
#include "perf_sim.h"

#include <algorithm>
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

constexpr ssize_t MIN_LOOKAHEAD{8};
constexpr ssize_t MAX_LOOKAHEAD{1024};
constexpr ssize_t LOOKAHEAD_DELTA{8};

using inst_ptr = RotationDirectedRunahead::inst_ptr;
using request_type = RotationDirectedRunahead::request_type;

template <class Iter>
Iter _find_request_for_instruction(Iter begin, Iter end, inst_ptr);

/*
 * Updates the estimated time to a rotation (second parameter). If
 * the given instruction is a rotation and the cost of the rotation
 * is within the current time to rotation for a qubit, then this
 * function returns true.
 * */
bool _update_time_to_rotation(const inst_ptr, 
                                std::vector<int>&, 
                                size_t code_distance,
                                double cov,
                                size_t layer);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

RotationDirectedRunahead::RotationDirectedRunahead(ComputeSubsystem* cs)
    :compute_subsystem_(cs),
    lookahead_depth_(GL_RDR_LOOKAHEAD_DEPTH)
{
    std::copy_if(cs->dedicated_ancilla().begin(), cs->dedicated_ancilla().end(), std::back_inserter(free_qubits_),
            [] (const auto* q) { return q->client_id == RDR_CLIENT_ID; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
RotationDirectedRunahead::operate()
{
    long progress{0};

    /* 1. check if any pinned qubits can be deallocated and placed into the completion buffer */
    
    if (completion_buffer_.size() < GL_RDR_COMPLETION_BUFFER_CAPACITY)
    {
        // searching for entry in completion buffer:
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
                completion_buffer_.insert({req_it->inst, *req_it});
                active_requests_.erase(req_it);
            }
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
    
    for (auto& r : active_requests_)
    {
        const size_t magic_states_avail = compute_subsystem_->count_available_magic_states();
        if (!r.interrupted && magic_states_avail <= 1)
            continue;
        if (r.done)
            continue;
        if (GL_RLTP_DEGREE == 0)
        {
            inst_ptr uop = r.inst->current_uop();
            auto result = compute_subsystem_->execute_instruction(uop, {r.pinned_qubit});
            progress += result.progress;
            if (result.progress > 0 && r.inst->retire_current_uop())
                retire_request(r);
        }
        else
        {
            size_t degree = std::min(static_cast<size_t>(GL_RLTP_DEGREE), magic_states_avail-1);
            if (r.interrupted)
                degree = GL_RLTP_DEGREE;
            auto result = compute_subsystem_->do_rotation_via_rltp(r.inst, r.pinned_qubit, GL_RLTP_DEGREE);
            progress += result.progress;
            if (result.progress > 0 && r.inst->uops_retired() == r.inst->uop_count())
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
RotationDirectedRunahead::do_runahead(Client* c, inst_ptr from)
{
    if (from->rdr.visited)
        return;

    const double cov = (s_requests_started < 100) ? 1.0 : coverage();
    const double tml = (s_requests_completed < 100) ? 1.0 : timeliness();

    if (!GL_RDR_FIXED_LOOKAHEAD)
    {
        const double fom = cov;
        if (fom < 0.5)        lookahead_depth_ = 16;
        else if (fom < 0.65)  lookahead_depth_ = 32;
        else if (fom < 0.8)   lookahead_depth_ = 64;
        else if (fom < 0.9)   lookahead_depth_ = 128;
        else                  lookahead_depth_ = 256;
    }

    size_t start_layer = 0,
           end_layer = GL_RDR_START_LAYER + lookahead_depth_;
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

#if defined(RDR_DEBUG)
        std::cout << "-------------------------------------------------- LD = " << lookahead_depth_ << "\n";
#endif

    size_t request_count{0};
#if defined(RDR_DEBUG)
    std::cout << ">>>>>>>>>>\n";
#endif
    c->dag()->for_each_instruction_in_layer_order(
                    [this, c, cov, &time_to_rotation, &request_count] (inst_ptr x, size_t layer)
                    {
                        cycle_type t = time_to_rotation[x->qubits[0]];

                        bool good = _update_time_to_rotation(x, 
                                                            time_to_rotation, 
                                                            compute_subsystem_->code_distance,
                                                            cov,
                                                            layer);
                        if (layer >= GL_RDR_START_LAYER && good && request_count < GL_RDR_DEGREE)
                        {
                            request_type req{ .client=c, 
                                                .inst=x, 
                                                .dag_layer=x->number, 
                                                .expected_start_cycle=current_cycle()+t,
                                                .install_cycle=current_cycle() };
                            enqueue_request(std::move(req));
                            request_count++;
#if defined(RDR_DEBUG)
                            std::cout << "installed " << *x << " @ layer = " << layer << "\n";
#endif
                        }
                    },
                    start_layer,
                    end_layer);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
RotationDirectedRunahead::is_done(inst_ptr inst)
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

RotationDirectedRunahead::ApplyMagicStateResult
RotationDirectedRunahead::apply_magic_state(inst_ptr inst, Qubit* q)
{
    const bool needs_correction = (GL_RNG() & 1) == 0;

    metadata_type meta;

    bool could_apply{false};
    auto b_it = completion_buffer_.find(inst);
    if (b_it != completion_buffer_.end())
    {
        if (compute_subsystem_->rdr_apply_rotation_magic_state_from_memory(q))
        {
            meta = b_it->second;
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
            meta = *a_it;
            free_qubits_.push_back(a_it->pinned_qubit);
            active_requests_.erase(a_it);
            could_apply = true;
        }
    }

    if (could_apply)
    {
        // update stats:
        const cycle_type rz_prep_time = meta.rdr_end_cycle - meta.rdr_start_cycle;
        const cycle_type rz_idle_time = current_cycle() - meta.rdr_end_cycle;
        update_on_consumption(meta.inst->uop_count(), rz_prep_time, rz_idle_time);

#if defined(RDR_DEBUG)
        std::cout << "Request consumed @ t = " << current_cycle()
                    << "\n\tinst = " << *meta.inst
                    << "\n\tinstall = " << meta.install_cycle
                    << "\n\texpected start = " << meta.expected_start_cycle
                    << "\n\tstart = " << meta.rdr_start_cycle
                    << "\n\tend = " << meta.rdr_end_cycle
                    << "\n\tinterrupted = " << meta.interrupted
                    << "\n";
#endif

        s_requests_used++;
        return needs_correction ? ApplyMagicStateResult::NEEDS_CORRECTION : ApplyMagicStateResult::GOOD;
    }
    else
    {
        return ApplyMagicStateResult::ROUTING_CONTENTION;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
RotationDirectedRunahead::interrupt_and_invalidate_if_necessary(inst_ptr inst)
{
    // first search for request amongst `active_requests_`
    auto a_it = _find_request_for_instruction(active_requests_.begin(), active_requests_.end(), inst);
    bool request_killed{false};
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
            request_killed = true;
        }
        else
        {
            if (!a_it->interrupted)
            {
                a_it->interrupted = true;
                s_requests_interrupted++;
            }
        }
    }
    else
    {
        auto r_it = _find_request_for_instruction(request_queue_.begin(), request_queue_.end(), inst);
        if (r_it != request_queue_.end())
        {
#if defined(RDR_DEBUG)
            std::cout << *r_it->inst << ": time in request queue " 
                        << (current_cycle() - r_it->install_cycle) 
                        << ", dag_layer = " << r_it->dag_layer
                        << "\n";
#endif

            request_queue_.erase(r_it);
            s_requests_invalidated_before_issue++;
        }
        request_killed = true;
    }

    // reduce lookahead depth since we are overfetching:
    return request_killed;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ssize_t
RotationDirectedRunahead::lookahead_depth() const
{
    return lookahead_depth_;
}

double
RotationDirectedRunahead::coverage() const
{
    return mean(s_requests_completed, s_requests_submitted);
}

double
RotationDirectedRunahead::timeliness() const
{
    return mean(s_requests_completed - s_requests_interrupted, s_requests_completed);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RotationDirectedRunahead::retire_request(request_type& r)
{
    r.inst->reset_uops();
    r.done = true;
    r.rdr_end_cycle = current_cycle();
    s_requests_completed++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RotationDirectedRunahead::allocate_free_qubit(Qubit* q)
{
    assert(request_queue_.size() > 0);

    request_type r = request_queue_.front();
    while (current_cycle() > r.expected_start_cycle)
    {
        request_queue_.pop_front();
        if (request_queue_.empty())
            return;
        r = request_queue_.front();
    }
    request_queue_.pop_front();
    r.pinned_qubit = q;
    r.rdr_start_cycle = current_cycle();
    active_requests_.push_back(r);
    s_requests_started++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RotationDirectedRunahead::enqueue_request(request_type&& r)
{
    request_queue_.push_back(r);
    r.inst->rdr.pending = true;
    s_requests_submitted++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type
RotationDirectedRunahead::current_cycle() const
{
    return compute_subsystem_->current_cycle();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
RotationDirectedRunahead::update_on_consumption(uint64_t uops, cycle_type rz_prep_time, cycle_type rz_idle_time)
{
    // update stats
    s_request_uop_sum += uops;
    s_request_completion_cycles_sum += rz_prep_time;
    s_post_completion_idle_time_sum += rz_idle_time;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Iter> Iter
_find_request_for_instruction(Iter begin, Iter end, inst_ptr inst)
{
    return std::find_if(begin, end, [inst] (const auto& r) { return r.inst == inst; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
_update_time_to_rotation(const inst_ptr inst, 
                        std::vector<int>& time_to_rotation, 
                        size_t d,
                        double cov,
                        // information only for debugging:
                        size_t layer)
{
    int cost;
    if (is_rotation_instruction(inst->type))
        cost = inst->uop_count() * (d+GL_REACTION_TIME);
    else if (is_toffoli_like_instruction(inst->type))
        cost = 7*(d+GL_REACTION_TIME) + 8*2*d;
    else
        cost = 2*d;

    if (is_rotation_instruction(inst->type) && sim::GL_RLTP_DEGREE > 0)
        cost = cost /  sim::GL_RLTP_DEGREE; 

    bool good_rotation = is_rotation_instruction(inst->type) && !inst->rdr.pending && !inst->rdr.visited;
    good_rotation &= (2.0*(1.0/cov)*cost < time_to_rotation[inst->qubits[0]]);
#if defined(RDR_DEBUG)
    if (is_rotation_instruction(inst->type) && !inst->rdr.pending && !inst->rdr.visited)
    {
        std::cout << "inst = " << *inst 
                    << ", cost = " << cost 
                    << ", t = " << time_to_rotation[inst->qubits[0]] 
                    << ", L = " << layer
                    << ", good = " << good_rotation
                    << "\n";
    }
#endif

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
