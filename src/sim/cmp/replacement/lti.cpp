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
LTI::select_victim(QUBIT requested)
{
    const auto& req_win = cmp->get_instruction_window(requested);
    CLIENT::inst_ptr req_inst_head = req_win.front();

    const auto& clients = cmp->get_clients();
    
    std::optional<QUBIT>  victim{};
    COMPUTE::inst_ptr     victim_inst_head{nullptr};
    timeliness_type       victim_timeliness{-1};

#if defined(LTI_VERBOSE)
    std::cout << "SELECTING VICTIM FOR " << requested << "\n";
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
            const auto& q_win = cmp->get_instruction_window(q);
            if (q_win.empty())
                return std::make_optional(q);

            // make sure that `q_head` is not earlier than `reference_inst`
            COMPUTE::inst_ptr q_head = q_win.front();
            timeliness_type q_timeliness = compute_instruction_timeliness(q);

#if defined(LTI_VERBOSE)
            std::cout << "\tQUBIT " << q << " IS A VALID VICTIM FOR " << requested << ", TIMELINESS = " << q_timeliness << ", DOMINATION( " << *req_inst_head << " , " << *q_head << " ) = " << check_for_domination(req_inst_head, q_head) << "\n";
#endif

//          if (!check_for_domination(req_inst_head, q_head))
//              continue;
            if (q_head->inst_number < req_inst_head->inst_number)
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
        std::cout << "\tSELECTED VICTIM " << *victim << "\n";
    else
        std::cout << "\tNO VICTIM FOUND FOR " << requested << "\n";
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

bool
LTI::check_for_domination(CLIENT::inst_ptr winner, CLIENT::inst_ptr loser)
{
    if (winner == nullptr)
        return false;
    if (loser == nullptr)
        return true;

    auto fw_it = std::find_if(domination_table.begin(), domination_table.end(), 
                        [winner, loser](const auto& e) { return e.has_value() && e->winner == winner && e->loser == loser; });
    auto rv_it = std::find_if(domination_table.begin(), domination_table.end(), 
                        [winner, loser](const auto& e) { return e.has_value() && e->winner == loser && e->loser == winner; });

    if (fw_it == domination_table.end() && rv_it == domination_table.end())
    {
        // create the domination entry in favor of `winner`
        create_domination_entry(winner, loser);
        return true;
    }
    else
    {
        if (fw_it != domination_table.end())
            (*fw_it)->last_access = dom_count++;
        else
            (*rv_it)->last_access = dom_count++;

        return fw_it != domination_table.end();
    }
}

void
LTI::create_domination_entry(CLIENT::inst_ptr winner, CLIENT::inst_ptr loser)
{
    // find an empty entry:
    auto it = std::find_if(domination_table.begin(), domination_table.end(), [] (const auto& e) { return !e.has_value(); });

    if (it == domination_table.end())
    {
        // evict via LRU:
        it = std::min_element(domination_table.begin(), domination_table.end(), 
                [] (const auto& a, const auto& b) { return a->last_access < b->last_access; });
    }

    *it = dominator_table_entry_type{winner, loser, dom_count++};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace repl
}   // namespace cmp
}   // namespace sim