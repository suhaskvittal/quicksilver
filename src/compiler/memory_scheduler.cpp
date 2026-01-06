/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/memory_scheduler.h"

namespace compile
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
        inst_ptr m = new INSTRUCTION{INSTRUCTION::TYPE::MSWAP, {q, *it}};
        out.memory_accesses.push_back(m);
        out.active_set.erase(it);
        out.active_set.insert(q);
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
read_instructions_into_dag(dag_ptr& dag, generic_strm_type& istrm, size_t until_capacity)
{
    while (dag->inst_count() < until_capacity && !generic_strm_eof(istrm))
    {
        inst_ptr inst = read_instruction_from_stream(istrm);
        dag->add_instruction(inst);
    }
}

bool
instruction_is_ready(inst_ptr inst, const active_set_type& active_set)
{
    return is_software_instruction(inst->type)
           || std::all_of(inst->q_begin(), inst->q_end(), [&active_set] (auto q) { return active_set.count(q) > 0; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace compile
