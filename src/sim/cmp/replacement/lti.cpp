/*
    author: Suhas Vittal
    date:   6 September 2025
*/

#include "sim/cmp/replacement/lti.h"
#include "sim/compute.h"

#include <algorithm>
#include <numeric>

//#define LTI_VERBOSE

namespace sim
{
namespace cmp
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

std::optional<QUBIT>
LTI::select_victim(QUBIT requested, bool is_prefetch)
{
    timeliness_type req_timeliness = compute_instruction_timeliness(requested);

    const auto& clients = cmp->get_clients();
    
    std::optional<QUBIT>  victim{};
    COMPUTE::inst_ptr     victim_inst_head{nullptr};
    timeliness_type       victim_timeliness{-1};

#if defined(LTI_VERBOSE)
    std::cout << "SELECTING VICTIM for " << requested << " -- timeliness = " << req_timeliness << "\n";
#endif

    for (const auto& c : clients)
    {
        // select the qubit with the least timely instruction:
        for (qubit_type qid = 0; qid < static_cast<qubit_type>(c->num_qubits); qid++)
        {
            QUBIT q{c->id, qid};
            if (!is_valid_victim(q, requested))
                continue;

            // exit early if a qubit with an empty instruction window is found (does not have any operations pending)
            if (cmp->has_empty_instruction_window(q))
                return std::make_optional(q);

            const auto& q_win = cmp->get_instruction_window(q);

            // make sure that `q_head` is not earlier than `reference_inst`
            size_t num_uses = cmp->get_num_uses_in_compute(q);
            COMPUTE::inst_ptr q_head = q_win.front();
            timeliness_type q_timeliness = compute_instruction_timeliness(q);

#if defined(LTI_VERBOSE)
            std::cout << "\tQUBIT " << q << " -- timeliness = " << q_timeliness << ", inst number = " << q_head->inst_number << ", num uses = " << num_uses << "\n";
#endif

            if (num_uses == 0)
                continue;

            if (q_timeliness < req_timeliness)
                continue;
            
            // evict based on timeliness and break ties using instruction recency
            bool evict = !victim.has_value()
                            || (q_timeliness > victim_timeliness)
                            || (q_timeliness == victim_timeliness && q_head->inst_number > victim_inst_head->inst_number);
            if (evict)
            {
                victim = q;
                victim_inst_head = q_head;
                victim_timeliness = q_timeliness;
            }
        }
    }

#if defined(LTI_VERBOSE)
    if (victim.has_value())
        std::cout << "SELECTED VICTIM " << *victim << " for " << requested << " -- timeliness = " << victim_timeliness << ", pf timeliness = " << req_timeliness << "\n";
    else
        std::cout << "NO VICTIM FOUND for " << requested << " -- timeliness = " << req_timeliness << "\n";
    std::cout << "\n";
#endif

    return victim;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LTI::timeliness_type
LTI::compute_instruction_timeliness(QUBIT q) const
{
    const auto& win = cmp->get_instruction_window(q);
    const auto& clients = cmp->get_clients();
    const auto& c = clients.at(q.client_id);

    COMPUTE::inst_ptr inst = win.front();

    std::vector<timeliness_type> timeliness(inst->qubits.size());
    std::transform(inst->qubits.begin(), inst->qubits.end(), timeliness.begin(),
                    [this, &inst, &q, client_id=c->id] (qubit_type qid) 
                    { 
                        QUBIT _q{client_id, qid};
                        const auto& _win = this->cmp->get_instruction_window(_q);
                        auto inst_it = std::find(_win.begin(), _win.end(), inst);
                        return std::distance(_win.begin(), inst_it);
                    });
    return *std::max_element(timeliness.begin(), timeliness.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace cmp
}   // namespace sim