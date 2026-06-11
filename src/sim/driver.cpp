/*
 *  author: Suhas Vittal
 *  date:   12 March 2026
 * */

#include "sim/configuration/resource_estimation.h"
#include "sim/driver.h"
#include "sim/stats.h"

#include <algorithm>
#include <cassert>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = COMPUTE_SUBSYSTEM::inst_ptr;

constexpr size_t STALL_MONITOR_MAX_RANGES{2'048};

/*
 * Returns true if the client is complete.
 * */
bool _client_is_done(const CLIENT*, uint64_t simulation_instructions);

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

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

DRIVER::DRIVER(std::vector<std::string> client_trace_files,
                size_t _concurrent_clients,
                uint64_t _simulation_instructions,
                COMPUTE_SUBSYSTEM* compute_subsystem,
                std::vector<PRODUCER_BASE*> t_factories,
                std::vector<MEMORY_LEVEL*> memory_subsystem)
    :OPERABLE("sim", compute_subsystem->freq_khz),
    concurrent_clients(_concurrent_clients),
    total_clients(client_trace_files.size()),
    simulation_instructions(_simulation_instructions),
    clients_(total_clients),
    active_clients_(concurrent_clients),
    inactive_clients_(total_clients - concurrent_clients),
    client_context_table_(total_clients),
    compute_subsystem_(compute_subsystem),
    t_factories_(t_factories),
    memory_subsystem_(memory_subsystem),
    stall_monitor_(STALL_MONITOR_MAX_RANGES)
{
    /* initialize all clients */

    assert(total_clients >= concurrent_clients);
    for (client_id_type i = 0; i < client_trace_files.size(); i++)
        clients_[i] = new CLIENT(client_trace_files[i], i);
    std::copy(clients_.begin(), clients_.begin() + concurrent_clients, active_clients_.begin());
    std::copy(clients_.begin() + concurrent_clients, clients_.end(), inactive_clients_.begin());

    std::vector<QUBIT*> program_qubits;
    for (auto* c : clients_)
        std::copy(c->qubits().begin(), c->qubits().end(), std::back_inserter(program_qubits));
    compute_subsystem_->initialize_qubits(program_qubits);

    if (GL_RDR_ENABLED)
        rdr_ = new driver::ROTATION_DIRECTED_RUNAHEAD(compute_subsystem_);
}

DRIVER::~DRIVER()
{
    for (auto* c : clients_)
        delete c;
    if (GL_RDR_ENABLED)
        delete rdr_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
DRIVER::print_progress(std::ostream& out) const
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
DRIVER::print_deadlock_info(std::ostream& out) const
{
    for (auto* f : t_factories_)
        f->print_deadlock_info(out);

    out << "local memory contents:";
    for (auto* q : compute_subsystem_->local_memory())
        out << " " << *q;
    out << "\n";

    for (auto* c : active_clients_)
    {
        out << "Client " << static_cast<int>(c->id) << " front layer:\n";
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
                            QUBIT* q = c->qubits()[q_id];
                            out << " " << q->cycle_available;
                        });
            out << "\tin memory: ";
            std::for_each(inst->q_begin(), inst->q_end(),
                        [this, c, &out] (auto q_id)
                        {
                            QUBIT* q = c->qubits()[q_id];
                            out << compute_subsystem_->is_qubit_in_local_memory(q);
                        });
            out << "\n";
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
DRIVER::done() const
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

void
DRIVER::stop_simulation()
{
    stall_monitor_.commit_contents();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

DRIVER::fidelity_data_type
DRIVER::application_fidelity(int id, uint64_t scale_to_inst, double p) const
{
    CLIENT* c = clients_[id];
    const double scale = mean(scale_to_inst, c->s_inst_done);

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
        double req_completion_cycles = convert_cycles_between_frequencies(rdr_->s_request_completion_cycles_sum,
                                                                            compute_subsystem_->freq_khz,
                                                                            freq_khz);
        double req_idle_cycles = convert_cycles_between_frequencies(rdr_->s_post_completion_idle_time_sum,
                                                                    compute_subsystem_->freq_khz,
                                                                    freq_khz);

        double mean_cycles_per_req = mean(req_completion_cycles, rdr_->s_requests_used);
        double t_gates_per_req = mean(c->s_total_rotation_uops, c->s_total_rotations);
        double mean_idle_time_per_req = mean(req_idle_cycles, rdr_->s_requests_used);
        double t_infidelity = t_factories_[0]->output_error_probability;
        double reqs = rdr_->s_requests_used * scale;

        size_t d = compute_subsystem_->code_distance;

        double ler_per_d_cycles = configuration::surface_code_logical_error_rate(d, p);

        lg_rdr_f = reqs * mean(mean_cycles_per_req, d) * std::log(1 - ler_per_d_cycles)
                    + 0.5 * reqs * t_gates_per_req * std::log(1 - t_infidelity) // we need to multiply by 0.5 to avoid
                                                                                // double counting with `lg_compute_f`
                    + reqs * mean(mean_idle_time_per_req, d) * std::log(1 - ler_per_d_cycles);
    }


    f.overall = std::exp(lg_compute_f + lg_memory_total_f + lg_rdr_f);
    f.compute = std::exp(lg_compute_f);
    f.mem = std::exp(lg_memory_total_f);
    f.rdr = std::exp(lg_rdr_f);
    return f;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM*
DRIVER::compute_subsystem() const
{
    return compute_subsystem_;
}

const std::vector<CLIENT*>&
DRIVER::clients() const
{
    return clients_;
}

const DRIVER::stall_monitor_type&
DRIVER::stall_monitor() const
{
    return stall_monitor_;
}

driver::ROTATION_DIRECTED_RUNAHEAD*
DRIVER::rdr() const
{
    return rdr_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
DRIVER::operate()
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
                                INSTRUCTION cls(INSTRUCTION::TYPE::COUPLED_LOAD_STORE, {q1->qubit_id, q2->qubit_id});
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
    size_t ii{last_used_client_idx_};
    for (size_t i = 0; i < concurrent_clients; i++)
    {
        CLIENT* c = active_clients_[ii];
        progress += fetch_and_execute_instructions_from_client(c);
        ii++;
        if (ii >= concurrent_clients)
            ii = 0;
    }
    last_used_client_idx_ = (last_used_client_idx_+1) % active_clients_.size();

    if (GL_RDR_ENABLED)
    {
        size_t magic_states_avail = std::transform_reduce(t_factories_.begin(), t_factories_.end(), size_t{0},
                                                        std::plus<size_t>{},
                                                        [] (const auto* f) { return f->buffer_occupancy(); });
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
DRIVER::handle_completed_clients()
{
    for (auto c_it = active_clients_.begin(); c_it != active_clients_.end(); )
    {
        CLIENT* c = *c_it;
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

std::pair<CLIENT*, CLIENT*>
DRIVER::context_switch_condition() const
{
    return std::make_pair(nullptr, nullptr);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
DRIVER::do_context_switch(CLIENT* in, CLIENT* out)
{
    auto c_it = std::find(active_clients_.begin(), active_clients_.end(), out);
    if (c_it == active_clients_.end())
        std::cerr << "DRIVER::context_switch: tried to context switch out an inactive client" << _die{};
    *c_it = in;

    /* 1. Populate `context_switch_memory_access_buffer_` */

    // need to get lists of active qubits for `in` and `out`
    const context_type& in_ctx = client_context_table_[in->id];
    const size_t qubits_per_client = compute_subsystem_->local_memory_capacity / concurrent_clients;
    std::vector<QUBIT*> out_active_qubits;
    out_active_qubits.reserve(qubits_per_client);

    const auto& m = compute_subsystem_->local_memory();
    std::copy_if(m.begin(), m.end(), std::back_inserter(out_active_qubits),
                [out_id=out->id] (QUBIT* q) { return q->client_id == out_id; });

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
DRIVER::retire_instruction(CLIENT* c, inst_ptr inst, cycle_type inst_latency)
{
    if (is_rotation_instruction(inst->type))
        s_rotation_instructions++;

    inst->cycle_done = current_cycle() + inst_latency;
    c->retire_instruction(inst);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
DRIVER::fetch_and_execute_instructions_from_client(CLIENT* c)
{
    auto front_layer = c->get_ready_instructions([] (const auto* ) { return true; });
    std::sort(front_layer.begin(), front_layer.end(),
            [] (const auto* a, const auto* b) { return a->number < b->number; });
    std::vector<QUBIT*> operands(3);
    long success_count{0};
    for (auto* inst : front_layer)
    {
        if (GL_ELIDE_CLIFFORDS
                && !is_rotation_instruction(inst->type) 
                && !is_t_like_instruction(inst->type) 
                && !is_memory_access(inst->type))
        {
            std::cerr << "DRIVER::fetch_and_execute_instruction: unexpected clifford: " << *inst << _die{};
        }

        // translate the operands of the instruction into the actual program qubits
        auto* executed_inst = (inst->uop_count() == 0) ? inst : inst->current_uop();
        std::transform(executed_inst->q_begin(), executed_inst->q_end(), operands.begin(),
                [&c] (auto q_id) { return c->qubits()[q_id]; });

        // check if the operands are ready:
        if (!is_instruction_ready(executed_inst, operands))
            continue;

        // update stats:
        update_instruction_stats_on_fetch(inst, operands);

        /* RDR logic: first check if the rotation's magic state is already available    *
         * if so, then try and apply the magic state                                    */
        if (GL_RDR_ENABLED && is_rotation_instruction(inst->type))
        {
            if (rdr_handle_instruction(c, inst, operands[0])) 
                continue;
            inst->rdr_has_been_visited = true;
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
                update_instruction_stats_before_retire(inst);
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
DRIVER::update_instruction_stats_on_fetch(inst_ptr inst, const std::vector<QUBIT*>& operands)
{
    _assign_if_empty(inst->first_ready_cycle, current_cycle());
    _assign_if_empty(inst->first_ready_cycle_for_current_uop, current_cycle());

    const size_t qubit_count = (inst->uop_count() > 0) ? inst->current_uop()->qubit_count : inst->qubit_count;

    if (!inst->first_cycle_with_all_load_results_available.has_value())
    {
        cycle_type latest_load_result_cycle{current_cycle()};
        for (size_t i = 0; i < qubit_count; i++)
            if (operands[i]->last_operation_was_memory_access)
                latest_load_result_cycle = std::max(operands[i]->cycle_available, latest_load_result_cycle);
        inst->first_cycle_with_all_load_results_available = latest_load_result_cycle;
    }
    
    if (!inst->first_cycle_with_available_resource_state.has_value())
    {
        if (_current_uop_is_t_like(inst))
        {
            bool any_magic_state_avail = std::any_of(t_factories_.begin(), t_factories_.end(),
                                                     [] (const auto* f) { return f->buffer_occupancy() > 0; });
            if (any_magic_state_avail)
                _assign_if_empty(inst->first_cycle_with_available_resource_state, current_cycle());
        }
        else
        {
            inst->first_cycle_with_available_resource_state = current_cycle();
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
DRIVER::update_instruction_stats_before_retire(inst_ptr inst)
{
    cycle_type s = *inst->first_ready_cycle_for_current_uop,
               x = *inst->first_cycle_with_all_load_results_available,
               y = *inst->first_cycle_with_available_resource_state;

    stall_monitor_.add_stall_range(STALL_TYPE::MEMORY, s, x, false);
    if (is_t_like_instruction(inst->type))
        stall_monitor_.add_stall_range(STALL_TYPE::MAGIC_STATE, s, y, false);

    // reset any stats:
    inst->first_ready_cycle_for_current_uop.reset();
    inst->first_cycle_with_all_load_results_available.reset();
    inst->first_cycle_with_available_resource_state.reset();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
DRIVER::is_instruction_ready(inst_ptr inst, const std::vector<QUBIT*>& operands) const
{
    auto begin = operands.begin(),
         end = operands.begin() + inst->qubit_count;
    bool all_available = std::all_of(begin, end,
                                [this] (const auto* q) { return q->cycle_available <= current_cycle(); });

    if (inst->type == INSTRUCTION::TYPE::COUPLED_LOAD_STORE)
    {
        all_available &= compute_subsystem_->is_qubit_in_local_memory(operands[1]);
    }
    else if (inst->type != INSTRUCTION::TYPE::LOAD)
    {
        all_available &= std::all_of(begin, end,
                                [this] (const auto* q) { return compute_subsystem_->is_qubit_in_local_memory(q); });
    }

    return all_available;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
DRIVER::rdr_handle_instruction(CLIENT* c, inst_ptr inst, QUBIT* q)
{
    if (inst->rdr_has_been_visited)
        return false;

    if (!inst->rdr_is_pending)
    {
        rdr_->do_runahead(c, inst);
        return false;
    }
    else if (rdr_->is_done(inst))
    {
        using outcome_type = driver::ROTATION_DIRECTED_RUNAHEAD::APPLY_MAGIC_STATE_RESULT;

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
_client_is_done(const CLIENT* c, uint64_t s)
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

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
