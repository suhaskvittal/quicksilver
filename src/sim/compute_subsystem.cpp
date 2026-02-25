/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include "sim/compute_subsystem.h"
#include "sim/memory_subsystem.h"
#include "sim/production/epr.h"
#include "sim/stats.h"

#include <fstream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = COMPUTE_SUBSYSTEM::inst_ptr;

static std::uniform_real_distribution FPR{0.0,1.0};

/*
 * Returns true if the client is complete.
 * */
bool   _client_is_done(const CLIENT*, uint64_t simulation_instructions);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::COMPUTE_SUBSYSTEM(double freq_khz,
                                     std::vector<std::string>     client_trace_files,
                                     size_t                       code_distance,
                                     size_t                       local_memory_capacity,
                                     size_t                       _concurrent_clients,
                                     uint64_t                     _simulation_instructions,
                                     std::vector<PRODUCER_BASE*>  top_level_t_factories,
                                     MEMORY_SUBSYSTEM*            memory_hierarchy,
                                     compute_extended_config      conf)
    :COMPUTE_BASE("compute_subsystem", 
                    freq_khz, 
                    code_distance,
                    local_memory_capacity, 
                    top_level_t_factories,
                    memory_hierarchy),
    concurrent_clients(_concurrent_clients),
    total_clients(client_trace_files.size()),
    simulation_instructions(_simulation_instructions),
    all_clients_(total_clients),
    active_clients_(concurrent_clients),
    inactive_clients_(total_clients - concurrent_clients),
    client_context_table_(total_clients),
    ed_units_(conf.ed_units)
{
    // initialize clients:
    assert(total_clients >= concurrent_clients);
    for (client_id_type i = 0; i < client_trace_files.size(); i++)
        all_clients_[i] = new CLIENT{client_trace_files[i], i};

    auto c_begin = all_clients_.begin(),
         c_end = all_clients_.end();
    auto c_mid = c_begin + concurrent_clients;
    std::copy(c_begin, c_mid, active_clients_.begin());
    std::copy(c_mid, c_end, inactive_clients_.begin());

    // initialize all the memory:
    std::vector<std::vector<QUBIT*>> qubits_by_client(total_clients);
    std::transform(c_begin, c_end, qubits_by_client.begin(), [] (const auto* c) { return c->qubits(); });
    std::vector<STORAGE*> all_storage{local_memory_.get()};
    std::copy(memory_hierarchy_->storages().begin(), memory_hierarchy_->storages().end(), std::back_inserter(all_storage));
    storage_striped_initialization(all_storage, qubits_by_client, concurrent_clients);

    // initialize context for all inactive clients:
    const size_t active_qubits_per_client = local_memory_capacity / concurrent_clients;
    for (auto* c : inactive_clients_)
    {
        auto q_begin = c->qubits().begin();
        auto q_end = q_begin + active_qubits_per_client;
        client_context_table_[c->id].active_qubits.assign(q_begin, q_end);
    }
    context_switch_memory_access_buffer_.reserve(active_qubits_per_client);

    /* Extended config setup */
    if (conf.rpc_enabled)
    {
        rotation_subsystem_ = new ROTATION_SUBSYSTEM(freq_khz, 
                                                    code_distance,
                                                    conf.rpc_capacity,
                                                    this,
                                                    conf.rpc_watermark);
    }
}

COMPUTE_SUBSYSTEM::~COMPUTE_SUBSYSTEM()
{
    for (auto* c : all_clients_)
        delete c;

    if (rotation_subsystem_ != nullptr)
        delete rotation_subsystem_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::print_progress(std::ostream& ostrm) const
{
    std::cout << "cycle " << current_cycle() << " -------------------------------------------------------------";

    double t_bandwidth = mean(s_magic_state_produced_sum, current_cycle());
    double t_bandwidth_per_s = mean(s_magic_state_produced_sum, current_cycle() / (1e3*freq_khz));

    ostrm << "\nwalltime = " << sim::walltime_s() << "s"
        << "\nt bandwidth (#/cycle) = " << t_bandwidth << " (#/s) = " << t_bandwidth_per_s
        << "\n";

    for (auto* c : all_clients_)
    {
        auto active_it = std::find(active_clients_.begin(), active_clients_.end(), c);
        bool is_active = (active_it != active_clients_.end());

        if (is_active)
            ostrm << " * client " << static_cast<int>(c->id);
        else
            ostrm << "   client " << static_cast<int>(c->id);

        double ipc = stats::ipc(c->s_unrolled_inst_done, current_cycle());
        double ipdc = stats::ipdc(c->s_unrolled_inst_done, current_cycle(), code_distance);
        double kips = stats::kips(c->s_unrolled_inst_done, current_cycle(), freq_khz);

        ostrm << "\n\tinstructions completed = " << c->s_unrolled_inst_done
                << "\n\tIPC = " << ipc
                << "\n\tIPdC = " << ipdc
                << "\n\tKIPS = " << kips
                << "\n";
    }

    if (is_rpc_enabled())
        rotation_subsystem_->print_progress(ostrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::print_deadlock_info(std::ostream& ostrm) const
{
    for (auto* f : top_level_t_factories_)
        f->print_deadlock_info(ostrm);

    ostrm << "local memory contents:";
    for (auto* q : local_memory_->contents())
        ostrm << " " << *q;
    ostrm << "\n";

    for (auto* c : active_clients_)
    {
        ostrm << "Client " << static_cast<int>(c->id) << " front layer:\n";
        for (const auto* inst : c->dag()->get_front_layer())
        {
            ostrm << "\t" << *inst;
            if (inst->uop_count() > 0)
            {
                ostrm << "\tcurrent uop = " << *inst->current_uop() << ", " << inst->uops_retired()
                            << " of " << inst->uop_count();
            }

            ostrm << "\tcycle ready (current cycle = " << current_cycle() << "):";
            std::for_each(inst->q_begin(), inst->q_end(),
                        [this, c, &ostrm] (auto q_id)
                        {
                            QUBIT* q = c->qubits()[q_id];
                            ostrm << " " << q->cycle_available;
                        });
            ostrm << "\tin memory: ";
            std::for_each(inst->q_begin(), inst->q_end(),
                        [this, c, &ostrm] (auto q_id)
                        {
                            QUBIT* q = c->qubits()[q_id];
                            ostrm << static_cast<int>(local_memory_->contains(q));
                        });
            ostrm << "\n";
        }
    }

    if (is_rpc_enabled())
        rotation_subsystem_->print_deadlock_info(ostrm);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::done() const
{
    bool all_done{true};
    for (auto* c : all_clients_)
    {
        bool d = _client_is_done(c, simulation_instructions);
        if (d)
            c->s_cycle_complete = std::min(current_cycle(), c->s_cycle_complete);
        all_done &= d;
    }
    return all_done;
}

const std::vector<CLIENT*>&
COMPUTE_SUBSYSTEM::clients() const
{
    return all_clients_;
}

ROTATION_SUBSYSTEM*
COMPUTE_SUBSYSTEM::rotation_subsystem() const
{
    return rotation_subsystem_;
}

const std::vector<COMPUTE_SUBSYSTEM::production_level_type>&
COMPUTE_SUBSYSTEM::entanglement_distillation_units() const
{
    return ed_units_;
}

bool
COMPUTE_SUBSYSTEM::is_rpc_enabled() const
{
    return rotation_subsystem_ != nullptr;
}

bool
COMPUTE_SUBSYSTEM::is_ed_in_use() const
{
    return !ed_units_.empty();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::optional<cycle_type>
COMPUTE_SUBSYSTEM::skip_to_cycle() const
{
    // 1. check criteria are met for skipping:

    const bool t_factories_full = std::all_of(top_level_t_factories_.begin(), top_level_t_factories_.end(),
                                            [] (const auto* f) { return f->buffer_occupancy() == f->buffer_capacity; });
    const bool rs_done = !is_rpc_enabled() || !rotation_subsystem_->is_active();

    const bool do_skip = t_factories_full && rs_done;
    if (do_skip)
    {
        std::optional<cycle_type> min_cycle;
        for (auto* c : active_clients_)
        {
            for (auto* inst : c->dag()->get_front_layer())
            {
                auto r = get_next_ready_cycle_for_instruction(c, inst);
                if (r.has_value() && (!min_cycle.has_value() || r < *min_cycle))
                    min_cycle = r;
            }
        }
        return min_cycle;
    }
    return std::nullopt;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
COMPUTE_SUBSYSTEM::operate()
{
    long progress{0};

    /* Update stats (pre-execution) */

    const size_t magic_states_before_exec{count_available_magic_states()};
    s_magic_state_produced_sum += magic_states_before_exec - magic_states_avail_last_cycle_;
    had_rpc_stall_this_cycle_ = false;

    /* 1. Update clients and execute context switch if needed */

    handle_completed_clients();
    auto [ctx_s_c1, ctx_s_c2] = context_switch_condition();
    if (ctx_s_c1 != nullptr)
        do_context_switch(ctx_s_c1, ctx_s_c2);

    /* 2. Handle context switch memory accesses */

    if (!context_switch_memory_access_buffer_.empty())
    {
        auto begin = context_switch_memory_access_buffer_.begin(),
             end = context_switch_memory_access_buffer_.end();
        auto it = std::remove_if(begin, end,
                        [this] (const auto& p)
                        {
                            const auto [q1, q2] = p;  // don't be fooled -- `q1` and `q2` are pointers.
                            if (q1->cycle_available <= current_cycle() && q2->cycle_available <= current_cycle())
                                return do_memory_access(nullptr, q1, q2).progress > 0;
                            else
                                return false;
                        });
        progress += std::distance(it, end);
        context_switch_memory_access_buffer_.erase(it, end);
    }

    /* 3. Handle pending instructions for any active clients */

    // process instructions for each client
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

    /* Update stats (post-execution) */

    const size_t magic_states_after_exec{count_available_magic_states()};
    magic_states_avail_last_cycle_ = magic_states_after_exec;
    if (had_rpc_stall_this_cycle_)
        s_cycles_with_rpc_stalls++;
    
    if (progress == 0)
        cycles_without_progress++;
    else
        cycles_without_progress = 0;

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::handle_completed_clients()
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

void
COMPUTE_SUBSYSTEM::retire_instruction(CLIENT* c, inst_ptr inst, cycle_type inst_latency)
{
    if (is_rotation_instruction(inst->type))
        s_total_rotations++;

    if (is_rpc_enabled())
        rotation_subsystem_->invalidate(inst);

    inst->cycle_done = current_cycle() + inst_latency;
    c->retire_instruction(inst);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::ctx_switch_condition_type
COMPUTE_SUBSYSTEM::context_switch_condition() const
{
    return ctx_switch_condition_type{nullptr, nullptr};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::do_context_switch(CLIENT* in, CLIENT* out)
{
    auto c_it = std::find(active_clients_.begin(), active_clients_.end(), out);
    if (c_it == active_clients_.end())
        std::cerr << "COMPUTE_SUBSYSTEM::context_switch: tried to context switch out an inactive client" << _die{};
    *c_it = in;

    /* 1. Populate `context_switch_memory_access_buffer_` */

    // need to get lists of active qubits for `in` and `out`
    const context_type& in_ctx = client_context_table_[in->id];
    std::vector<QUBIT*> out_active_qubits;
    out_active_qubits.reserve(local_memory_capacity / concurrent_clients);
    std::copy_if(local_memory_->contents().begin(), local_memory_->contents().end(), out_active_qubits.begin(),
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

long
COMPUTE_SUBSYSTEM::fetch_and_execute_instructions_from_client(CLIENT* c)
{
    auto front_layer = c->get_ready_instructions(
                            [&c, cc=current_cycle()] (const auto* inst)
                            {
                                return std::all_of(inst->q_begin(), inst->q_end(),
                                            [&c, cc] (auto q_id) { return c->qubits()[q_id]->cycle_available <= cc; });
                            });

    long success_count{0};
    for (auto* inst : front_layer)
    {
        if (GL_ELIDE_CLIFFORDS
                && !is_rotation_instruction(inst->type) 
                && !is_t_like_instruction(inst->type) 
                && !is_memory_access(inst->type))
        {
            std::cerr << "COMPUTE_SUBSYSTEM::fetch_and_execute_instruction: unexpected clifford: " << *inst << _die{};
        }
    
        inst->first_ready_cycle = std::min(current_cycle(), inst->first_ready_cycle);
        auto* executed_inst = (inst->uop_count() == 0) ? inst : inst->current_uop();
        std::array<QUBIT*, 3> operands;
        std::transform(executed_inst->q_begin(), executed_inst->q_end(), operands.begin(),
                [&c] (auto q_id) { return c->qubits()[q_id]; });

        bool any_not_in_memory = std::any_of(operands.begin(), operands.begin() + executed_inst->qubit_count,
                                        [this] (QUBIT* q) { return !local_memory_->contains(q); });
        if (any_not_in_memory && inst->type != INSTRUCTION::TYPE::LOAD && inst->type != INSTRUCTION::TYPE::COUPLED_LOAD_STORE)
            continue; 
        
        // (rpc) if this is the first visit for this instruction, check the `rotation_subsystem_`
        // and do other actions:
        if (is_rpc_enabled() && is_rotation_instruction(inst->type) && !inst->rpc_has_been_visited)
            if (rpc_handle_instruction(c, inst, operands[0]))
                continue;
            
        // RZ and RX gates are a special case since multiple uops of progress can be done
        if (is_rotation_instruction(inst->type) && GL_T_GATE_TELEPORTATION_MAX > 0)
        {
            QUBIT* q = operands[0];
            auto result = do_rotation_gate_with_teleportation(inst, q, GL_T_GATE_TELEPORTATION_MAX);
            success_count += result.progress;
            if (inst->uops_retired() == inst->uop_count())
                retire_instruction(c, inst, result.latency);
        }
        else
        {
            auto result = execute_instruction(executed_inst, std::move(operands));
            success_count += result.progress;
            if (result.progress)
                if (inst->uop_count() == 0 || inst->retire_current_uop())
                    retire_instruction(c, inst, result.latency);
        }
    }

    // recursively call `fetch_and_execute_instruction_from_client` if progress was made
    if (success_count)
        success_count += fetch_and_execute_instructions_from_client(c);
    return success_count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::rpc_handle_instruction(CLIENT* c, inst_ptr inst, QUBIT* q)
{
    RPC_LOOKUP_RESULT lookup_result = rpc_lookup_rotation(inst, q);
    if (lookup_result == RPC_LOOKUP_RESULT::RETIRE)
    {
        if (GL_RPC_ALWAYS_RUNAHEAD)
            rpc_find_and_attempt_allocate_for_future_rotation(c, inst);

        retire_instruction(c, inst, 2*code_distance);
    }
    else if (lookup_result == RPC_LOOKUP_RESULT::NEEDS_CORRECTION)
    {
        assert(!inst->corr_urotseq_array.empty());

        inst->urotseq = inst->corr_urotseq_array.front();
        inst->corr_urotseq_array.pop_front();

        // since we will have to do a corrective rotation, search for future rotations
        // to schedule:
        rpc_find_and_attempt_allocate_for_future_rotation(c, inst);
    }
    else if (lookup_result == RPC_LOOKUP_RESULT::IN_PROGRESS)
    {
        rotation_subsystem_->invalidate(inst);
        rpc_find_and_attempt_allocate_for_future_rotation(c, inst);
        return false;
    }
    else
    {
        rpc_find_and_attempt_allocate_for_future_rotation(c, inst);
    }
    return (lookup_result == RPC_LOOKUP_RESULT::RETIRE) || (lookup_result == RPC_LOOKUP_RESULT::IN_PROGRESS);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::RPC_LOOKUP_RESULT
COMPUTE_SUBSYSTEM::rpc_lookup_rotation(inst_ptr inst, QUBIT* q)
{
    const cycle_type RPC_FETCH_CYCLES{2*code_distance};
    const cycle_type RPC_APPLY_CYCLES{2*code_distance};

    if (!is_rpc_enabled())
        return RPC_LOOKUP_RESULT::NOT_FOUND;
    assert(is_rotation_instruction(inst->type));

    if (rotation_subsystem_->find_and_delete_request_if_done(inst))
    {
        bool success = (GL_RNG()&1) > 0;
        q->cycle_available = current_cycle() + RPC_FETCH_CYCLES + RPC_APPLY_CYCLES;
        s_total_rpc++;
        if (success)
            s_successful_rpc++;

        return success ? RPC_LOOKUP_RESULT::RETIRE : RPC_LOOKUP_RESULT::NEEDS_CORRECTION;
    }
    else if (rotation_subsystem_->is_request_pending(inst))
    {
        return RPC_LOOKUP_RESULT::IN_PROGRESS;
    }
    else
    {
        return RPC_LOOKUP_RESULT::NOT_FOUND;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::rpc_find_and_attempt_allocate_for_future_rotation(CLIENT* c, inst_ptr inst)
{
    constexpr size_t RPC_DAG_LOOKAHEAD_START_LAYER{0};
    constexpr size_t RPC_DAG_LOOKAHEAD_DEPTH{16};
    
    inst->rpc_has_been_visited = true;

    if (!is_rpc_enabled())
        return;
    assert(is_rotation_instruction(inst->type));

    for (size_t i = 0; i < GL_RPC_DEGREE; i++)
    {
        if (!rotation_subsystem_->can_accept_request())
            break;
        
        auto [dependent_inst, layer] = c->dag()->find_earliest_dependent_instruction_such_that(
                                            [inst, rs=rotation_subsystem_] (inst_ptr x) 
                                            { 
                                                return x != inst 
                                                        && is_rotation_instruction(x->type)
                                                        && !rs->is_request_pending(x) 
                                                        && (x->number - inst->number) < GL_RPC_INST_DELTA_LIMIT;
                                            }, 
                                            inst, 
                                            RPC_DAG_LOOKAHEAD_START_LAYER,
                                            RPC_DAG_LOOKAHEAD_START_LAYER + RPC_DAG_LOOKAHEAD_DEPTH);
        if (dependent_inst != nullptr)
            rotation_subsystem_->submit_request(dependent_inst, layer, inst);
        else
            break;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::optional<cycle_type>
COMPUTE_SUBSYSTEM::get_next_ready_cycle_for_instruction(CLIENT* c, inst_ptr inst) const
{
    if (is_memory_access(inst->type))
    {
        QUBIT* ld = c->qubits()[inst->qubits[0]],
             * st = c->qubits()[inst->qubits[1]];
        if (!local_memory_->contains(st))
            return std::nullopt;

        // we need to check when the memory subsystem can serve this load:
        cycle_type ready_cycle = memory_hierarchy_->get_next_ready_cycle_for_load(ld, freq_khz);

        // finally, make sure that `ready_cycle` does not jump any entanglement distillation unit,
        // which is generally as slow as memory:
        if (is_ed_in_use())
        {
            for (const auto& level : ed_units_)
            {
                for (const PRODUCER_BASE* _p : level)
                {
                    const auto* p = static_cast<const producer::ENT_DISTILLATION*>(_p);
                    cycle_type c = p->get_next_progression_cycle();
                    c = convert_cycles_between_frequencies(c, p->freq_khz, freq_khz);
                    ready_cycle = std::min(c, ready_cycle);
                }
            }
        }

        return std::make_optional(ready_cycle);
    }
    else
    {
        bool all_in_active_set{true};
        cycle_type ready_cycle{0};
        std::for_each(inst->q_begin(), inst->q_end(),
                [this, c, &all_in_active_set, &ready_cycle] (auto qid)
                {
                    QUBIT* q = c->qubits()[qid];
                    all_in_active_set &= this->local_memory_->contains(q);
                    ready_cycle = std::max(ready_cycle, q->cycle_available);
                });
        return all_in_active_set ? std::make_optional(ready_cycle) : std::nullopt;
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

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
