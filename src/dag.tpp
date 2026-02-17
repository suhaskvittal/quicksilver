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
    std::vector<inst_ptr> front_layer_insts;
    front_layer_insts.reserve(front_layer_.size());
    for (const auto& [inst, __unused_node] : front_layer_)
        if (pred(inst))
            front_layer_insts.push_back(inst);
    return front_layer_insts;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CALLBACK> void
DAG::for_each_instruction_in_layer_order(const CALLBACK& callback, size_t min_layer, size_t max_layer) const
{
    return generic_operate_on_nodes_in_layer_order_c(
                        [&callback] (node_type* x) { callback(x->inst); }, 
                        min_layer, 
                        max_layer);
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class PRED> std::pair<typename DAG::inst_ptr, size_t>
DAG::find_earliest_dependent_instruction_such_that(const PRED& pred, 
                                                    inst_ptr source, 
                                                    size_t min_layer,
                                                    size_t max_layer) const
{
    node_type* source_node = front_layer_.at(source);
    std::vector<node_type*> curr_layer(source_node->dependent);

    // use an `std::unordered_set` to avoid duplicates when setting `curr_layer`
    std::unordered_set<node_type*> next_layer_set;
    next_layer_set.reserve(curr_layer.size());

    size_t layer_count{0};
    while (layer_count < max_layer)
    {
        for (auto* x : curr_layer)
        {
            next_layer_set.insert(x->dependent.begin(), x->dependent.end());
            if (layer_count >= min_layer && pred(x->inst))
                return std::make_pair(x->inst, layer_count);
        }
        curr_layer.assign(next_layer_set.begin(), next_layer_set.end());
        next_layer_set.clear();
        layer_count++;
    }
    
    return std::make_pair(nullptr, 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class PRED> size_t
DAG::contract_instructions_such_that(const PRED& pred, size_t min_layer, size_t max_layer)
{
    size_t num_deleted{0};
    generic_operate_on_nodes_in_layer_order(
            [this, &pred, &num_deleted] (node_type* parent)
            {
                if (parent->dependent.size() != 1)
                    return;
                auto* child = parent->dependent[0];
                if (pred(parent, child))
                {
                    const bool child_is_back_inst = child->dependent.empty();

                    // merge dependent lists
                    std::unordered_set<node_type*> dependent_set(parent->dependent.begin(), 
                                                                 parent->dependent.end());
                    for (auto* x : child->dependent)
                    {
                        if (!dependent_set.count(x))
                        {
                            parent->dependent.push_back(x);
                            x->pred_count++;
                        }
                    }

                    // handle the case where the child is at the end of the DAG:
                    if (child_is_back_inst)
                    {
                        auto it = std::find(back_instructions_.begin(), back_instructions_.end(), child);
                        *it = parent;
                    }

                    // mark parent as `deleteable` (cannot delete yet since the template function needs
                    // `parent` to traverse through the DAG)
                    parent->deleteable = true;

                    delete child;
                    num_deleted++;
                }
            },
            min_layer,
            max_layer);

    num_deleted += delete_any_deletable_nodes();  // this should clean up parents
    return num_deleted;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CALLBACK> void
DAG::generic_operate_on_nodes_in_layer_order(const CALLBACK& callback, size_t min_layer, size_t max_layer)
{
    // update iteration generation so we know when to reset predecessor
    iteration_generation_++;
    const size_t gen = iteration_generation_;

    std::vector<node_type*> current_layer;
    current_layer.reserve(front_layer_.size());
    for (const auto& [_, node] : front_layer_)
        current_layer.push_back(node);

    size_t layer_count{0};
    while (!current_layer.empty() && layer_count < max_layer)
    {
        std::vector<node_type*> next_layer;
        next_layer.reserve(current_layer.size());

        for (auto* x : current_layer)
        {
            if (layer_count >= min_layer)
                callback(x);

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

template <class CALLBACK> void
DAG::generic_operate_on_nodes_in_layer_order_c(const CALLBACK& callback, size_t min_layer, size_t max_layer) const
{
    // update iteration generation so we know when to reset predecessor
    iteration_generation_++;
    const size_t gen = iteration_generation_;

    std::vector<node_type*> current_layer;
    current_layer.reserve(front_layer_.size());
    for (const auto& [_, node] : front_layer_)
        current_layer.push_back(node);

    size_t layer_count{0};
    while (!current_layer.empty() && layer_count < max_layer)
    {
        std::vector<node_type*> next_layer;
        next_layer.reserve(current_layer.size());

        for (auto* x : current_layer)
        {
            if (layer_count >= min_layer)
                callback(x);

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
