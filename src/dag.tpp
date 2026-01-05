/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#include <unordered_set>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class PRED> std::vector<DAG::inst_ptr>
DAG::get_front_layer_if(const PRED& pred) const
{
    std::unordered_set<inst_ptr> front_layer_insts; 
    front_layer_insts.reserve(front_layer_.size());

    for (const auto& [inst, __unused_node] : front_layer_)
        if (pred(inst))
            front_layer_insts.insert(inst);

    return std::vector<inst_ptr>(front_layer_insts.begin(), front_layer_insts.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CALLBACK> void
DAG::for_each_instruction_in_layer_order(const CALLBACK& callback) const
{
    std::unordered_set<node_type*> current_layer;
    for (const auto& [__unused_inst, node] : front_layer_)
        current_layer.insert(node);

    // we will use `pred_table` to identify when to add an instruction to the next layer.
    // condition: `pred_table[node] == node->pred_count`
    std::unordered_map<node_type*, size_t> pred_table;
    while (!current_layer.empty())
    {
        std::unordered_set<node_type*> next_layer;
        next_layer.reserve(current_layer.size());
        for (auto* x : current_layer)
        {
            callback(x->inst);

            // traverse to neighbors:
            for (node_type* y : x->dependent)
                if ((++pred_table[y]) == y->pred_count)
                    next_layer.insert(y);
        }
        current_layer = std::move(next_layer);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
