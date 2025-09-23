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
    const auto& q_win = cmp->get_instruction_window(q);
    const auto& req_win = cmp->get_instruction_window(requested);
    if (q_win.empty())
        return is_valid_victim(q);

    COMPUTE::inst_ptr ref_inst = req_win.front();
    COMPUTE::inst_ptr q_inst = q_win.front();

    bool client_match = (q.client_id == requested.client_id);
    bool inst_earlier = q_inst->inst_number < ref_inst->inst_number;

    return is_valid_victim(q) && !(client_match && inst_earlier);
}

}   // namespace cmp
}   // namespace sim