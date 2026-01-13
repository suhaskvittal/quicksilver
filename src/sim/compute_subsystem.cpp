/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include "sim/compute_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper function declarations:
 * */

namespace
{

void _update_available_cycle(std::vector<QUBIT*>, cycle_type);

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::print_progress(std::ostream& ostrm) const
{
    std::cout << "cycle " << current_cycle() << " -------------------------------------------------------------\n";
}

void
COMPUTE_SUBSYSTEM::print_deadlock_info(std::ostream& ostrm) const
{
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COMPUTE_SUBSYSTEM::context_switch(CLIENT* in, CLIENT* out)
{
    auto c_it = std::find(active_clients_.begin(), active_clients_.end(), out);
    if (c_it == active_clients_.end())
        std::cerr << "COMPUTE_SUBSYSTEM::context_switch: tried to context switch out an inactive client" << _die{};
    *c_it = in;

    /* 1. Populate `context_switch_memory_access_buffer_` */

    // need to get lists of active qubits for `in` and `out`
    const context_type& in_ctx = client_context_table_[in->id];
    std::vector<QUBIT*> out_active_qubits;
    out_active_qubits.reserve(qubit_capacity / concurrent_clients);
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
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const std::unique_ptr<STORAGE>&
COMPUTE_SUBSYSTEM::local_memory() const
{
    return local_memory_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
COMPUTE_SUBSYSTEM::operate()
{
    long progress{0};

    /* 1. Handle context switch memory accesses */

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

    /* 2. Handle pending instructions for any active clients */

    // construct the ready qubits map for each client:
    std::vector<ready_qubits_map> ready_qubits_by_client(total_clients);
    for (auto& m : ready_qubits_by_client)
        m.reserve(qubit_capacity);
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
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
COMPUTE_SUBSYSTEM::fetch_and_execute_instructions_from_client(CLIENT* c, const ready_qubits_map& ready_qubits)
{
    auto front_layer = c->get_ready_instructions(
                            [&ready_qubits] (const auto* inst)
                            {
                                if (is_memory_access(inst->type))
                                {
                                    // memory accesses need to be handled specially since one of the qubits
                                    // is not in the active set.
                                    const qubit_type incoming_id = inst->qubits[0],
                                                     outgoing_id = inst->qubits[1];
                                    const bool incoming_ready = /* todo */; 
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
        auto* executed_inst = (inst->uop_count() == 0) ? inst : inst->current_uop();

        std::array<QUBIT*, 3> operands{};
        std::transform(executed_inst->q_begin(), executed_inst->q_end(), operands.begin(),
                [&ready_qubits] (auto q_id) { return ready_qubits.at(q_id); });
        bool success = execute_instruction(executed_inst, std::move(operands));
        if (success)
        {
            success_count++;
            if (inst->uop_count() == 0 || inst->retire_current_uop())
                c->retire_instruction(inst);
        }
    }
    return success_count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::execute_instruction(inst_ptr inst, std::array<QUBIT*, 3>&& args)
{
    if (is_software_instruction(inst->type))
        return true;

    switch (inst->type)
    {
    case INSTRUCTION::TYPE::H:
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SX:
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SXDG:
        return do_h_or_s_gate(inst, args[0]);

    case INSTRUCTION::TYPE::CX:
    case INSTRUCTION::TYPE::CZ:
        return do_cx_like_gate(inst, args[0], args[1]);

    case INSTRUCTION::TYPE::T:
    case INSTRUCTION::TYPE::TX:
    case INSTRUCTION::TYPE::TDG:
    case INSTRUCTION::TYPE::TXDG:
        return do_t_like_gate(inst, args[0]);

    case INSTRUCTION::TYPE::MSWAP:
        return do_memory_access(inst, args[0], args[1]);

    default:
        std::cerr << "COMPUTE_SUBSYSTEM::execute_instruction: unknown instruction: " << *inst << _die{};
    }

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::do_h_or_s_gate(inst_ptr inst, QUBIT* q)
{
    _update_available_cycle({q}, current_cycle()+2);
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::do_cx_like_gate(inst_ptr inst, QUBIT* q1, QUBIT* q2)
{
    _update_available_cycle({q1, q2}, current_cycle()+2);
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::do_t_like_gate(inst_ptr inst, QUBIT* q)
{
    // search for an available magic state:
    auto f_it = std::find_if(top_level_t_factories_.begin(), top_level_t_factories_.end(),
                        [] (const auto* f) { return f->buffer_occupancy() > 0; });
    if (f_it == top_level_t_factories_.end())
        return false;

    (*f_it)->consume(1);
    cycle_type latency = (GL_RNG() & 1) ? 4 : 2;
    _update_available_cycle({q}, current_cycle() + latency);
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
COMPUTE_SUBSYSTEM::do_memory_access(inst_ptr inst, QUBIT* ld, QUBIT* st)
{
    auto result = memory_hierarchy_->do_memory_access(ld, st);
    if (result.success)
    {
        // need to convert storage latency to compute cycles:
        cycle_type latency = convert_cycles(result.latency, result.storage_freq_khz, freq_khz);
        local_memory_->do_memory_access(st, ld);
        _update_available_cycle({ld, st}, current_cycle() + latency);
    }
    return result.success;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTION DEFINITIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_update_available_cycle(std::vector<QUBIT*> qubits, cycle_type c)
{
    for (auto* q : qubits)
        q->cycle_available = c;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
