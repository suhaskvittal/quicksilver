/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include "sim/compute_subsystem.h"
#include "sim/memory_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

bool _client_is_done(const CLIENT*, uint64_t simulation_instructions);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::COMPUTE_SUBSYSTEM(double freq_khz,
                                     std::vector<std::string>     client_trace_files,
                                     size_t                       local_memory_capacity,
                                     size_t                       _concurrent_clients,
                                     uint64_t                     _simulation_instructions,
                                     std::vector<T_FACTORY_BASE*> top_level_t_factories,
                                     MEMORY_SUBSYSTEM*            memory_hierarchy,
                                     compute_extended_config      conf)
    :COMPUTE_BASE("compute_subsystem", freq_khz, local_memory_capacity, top_level_t_factories, memory_hierarchy),
    concurrent_clients(_concurrent_clients),
    total_clients(client_trace_files.size()),
    simulation_instructions(_simulation_instructions),
    all_clients_(total_clients),
    active_clients_(concurrent_clients),
    inactive_clients_(total_clients - concurrent_clients),
    client_context_table_(total_clients)
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
        rotation_subsystem_ = new ROTATION_SUBSYSTEM(conf.rpc_freq_khz, 
                                                    conf.rpc_capacity,
                                                    top_level_t_factories,
                                                    memory_hierarchy,
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

        double ipc = mean(c->s_unrolled_inst_done, current_cycle());
        double kips = mean(c->s_unrolled_inst_done / 1000, current_cycle() / (1e3*freq_khz));

        ostrm << "\n\tinstructions completed = " << c->s_unrolled_inst_done
                << "\n\tipc = " << ipc
                << "\n\tkips = " << kips
                << "\n";
    }
}

void
COMPUTE_SUBSYSTEM::print_deadlock_info(std::ostream& ostrm) const
{
    for (auto* f : top_level_t_factories_)
        f->print_deadlock_info(ostrm);

    std::cout << "local memory contents:";
    for (auto* q : local_memory_->contents())
        std::cout << " " << *q;
    std::cout << "\n";

    for (auto* c : active_clients_)
    {
        std::cout << "Client " << static_cast<int>(c->id) << " front layer:\n";
        for (const auto* inst : c->dag()->get_front_layer())
            std::cout << "\t" << *inst << "\n";
    }
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

bool
COMPUTE_SUBSYSTEM::is_rpc_enabled() const
{
    return rotation_subsystem_ != nullptr;
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
                                return do_memory_access(nullptr, q1, q2);
                            else
                                return false;
                        });
        progress += std::distance(it, end);
        context_switch_memory_access_buffer_.erase(it, end);
    }

    /* 3. Handle pending instructions for any active clients */

    // construct the ready qubits map for each client:
    std::vector<ready_qubits_map> ready_qubits_by_client(total_clients);
    for (auto& m : ready_qubits_by_client)
        m.reserve(local_memory_capacity / concurrent_clients);
    for (auto* q : local_memory_->contents())
        if (q->cycle_available <= current_cycle())
            ready_qubits_by_client[q->client_id].insert({q->qubit_id, q});

    // process instructions for each client
    size_t ii{last_used_client_idx_};
    for (size_t i = 0; i < concurrent_clients; i++)
    {
        CLIENT* c = active_clients_[ii];
        progress += fetch_and_execute_instructions_from_client(c, ready_qubits_by_client[c->id]);
        ii++;
        if (ii >= concurrent_clients)
            ii = 0;
    }
    last_used_client_idx_ = (last_used_client_idx_+1) % active_clients_.size();

    /* Update stats (post-execution) */

    const size_t magic_states_after_exec{count_available_magic_states()};
    magic_states_avail_last_cycle_ = magic_states_after_exec;

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

size_t
COMPUTE_SUBSYSTEM::fetch_and_execute_instructions_from_client(CLIENT* c, const ready_qubits_map& ready_qubits)
{
    ready_qubits_map in_mem_qubits;
    in_mem_qubits.reserve(4);

    auto front_layer = c->get_ready_instructions(
                            [&ready_qubits, &in_mem_qubits, &c, cc=current_cycle()] 
                            (const auto* inst)
                            {
                                if (is_memory_access(inst->type))
                                {
                                    // memory accesses need to be handled specially since one of the qubits
                                    // is not in the active set.
                                    const qubit_type incoming_id = inst->qubits[0],
                                                     outgoing_id = inst->qubits[1];

                                    // retrieve pointer to requested qubit from memory
                                    auto* in_memory_qubit = c->qubits()[incoming_id];
                                    in_mem_qubits.insert({incoming_id, in_memory_qubit});
                                    
                                    const bool incoming_ready = in_memory_qubit->cycle_available <= cc;
                                    const bool outgoing_ready = ready_qubits.count(outgoing_id);

                                    return incoming_ready && outgoing_ready;
                                }
                                else
                                {
                                    return std::all_of(inst->q_begin(), inst->q_end(),
                                                [&ready_qubits] (auto q_id) { return ready_qubits.count(q_id) > 0; });
                                }
                            });

    size_t success_count{0};
    for (auto* inst : front_layer)
    {
        // (rpc) if this is the first visit for this instruction, check the `rotation_subsystem_`
        // and do other actions:
        if (is_rotation_instruction(inst->type) && !inst->rpc_has_been_visited)
        {
            QUBIT* q = ready_qubits.at(inst->qubits[0]);
            RPC_LOOKUP_RESULT lookup_result = rpc_lookup_rotation(inst, q);
            if (lookup_result == RPC_LOOKUP_RESULT::RETIRE)
            {
                success_count++;
                c->retire_instruction(inst);
            }
            else if (lookup_result == RPC_LOOKUP_RESULT::IN_PROGRESS)
            {
                /*
                std::cerr << "COMPUTE_SUBSYSTEM::fetch_and_execute_instructions_from_client: todo"
                        << " -- rpc lookup result returned in progress for " << *inst 
                        << ", uops retired = " << rotation_subsystem_->get_rotation_progress(inst)
                        << _die{};
                        */
            }
            else
            {
                rpc_find_and_attempt_allocate_for_future_rotation(c, inst);
                inst->rpc_has_been_visited = true;
            }
            continue;
        }
            
        // RZ and RX gates are a special case since multiple uops of progress can be done
        if (is_rotation_instruction(inst->type) && GL_T_GATE_TELEPORTATION_MAX > 0)
        {
            QUBIT* q = ready_qubits.at(inst->qubits[0]);
            size_t uops_retired_this_cycle = do_rotation_gate_with_teleportation(inst, q);
            success_count += uops_retired_this_cycle;
            if (inst->uops_retired() == inst->uop_count())
                c->retire_instruction(inst);
        }
        else
        {
            auto* executed_inst = (inst->uop_count() == 0) ? inst : inst->current_uop();

            std::array<QUBIT*, 3> operands{};
            std::transform(executed_inst->q_begin(), executed_inst->q_end(), operands.begin(),
                    [&ready_qubits, &in_mem_qubits] (auto q_id) 
                    { 
                        return ready_qubits.count(q_id) > 0 ? ready_qubits.at(q_id) : in_mem_qubits.at(q_id);
                    });
            bool success = execute_instruction(executed_inst, std::move(operands));

            if (success)
            {
                success_count++;
                if (inst->uop_count() == 0 || inst->retire_current_uop())
                    c->retire_instruction(inst);
            }
        }
    }
    return success_count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
COMPUTE_SUBSYSTEM::do_rotation_gate_with_teleportation(inst_ptr inst, QUBIT* q)
{
    int tp_remaining{GL_T_GATE_TELEPORTATION_MAX+1};  // add +1 for initial uop (not counted for teleportation)
    size_t count{0};
    while (tp_remaining--)
    {
        bool success = execute_instruction(inst->current_uop(), {q});
        if (success)
        {
            if (count)
                s_t_gate_teleports++;
            count++;
            if (inst->retire_current_uop())
                break;
        }
        else
        {
            break;
        }
    }
    return count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COMPUTE_SUBSYSTEM::RPC_LOOKUP_RESULT
COMPUTE_SUBSYSTEM::rpc_lookup_rotation(inst_ptr inst, QUBIT* q)
{
    if (!is_rpc_enabled())
        return RPC_LOOKUP_RESULT::NOT_FOUND;
    assert(is_rotation_instruction(inst->type));

    if (rotation_subsystem_->find_and_delete_rotation_if_done(inst))
    {
        bool success = (GL_RNG()&1) > 0;
        q->cycle_available = current_cycle() + 2; // it takes 2 cycles to attempt the teleportation.
        return success ? RPC_LOOKUP_RESULT::RETIRE : RPC_LOOKUP_RESULT::NEEDS_CORRECTION;
    }
    else if (rotation_subsystem_->is_rotation_pending(inst))
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
    constexpr size_t RPC_DAG_LOOKAHEAD_START_LAYER{1};
    constexpr size_t RPC_DAG_LOOKAHEAD_DEPTH{8};

    if (!is_rpc_enabled())
        return;
    assert(is_rotation_instruction(inst->type));

    auto* dependent_inst = c->dag()->find_earliest_dependent_instruction_such_that(
                                    [inst] (inst_ptr x) 
                                    { 
                                        return x != inst 
                                                && is_rotation_instruction(x->type)
                                                && !x->rpc_has_been_visited;
                                    }, 
                                    inst, 
                                    RPC_DAG_LOOKAHEAD_START_LAYER,
                                    RPC_DAG_LOOKAHEAD_DEPTH);
    if (dependent_inst != nullptr)
    {
        assert(dependent_inst != inst);
        rotation_subsystem_->submit_rotation_request(dependent_inst);
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
