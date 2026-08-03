/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/memory_scheduler/impl.h"

#include <algorithm>
#include <unordered_map>

namespace compile
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

result_type
eif(const active_set_type& active_set, const dag_ptr& dag, config_type conf)
{
    active_set_type target_active_set;
    target_active_set.reserve(conf.active_set_capacity);

    // the instruction we will look at are only front layer instructions.
    // compute the `priority` score of each instruction (number of arguments currently in `active_set`)
    auto front_layer = dag->get_front_layer();
    std::unordered_map<inst_ptr, size_t> priority_score_map;
    priority_score_map.reserve(front_layer.size());
    for (auto* inst : front_layer)
    {
        size_t s = std::count_if(inst->q_begin(), inst->q_end(),
                                [&active_set] (qubit_type q) { return active_set.count(q); });
        priority_score_map[inst] = s;
    }

    // sort the front layer by score and build the `target_active_set`
    std::sort(front_layer.begin(), front_layer.end(),
            [&priority_score_map] (auto* a, auto* b)
            {
                return priority_score_map.at(a) > priority_score_map.at(b);
            });
    for (auto* inst : front_layer)
    {
        if (target_active_set.size() >= conf.active_set_capacity)
            break;
        if (get_inst_qubit_count(inst->type) > conf.active_set_capacity - target_active_set.size())
            continue;
        target_active_set.insert(inst->q_begin(), inst->q_end());
    }

    std::vector<double> scores(dag->qubit_count, 0.0);
    dag->for_each_instruction_in_layer_order(
            [&scores, &target_active_set] (auto* inst)
            {
                size_t in_target = std::count_if(inst->q_begin(), inst->q_end(),
                                        [&target_active_set] (qubit_type q) { return target_active_set.count(q) > 0; });
                std::for_each(inst->q_begin(), inst->q_end(), 
                        [&scores, x= in_target] (auto q) { return scores[q] += x; });
            },
            0,
            conf.eif_lookahead_depth);

    return transform_active_set(active_set, target_active_set, scores);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace compile
