/*
    author: Suhas Vittal
    date:   24 September 2025   
*/

#include "compiler/memopt/impl.h"

namespace memopt
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

IMPL_BASE::IMPL_BASE(size_t cmp_count)
    :cmp_count(cmp_count)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

IMPL_BASE::result_type
IMPL_BASE::transform_working_set_into(const ws_type& curr, const ws_type& target, const std::vector<double>& qubit_scores)
{
    result_type result;
    // copy `curr` for now -- we will modify this here
    result.working_set = curr;
    result.unused_bandwidth = curr.size() - target.size();

    // now, for any qubit in `target` that is not in `curr`, we need to do some eviction of an entry in `curr`
    for (qubit_type q : target)
    {
        if (result.working_set.count(q))
            continue;
        
        // select valid victim with lowest score
        auto v_it = result.working_set.end();
        for (auto it = result.working_set.begin(); it != result.working_set.end(); it++)
        {
            // do not evict a qubit that is in `target` -- we are trying to install these qubits!
            if (target.count(*it))
                continue;
            
            if (v_it == curr.end() || qubit_scores[*v_it] < qubit_scores[*it])
                v_it = it;
        }

        if (v_it == result.working_set.end())
            break;

        // add memory instruction:
        inst_ptr mswap = new INSTRUCTION(INSTRUCTION::TYPE::MSWAP, {q, *v_it});
        result.memory_instructions.push_back(mswap);

        result.working_set.erase(v_it);
        result.working_set.insert(q);
    }

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memopt
