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

    // construct the ready qubits map for each client:
    std::array<ready_qubits_map, 8> ready_qubits_by_client(active_clients_.size());
    for (auto* q : local_memory_->contents())
        if (q->cycle_available <= current_cycle())
            ready_qubits_by_client[q->client_id].insert({q->qubit_id, q});

    // process instructions for each client
    size_t ii{last_used_client_idx_};
    for (size_t i = 0; i < active_clients_.size(); i++)
    {
        CLIENT* c = active_clients_[ii];
        progress += fetch_and_execute_instructions_from_client(c, ready_qubits_by_client[c->id]);
        ii++;
        if (ii >= active_clients_.size())
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
