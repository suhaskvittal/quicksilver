/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#include <cassert>
#include <set>
#include <unordered_set>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Pred> std::vector<DAG::inst_ptr>
DAG::get_front_layer_if(const Pred& pred) const
{
    std::vector<inst_ptr> front_layer_insts;
    front_layer_insts.reserve(front_layer_.size());
    for (const auto& [inst, __unused_node] : front_layer_)
        if (pred(inst))
            front_layer_insts.push_back(inst);
    return front_layer_insts;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Callback> void
DAG::for_each_instruction_in_layer_order(const Callback& callback, size_t min_layer, size_t max_layer) const
{
    return _generic_operate_on_nodes_in_layer_order(
                        [&callback] (node_type* x, size_t layer) { callback(x->inst, layer); }, 
                        min_layer, 
                        max_layer);
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Pred> std::pair<typename DAG::inst_ptr, size_t>
DAG::find_earliest_dependent_instruction_such_that(const Pred& pred, 
                                                    inst_ptr source, 
                                                    size_t min_layer,
                                                    size_t max_layer) const
{
    auto f_it = front_layer_.find(source);
    assert(f_it != front_layer_.end());
    return find_earliest_dependent_helper(pred, f_it->second, min_layer, max_layer);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Pred> std::pair<typename DAG::inst_ptr, size_t>
DAG::find_earliest_dependent_instruction_from_memoized_instruction_such_that(const Pred& pred, 
                                                                                inst_ptr source, 
                                                                                size_t min_layer,
                                                                                size_t max_layer) const
{
    auto lut_it = node_lookup_table_.find(source);
    assert(lut_it != node_lookup_table_.end());
    return find_earliest_dependent_helper(pred, lut_it->second, min_layer, max_layer);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Pred> std::pair<typename DAG::inst_ptr, size_t>
DAG::find_earliest_dependent_helper(const Pred& pred, node_type* source_node, size_t min_layer, size_t max_layer) const
{
    iteration_generation_++;
    const size_t gen = iteration_generation_;
    std::vector<node_type*> curr_layer(source_node->dependent);

    // use a sorted set to avoid duplicates and ensure deterministic iteration order
    std::vector<node_type*> next_layer;

    size_t layer_count{0};
    while (layer_count < max_layer)
    {
        for (auto* x : curr_layer)
        {
            if (layer_count >= min_layer && pred(x->inst, layer_count))
                return std::make_pair(x->inst, layer_count);

            // update dependents
            for (auto* y : x->dependent)
            {
                if (y->last_generation_ != gen)
                {
                    y->last_generation_ = gen;
                    y->tmp_pred_count_ = 0;
                }
                if ((++y->tmp_pred_count_) == y->pred_count)
                    next_layer.push_back(y);
            }
        }
        curr_layer = std::move(next_layer);
        next_layer.clear();
        layer_count++;
    }
    
    return std::make_pair(nullptr, 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Callback> void
DAG::_generic_operate_on_nodes_in_layer_order(this auto& self, 
                                                const Callback& callback, 
                                                size_t min_layer, 
                                                size_t max_layer)
{
    // update iteration generation so we know when to reset predecessor
    self.iteration_generation_++;
    const size_t gen = self.iteration_generation_;

    std::vector<node_type*> current_layer;
    current_layer.reserve(self.front_layer_.size());
    for (const auto& [_, node] : self.front_layer_)
        current_layer.push_back(node);

    size_t layer_count{0};
    while (!current_layer.empty() && layer_count < max_layer)
    {
        std::vector<node_type*> next_layer;
        next_layer.reserve(current_layer.size());

        for (auto* x : current_layer)
        {
            if (layer_count >= min_layer)
                callback(x, layer_count);

            for (node_type* y : x->dependent)
            {
                if (y->last_generation_ != gen)
                {
                    y->last_generation_ = gen;
                    y->tmp_pred_count_ = 0;
                }
                if ((++y->tmp_pred_count_) == y->pred_count)
                    next_layer.push_back(y);
            }
        }
        current_layer = std::move(next_layer);
        layer_count++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
