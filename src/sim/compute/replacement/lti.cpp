/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#include "sim/compute/replacement/lti.h"
#include "sim/compute.h"

#include <algorithm>
#include <numeric>

namespace sim
{
namespace compute
{
namespace repl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LTI::LTI(COMPUTE* c) 
    :REPLACEMENT_POLICY_BASE(c)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LTI::output_type
LTI::select_victim(const CLIENT::qubit_info_type& requested)
{
    uint64_t ref_inst_number = requested.inst_window.front()->inst_number;

    const auto& clients = cmp->clients();
    
    output_type      victim{nullptr};
    CLIENT::inst_ptr victim_inst_head{nullptr};
    timeliness_type  victim_timeliness{-1};

    for (auto& c : clients)
    {
        // select the qubit with the least timely instruction:
        for (auto& q : c->qubits)
        {
            if (!is_valid_victim(q, requested))
                continue;

            // exit early if a qubit with an empty instruction window is found (does not have any operations pending)
            if (q.inst_window.empty())
                return &q;

            // make sure that `q_head` is not earlier than `reference_inst`
            auto* q_head = q.inst_window.front();
            if (q_head->inst_number < ref_inst_number)
                continue;

            timeliness_type q_timeliness = compute_instruction_timeliness(q);
            
            // evict based on timeliness and break ties using instruction recency
            bool evict = (q_timeliness > victim_timeliness)
                            || (q_timeliness == victim_timeliness && q_head->inst_number > victim_inst_head->inst_number);
            if (evict)
            {
                victim = &q;
                victim_inst_head = q_head;
                victim_timeliness = q_timeliness;
            }
        }
    }

    return victim;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LTI::timeliness_type
LTI::compute_instruction_timeliness(const CLIENT::qubit_info_type& q) const
{
    const CLIENT::inst_ptr& inst = q.inst_window.front();
    const auto& clients = cmp->clients();

    std::vector<timeliness_type> timeliness(inst->qubits.size());
    std::transform(inst->qubits.begin(), inst->qubits.end(), timeliness.begin(),
                    [&inst, &q, &clients] (qubit_type qid) 
                    { 
                        const auto& c = clients.at(q.memloc_info.client_id);

                        const auto& _q = c->qubits[qid];
                        auto inst_it = std::find(_q.inst_window.begin(), _q.inst_window.end(), inst);
                        return std::distance(_q.inst_window.begin(), inst_it);
                    });
    return std::reduce(timeliness.begin(), timeliness.end(), timeliness_type{0});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace compute
}   // namespace sim