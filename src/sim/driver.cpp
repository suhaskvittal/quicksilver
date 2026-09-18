/*
 *  author: Suhas Vittal
 *  date:   12 March 2026
 * */

#include "sim/configuration/resource_estimation.h"
#include "sim/driver.h"
#include "sim/metrics.h"

#include <algorithm>
#include <cassert>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = ComputeSubsystem::inst_ptr;

constexpr size_t STALL_MONITOR_MAX_RANGES{2'048};

/*
 * Returns true if the client is complete.
 * */
bool _client_is_done(const Client*, uint64_t simulation_instructions);

/*
 * Returns true if either the given instruction, or its current uop,
 * passes `is_t_like_instruction()`
 * */
bool _current_uop_is_t_like(inst_ptr);

/*
 * Assigns the second parameter to first iff the first parameter does not
 * have a value.
 * */
template <class T>
void _assign_if_empty(std::optional<T>&, T);

template <class T, class F>
void _assign_if_empty(std::optional<T>&, const F&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Driver::Driver(std::vector<std::string> client_trace_files,
                size_t _concurrent_clients,
                uint64_t _simulation_instructions,
                ComputeSubsystem* compute_subsystem,
                BulkProducer* t_producer,
                BulkProducer* epr_producer,
                std::vector<MemoryLevel*> memory_subsystem)
    :Operable("sim", compute_subsystem->freq_khz),
    concurrent_clients(_concurrent_clients),
    total_clients(client_trace_files.size()),
    simulation_instructions(_simulation_instructions),
    clients_(total_clients),
    active_clients_(concurrent_clients),
    inactive_clients_(total_clients - concurrent_clients),
    client_context_table_(total_clients),
    compute_subsystem_(compute_subsystem),
    t_producer_(t_producer),
    epr_producer_(epr_producer),
    memory_subsystem_(memory_subsystem),
    stall_monitor_(STALL_MONITOR_MAX_RANGES)
{
    /* initialize all clients */

    assert(total_clients >= concurrent_clients);
    for (client_id_type i = 0; i < client_trace_files.size(); i++)
        clients_[i] = new Client(client_trace_files[i], i);
    std::copy(clients_.begin(), clients_.begin() + concurrent_clients, active_clients_.begin());
    std::copy(clients_.begin() + concurrent_clients, clients_.end(), inactive_clients_.begin());

    std::vector<Qubit*> program_qubits;
    for (auto* c : clients_)
        std::copy(c->qubits().begin(), c->qubits().end(), std::back_inserter(program_qubits));
    compute_subsystem_->initialize_qubits(program_qubits);

    if (GL_RDR_ENABLED)
        rdr_ = new driver::RotationDirectedRunahead(compute_subsystem_);
}

Driver::~Driver()
{
    for (auto* c : clients_)
        delete c;
    if (GL_RDR_ENABLED)
        delete rdr_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::print_progress(std::ostream& out) const
{
    std::cout << "cycle " << current_cycle() << " -------------------------------------------------------------";

    out << "\nwalltime = " << sim::walltime_s() << "s"
        << "\n";

    for (auto* c : clients_)
    {
        auto active_it = std::find(active_clients_.begin(), active_clients_.end(), c);
        bool is_active = (active_it != active_clients_.end());

        if (is_active)
            out << " * client " << static_cast<int>(c->id);
        else
            out << "   client " << static_cast<int>(c->id);

        double ipc = stats::ipc(c->s_unrolled_inst_done, current_cycle());
        double ipdc = stats::ipdc(c->s_unrolled_inst_done, current_cycle(), compute_subsystem_->code_distance);
        double kips = stats::kips(c->s_unrolled_inst_done, current_cycle(), freq_khz);

        out << "\n\tinstructions completed = " << c->s_unrolled_inst_done
                << "\n\tIPC = " << ipc
                << "\n\tIPdC = " << ipdc
                << "\n\tKIPS = " << kips;

        if (GL_RDR_ENABLED)
        {
            out << "\n\tRDR coverage = " << rdr_->coverage()
                << "\n\tRDR timeliness = " << rdr_->timeliness()
                << "\n\tRDR lookahead = " << rdr_->lookahead_depth();
        }

        out << "\n";
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::print_deadlock_info(std::ostream& out) const
{
    t_producer_->print_deadlock_info(out);
    if (epr_producer_ != nullptr)
        epr_producer_->print_deadlock_info(out);

    out << "local memory contents:";
    for (auto* q : compute_subsystem_->local_memory())
        out << " " << *q;
    out << "\n";

    for (auto* c : active_clients_)
    {
        out << "Client " << static_cast<int>(c->id)
            << " dag_inst_count=" << c->dag()->inst_count()
            << " eof=" << c->eof()
            << " s_inst_read=" << c->s_inst_read
            << " s_unrolled_inst_done=" << c->s_unrolled_inst_done
            << " front layer:\n";
        for (const auto* inst : c->dag()->get_front_layer())
        {
            out << "\t" << *inst;
            if (inst->uop_count() > 0)
            {
                out << "\tcurrent uop = " << *inst->current_uop() << ", " << inst->uops_retired()
                            << " of " << inst->uop_count();
            }

            out << "\tcycle ready (current cycle = " << current_cycle() << "):";
            std::for_each(inst->q_begin(), inst->q_end(),
                        [this, c, &out] (auto q_id)
                        {
                            Qubit* q = c->qubits()[q_id];
                            out << " " << q->cycle_available;
                        });
            out << "\tin memory: ";
            std::for_each(inst->q_begin(), inst->q_end(),
                        [this, c, &out] (auto q_id)
                        {
                            Qubit* q = c->qubits()[q_id];
                            out << compute_subsystem_->is_qubit_in_local_memory(q);
                        });
            out << "\n";
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
Driver::done() const
{
    bool all_done{true};
    for (auto* c : clients_)
    {
        bool d = _client_is_done(c, simulation_instructions);
        if (d)
            c->s_cycle_complete = std::min(current_cycle(), c->s_cycle_complete);
        all_done &= d;
    }
    return all_done;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::optional<cycle_type>
Driver::skip_to_cycle() const
{
    // check that production cannot do anything (top-level buffer is full)
    const bool ok_to_skip = t_producer_->is_saturated()
                            && (epr_producer_ == nullptr || epr_producer_->is_saturated())
                            && !GL_RDR_ENABLED;
    if (ok_to_skip)
    {
        cycle_type earliest_cycle = std::numeric_limits<cycle_type>::max();

        std::vector<Qubit*> operands;
        operands.reserve(3);
        for (auto* c : active_clients_)
        {
            auto front_layer = c->get_ready_instructions([] (const auto* ) { return true; });
            for (auto* inst : front_layer)
            {
                auto* executed_inst = (inst->uop_count() == 0) ? inst : inst->current_uop();
                operands.clear();
                std::transform(executed_inst->q_begin(), executed_inst->q_end(), std::back_inserter(operands),
                        [&c] (auto q_id) { return c->qubits()[q_id]; });
                if (!is_instruction_ready(executed_inst, operands))
                    continue;
                cycle_type ready_cycle{0};
                for (auto* q : operands)
                    ready_cycle = std::max(ready_cycle, q->cycle_available);
                earliest_cycle = std::min(earliest_cycle, ready_cycle);
            }
        }
        if (earliest_cycle > current_cycle())
            return std::make_optional(earliest_cycle);
    }
    return std::nullopt;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Driver::fidelity_data_type
Driver::application_fidelity(int id, uint64_t scale_to_inst, double p) const
{
    Client* c = clients_[id];
    const double scale = fpdiv(scale_to_inst, c->s_inst_done);

    fidelity_data_type f{};

    // 1. compute subsystem fidelity:
    double lg_compute_f = compute_subsystem_->log_fidelity(c, scale, freq_khz, p);

    // 2. memory subsystem fidelity:
    std::vector<double> lg_memory_f(memory_subsystem_.size());
    std::transform(memory_subsystem_.begin(), memory_subsystem_.end(), lg_memory_f.begin(),
                [this, c, scale, p] (const auto* m) { return m->log_fidelity(c, scale, this->freq_khz, p); });
    double lg_memory_total_f = std::reduce(lg_memory_f.begin(), lg_memory_f.end(), 0.0);

    // 3. fidelity of other components (i.e., RDR)
    double lg_rdr_f{0.0};
    if (GL_RDR_ENABLED)
    {
        // we need to compute the fidelity of each Rz magic state upon consumption
        double mean_cycles_per_req = convert_cycles_between_frequencies(rdr_->s_request_latency.mean(),
                                                                        compute_subsystem_->freq_khz,
                                                                        freq_khz);
        double mean_idle_time_per_req = convert_cycles_between_frequencies(rdr_->s_post_completion_idle_time.mean(),
                                                                            compute_subsystem_->freq_khz,
                                                                            freq_khz);
        double t_gates_per_req = c->s_rotation_uops.mean();
        double t_infidelity = t_producer_->top_level_infidelity();
        double reqs = rdr_->s_requests_used * scale;

        size_t d = compute_subsystem_->code_distance;

        double ler_per_cycle = configuration::surface_code_logical_error_rate(d, p);

        lg_rdr_f = reqs * mean_cycles_per_req * std::log(1 - ler_per_cycle)
                    + 0.5 * reqs * t_gates_per_req * std::log(1 - t_infidelity) // we need to multiply by 0.5 to avoid
                                                                                // double counting with `lg_compute_f`
                    + reqs * mean_idle_time_per_req * std::log(1 - ler_per_cycle);
    }


    f.overall = std::exp(lg_compute_f + lg_memory_total_f + lg_rdr_f);
    f.compute = std::exp(lg_compute_f);
    f.mem = std::exp(lg_memory_total_f);
    f.rdr = std::exp(lg_rdr_f);
    return f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
Driver::operate()
{
    long progress{0};

    /* 1. update clients and execute context switch if needed */

    handle_completed_clients();
    auto [c1, c2] = context_switch_condition();
    if (c1 != nullptr)
        do_context_switch(c1, c2);

    /* 2. handle context switch memory accesses */

    if (!context_switch_memory_access_buffer_.empty())
    {
        auto begin = context_switch_memory_access_buffer_.begin(),
             end = context_switch_memory_access_buffer_.end();
        auto it = std::remove_if(begin, end,
                        [this] (const auto& p)
                        {
                            const auto [q1, q2] = p;  // don't be fooled -- `q1` and `q2` are pointers.
                            if (q1->cycle_available <= current_cycle() && q2->cycle_available <= current_cycle())
                            {
                                Instruction cls(Instruction::Type::COUPLED_LOAD_STORE, {q1->qubit_id, q2->qubit_id});
                                auto result = compute_subsystem_->execute_instruction(&cls, {q1,q2});
                                return result.progress > 0;
                            }
                            else
                            {
                                return false;
                            }
                        });
        progress += std::distance(it, end);
        context_switch_memory_access_buffer_.erase(it, end);
    }

    /* 3. Handle pending instructions for any active clients */
    //
    // `active_clients_.size()` is normally `concurrent_clients`, but can be smaller near the
    // end of a run: `handle_completed_clients()` (above) erases a finished client outright
    // once `inactive_clients_` is exhausted, rather than replacing it. Indexing/wrapping by
    // `concurrent_clients` here would then read `active_clients_` out of bounds (or, once it
    // empties completely, divide by zero below) -- so this uses `active_clients_.size()`
    // itself, visiting every currently-active client exactly once.
    const size_t n_active = active_clients_.size();
    if (n_active > 0)
    {
        size_t ii{last_used_client_idx_ % n_active};
        for (size_t i = 0; i < n_active; i++)
        {
            Client* c = active_clients_[ii];
            progress += fetch_and_execute_instructions_from_client(c);
            ii++;
            if (ii >= n_active)
                ii = 0;
        }
        last_used_client_idx_ = (last_used_client_idx_+1) % n_active;
    }

    if (GL_RDR_ENABLED)
    {
        size_t magic_states_avail = t_producer_->top_level_buffer_occupancy();
        if (magic_states_avail > 1)
            progress += rdr_->operate();
    }

    if (progress == 0)
        cycles_without_progress++;
    else
        cycles_without_progress = 0;
    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::handle_completed_clients()
{
    for (auto c_it = active_clients_.begin(); c_it != active_clients_.end(); )
    {
        Client* c = *c_it;
        if (_client_is_done(c, simulation_instructions))
        {
            c->s_cycle_complete = current_cycle();
            if (!inactive_clients_.empty())
            {
                // note that `do_context_switch` will set `*c_it`, so we only
                // need to incremenet `c_it`
                do_context_switch(std::move(inactive_clients_.front()), c);
                inactive_clients_.pop_front();
                c_it++;
            }
            else
            {
                c_it = active_clients_.erase(c_it);
            }
        }
        else
        {
            c_it++;
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::do_context_switch(Client* in, Client* out)
{
    auto c_it = std::find(active_clients_.begin(), active_clients_.end(), out);
    if (c_it == active_clients_.end())
        std::cerr << "Driver::context_switch: tried to context switch out an inactive client" << _die{};
    *c_it = in;

    /* 1. Populate `context_switch_memory_access_buffer_` */

    // need to get lists of active qubits for `in` and `out`
    const context_type& in_ctx = client_context_table_[in->id];
    const size_t qubits_per_client = compute_subsystem_->local_memory_capacity / concurrent_clients;
    std::vector<Qubit*> out_active_qubits;
    out_active_qubits.reserve(qubits_per_client);

    const auto& m = compute_subsystem_->local_memory();
    std::copy_if(m.begin(), m.end(), std::back_inserter(out_active_qubits),
                [out_id=out->id] (Qubit* q) { return q->client_id == out_id; });

    // generate memory accesses:
    assert(context_switch_memory_access_buffer_.empty());
    assert(in_ctx.active_qubits.size() == out_active_qubits.size());
    for (size_t i = 0; i < in_ctx.active_qubits.size(); i++)
        context_switch_memory_access_buffer_.emplace_back(in_ctx.active_qubits[i], out_active_qubits[i]);

    /* 2. Update context for `out` */

    client_context_table_[out->id] = context_type{.active_qubits=std::move(out_active_qubits),
                                                  .cycle_saved=current_cycle()};

    s_context_switches++;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::retire_instruction(Client* c, inst_ptr inst, cycle_type inst_latency)
{
    if (is_rotation_instruction(inst->type))
        s_rotation_instructions++;

    inst->cycle_done = current_cycle() + inst_latency;
    c->retire_instruction(inst);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
Driver::fetch_and_execute_instructions_from_client(Client* c)
{
    auto front_layer = c->get_ready_instructions([] (const auto* ) { return true; });
    std::sort(front_layer.begin(), front_layer.end(),
            [] (const auto* a, const auto* b) { return a->number < b->number; });

    std::vector<Qubit*> operands;
    operands.reserve(3);

    long success_count{0};
    for (auto* inst : front_layer)
    {
        if (GL_ELIDE_CLIFFORDS
                && !is_rotation_instruction(inst->type) 
                && !is_t_like_instruction(inst->type) 
                && !is_memory_access(inst->type))
        {
            std::cerr << "Driver::fetch_and_execute_instruction: unexpected clifford: " << *inst << _die{};
        }

        // translate the operands of the instruction into the actual program qubits
        operands.clear();
        auto* executed_inst = (inst->uop_count() == 0) ? inst : inst->current_uop();
        std::transform(executed_inst->q_begin(), executed_inst->q_end(), std::back_inserter(operands),
                [&c] (auto q_id) { return c->qubits()[q_id]; });

        // check if the operands are ready:
        if (!is_instruction_ready(executed_inst, operands))
            continue;

        // update stats:
        inst->first_ready_cycle = std::min(inst->first_ready_cycle, current_cycle());
        update_instruction_stats_on_fetch(executed_inst, operands);

        const bool are_operands_ready = std::all_of(operands.begin(), operands.end(),
                                            [c=current_cycle()] (const auto& q) { return c >= q->cycle_available; });
        if (!are_operands_ready)
            continue;

        /* RDR logic: first check if the rotation's magic state is already available    *
         * if so, then try and apply the magic state                                    */
        if (GL_RDR_ENABLED && is_rotation_instruction(inst->type))
        {
            if (rdr_handle_instruction(c, inst, operands[0])) 
                continue;
            inst->rdr.visited = true;
        }

        if (GL_RLTP_DEGREE > 0 && is_rotation_instruction(inst->type))
        {
            // reaction-limited T teleportation:
            auto result = compute_subsystem_->do_rotation_via_rltp(inst, operands[0], GL_RLTP_DEGREE);
            if (result.progress)
                if (inst->uops_retired() == inst->uop_count())
                    retire_instruction(c, inst, result.latency);
        }
        else
        {
            auto result = compute_subsystem_->execute_instruction(executed_inst, operands);
            success_count += result.progress;
            if (result.progress)
            {
                update_instruction_stats_before_retire(executed_inst);
                if (inst->uop_count() == 0 || inst->retire_current_uop())
                    retire_instruction(c, inst, result.latency);
            }
        }
    }

    // recursively call `fetch_and_execute_instruction_from_client` if progress was made
    if (success_count)
        success_count += fetch_and_execute_instructions_from_client(c);
    return success_count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::update_instruction_stats_on_fetch(inst_ptr inst, const std::vector<Qubit*>& operands)
{
    _assign_if_empty(inst->stall_data.ready, current_cycle());
    _assign_if_empty(inst->stall_data.all_load_results_avail,
                    [this, &operands] ()
                    {
                        cycle_type latest = current_cycle();
                        for (size_t i = 0; i < operands.size(); i++)
                            if (operands[i]->last_operation_was_memory_access)
                                latest = std::max(latest, operands[i]->cycle_available);
                        return std::make_optional(latest);
                    });
    _assign_if_empty(inst->stall_data.resource_avail,
                    [this, t=inst->type] ()
                    {
                        if (is_t_like_instruction(t))
                        {
                            if (compute_subsystem_->count_available_magic_states() > 0)
                                return std::make_optional(current_cycle());
                            else
                                return std::optional<cycle_type>{};
                        }
                        else
                        {
                            return std::make_optional(current_cycle());
                        }
                    });
    _assign_if_empty(inst->stall_data.epr_avail,
                    [this, &operands, t=inst->type] ()
                    {
                        if (is_memory_access(t) && !compute_subsystem_->epr_credit_available_for(operands[0], t))
                            return std::optional<cycle_type>{};
                        else
                            return std::make_optional(current_cycle());
                    });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
Driver::update_instruction_stats_before_retire(inst_ptr inst)
{
    cycle_type s = *inst->stall_data.ready,
               x = *inst->stall_data.all_load_results_avail,
               y = *inst->stall_data.resource_avail;

    stall_monitor_.add_stall_range(StallType::MEMORY, s, x, false);
    if (is_t_like_instruction(inst->type))
        stall_monitor_.add_stall_range(StallType::MAGIC_STATE, s, y, false);
    if (is_memory_access(inst->type))
        stall_monitor_.add_stall_range(StallType::EPR, s, *inst->stall_data.epr_avail, false);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
Driver::is_instruction_ready(inst_ptr inst, const std::vector<Qubit*>& operands) const
{
    auto begin = operands.begin(),
         end = operands.begin() + inst->qubit_count;
    bool all_available = true; //std::all_of(begin, end,
                               //[this] (const auto* q) { return q->cycle_available <= current_cycle(); });

    if (inst->type == Instruction::Type::COUPLED_LOAD_STORE)
    {
        all_available &= compute_subsystem_->is_qubit_in_local_memory(operands[1]);
    }
    else if (inst->type != Instruction::Type::LOAD)
    {
        all_available &= std::all_of(begin, end,
                                [this] (const auto* q) { return compute_subsystem_->is_qubit_in_local_memory(q); });
    }

    return all_available;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
Driver::rdr_handle_instruction(Client* c, inst_ptr inst, Qubit* q)
{
    if (inst->rdr.visited)
        return false;

    if (!inst->rdr.pending)
    {
        rdr_->do_runahead(c, inst);
        return false;
    }
    else if (rdr_->is_done(inst))
    {
        using outcome_type = driver::RotationDirectedRunahead::ApplyMagicStateResult;

        auto outcome = rdr_->apply_magic_state(inst, q);
        if (outcome == outcome_type::GOOD)
        {
//          std::cout << "RDR instruction latency: " << (q->cycle_available - *inst->first_ready_cycle) 
//                      << " (apply time = " << (q->cycle_available - current_cycle()) 
//                      << "\n";
            retire_instruction(c, inst, q->cycle_available - current_cycle());
            return true;
        }
        else if (outcome == outcome_type::NEEDS_CORRECTION)
        {
            inst->reset_uops();
            inst->urotseq = inst->corr_urotseq_array.front();
            inst->corr_urotseq_array.pop_front();
            rdr_->do_runahead(c, inst);
            return false;
        }
        else  // `outcome_type::ROUTING_CONTENTION`
        {
            // idle until we get routing space for the magic state
            return true;
        }
    }
    else
    {
        bool inv = rdr_->interrupt_and_invalidate_if_necessary(inst);
        if (inv)
            rdr_->do_runahead(c, inst);
        return !inv;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

bool
_client_is_done(const Client* c, uint64_t s)
{
    return c->s_unrolled_inst_done >= s;
}

bool
_current_uop_is_t_like(inst_ptr inst)
{
    return is_t_like_instruction(inst->type) 
            || (inst->uop_count() > 0 && is_t_like_instruction(inst->current_uop()->type));
}

template <class T> void
_assign_if_empty(std::optional<T>& x, T y)
{
    if (!x.has_value())
        x = y;
}

template <class T, class F> void
_assign_if_empty(std::optional<T>& x, const F& fn)
{
    if (!x.has_value())
    {
        auto y = fn();
        if (y.has_value())
            x = *y;
    }
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
