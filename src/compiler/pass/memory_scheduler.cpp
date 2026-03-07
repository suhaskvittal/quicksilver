/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/pass/memory_scheduler.h"

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

result_type
transform_active_set(const active_set_type& current, const active_set_type& target)
{
    result_type out{};
    out.active_set = current;  // create copy of `current` -- we will edit this
    out.unused_bandwidth = current.size() - target.size();  // BW >= 0

    for (qubit_type q : target)
    {
        if (out.active_set.count(q))
            continue;

        // select victim (not in `target`)
        auto it = std::find_if(out.active_set.begin(), out.active_set.end(),
                        [&target] (qubit_type q) { return !target.count(q); });
        if (it == out.active_set.end())
            std::cerr << "memory_scheduler::transform_active_set: could not find victim" << _die{};
        inst_ptr m = new INSTRUCTION{INSTRUCTION::TYPE::COUPLED_LOAD_STORE, {q, *it}};
        out.memory_accesses.push_back(m);

        out.active_set.erase(it);
        out.active_set.insert(q);
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace pass
}  // namespace compiler
