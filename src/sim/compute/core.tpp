/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace sim
{
namespace compute
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class COMPUTE_MODEL_PTR, class ITER> size_t
fetch_and_execute_instruction_from_client(COMPUTE_MODEL_PTR& subsystem, client_ptr& c, ITER q_begin, ITER q_end)
{
    // identify all ready qubits belonging to `c` in `active_qubits_`
    std::unordered_map<qubit_type, QUBIT*> ready_qubits;
    ready_qubits.reserve(std::distance(q_begin, q_end));
    std::for_each(q_begin, q_end,
            [&subsystem, &ready_qubits] (const QUBIT* q)
            {
                if (q->client_id == c->id && q->cycle_available <= subsystem->current_cycle)
                    ready_qubits.insert({q->qubit_id, q});
            });
    
    // get all instructions in `c`'s front layer from `ready_qubits`
    auto front_layer = c->get_ready_instructions(
                                [&ready_qubits] (auto* inst)
                                {
                                    return std::all_of(inst->q_begin(), inst->q_end(),
                                            [&ready_qubits] (auto q) { return ready_qubits.count(q) > 0; });
                                });
    if (front_layer.empty())
        return 0;
    
    // try to execute instructions:
    size_t                success_count{0};
    std::vector<inst_ptr> retireable;
    for (auto* inst : front_layer)
    {
        // retrieve the pointers to the actual qubit instances:
        std::array<QUBIT*, 3> operands{};
        std::transform(inst->q_begin(), inst->q_end(), operands.begin(),
                [&ready_qubits] (qubit_type q_id) { return ready_qubits[q_id]; });
        bool success = subsystem->execute_instruction(inst, operands);
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

} // namespace compute
} // namespace sim
