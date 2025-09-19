#include "sim/compute/replacement.h"
#include "sim/client.h"
#include "sim/compute.h"

#include <algorithm>

namespace sim
{
namespace compute
{

REPLACEMENT_POLICY_BASE::REPLACEMENT_POLICY_BASE(COMPUTE* c) 
    :cmp(c)
{}

bool
REPLACEMENT_POLICY_BASE::is_valid_victim(const CLIENT::qubit_info_type& q) const
{
    return q.memloc_info.where == MEMINFO::LOCATION::COMPUTE
            && q.memloc_info.t_free <= cmp->current_cycle();
}

bool
REPLACEMENT_POLICY_BASE::is_valid_victim(const CLIENT::qubit_info_type& q, const CLIENT::qubit_info_type& requested) const
{
    if (q.inst_window.empty())
        return is_valid_victim(q);

    const CLIENT::inst_ptr& ref_inst = requested.inst_window.front();
    const CLIENT::inst_ptr& q_inst = q.inst_window.front();

    bool client_match = (q.memloc_info.client_id == requested.memloc_info.client_id);
    bool inst_earlier_or_same_time = (q_inst->inst_number <= ref_inst->inst_number);

    return is_valid_victim(q) && !(client_match && inst_earlier_or_same_time);
}

}   // namespace compute
}   // namespace sim