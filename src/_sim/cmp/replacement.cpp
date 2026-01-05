#include "sim/cmp/replacement.h"
#include "sim/client.h"
#include "sim/compute.h"

#include <algorithm>

namespace sim
{
namespace cmp
{

REPLACEMENT_POLICY_BASE::REPLACEMENT_POLICY_BASE(COMPUTE* c) 
    :cmp(c)
{}

bool
REPLACEMENT_POLICY_BASE::is_valid_victim(QUBIT q) const
{
    return cmp->is_present_in_compute(q);
}

bool
REPLACEMENT_POLICY_BASE::is_valid_victim(QUBIT q, QUBIT requested) const
{
    if (cmp->has_empty_instruction_window(q))
        return is_valid_victim(q);

    const auto& req_win = cmp->get_instruction_window(requested);
    const auto& q_win = cmp->get_instruction_window(q);

    COMPUTE::inst_ptr ref_inst = req_win.front();
    COMPUTE::inst_ptr q_inst = q_win.front();

    bool client_match = (q.client_id == requested.client_id);
    bool inst_earlier = q_inst->inst_number <= ref_inst->inst_number;
    bool inst_still_has_uops = q_inst->uop_completed < q_inst->num_uops;

    return is_valid_victim(q) && !(client_match && inst_earlier) && !inst_still_has_uops;
}

}   // namespace cmp
}   // namespace sim