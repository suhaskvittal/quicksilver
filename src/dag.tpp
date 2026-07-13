/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#include <cassert>
#include <set>
#include <unordered_set>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr bool
search_is_breadth_first(GraphSearchType s)
{
    return s == GraphSearchType::BreadthFirst || s == GraphSearchType::InvertedBreadthFirst;
}

constexpr bool
search_is_inverted(GraphSearchType s)
{
    return s == GraphSearchType::InvertedDepthFirst || s == GraphSearchType::InvertedBreadthFirst;
}

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
DAG::for_each_instruction_in_layer_order(size_t min_layer, size_t max_layer, const Callback& callback) const
{
    return operate_on_nodes_in_layer_order(
                        min_layer,
                        max_layer,
                        [&callback] (node_type* x, size_t layer) { callback(x->inst, layer); });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <GraphSearchType S, class Callback> void
DAG::search(inst_ptr src, const Callback& callback) const
{
    auto node_it = front_layer_.find(src);
    if (node_it == front_layer_.end())
        node_it = node_lookup_table_.find(src);
    if (node_it == node_lookup_table_.end())
        std::cerr << "DAG::search: input instruction \"" << *src << "\" is not in front layer or memoized" << _die{};

    node_type* src_node = node_it->second;
    std::deque<node_type*> buf{src_node};
    std::unordered_set<node_type*> visited;
    while (buf.size() > 0)
    {
        node_type* x;
        if constexpr (search_is_breadth_first(S))
        {
            x = buf.front();
            buf.pop_front();
        }
        else
        {
            x = buf.back();
            buf.pop_back();
        }

        if (visited.count(x))
            continue;
        visited.insert(x);
        const bool skip = callback(x->inst);
        if (skip)
            continue;
        const auto& neighbors = search_is_inverted(S) ? x->predecessors : x->dependent;
        for (auto* y : neighbors)
            buf.push_back(y);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class Callback> void
DAG::operate_on_nodes_in_layer_order(this auto& self,
                                                size_t min_layer,
                                                size_t max_layer,
                                                const Callback& callback)
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
                y->tmp_pred_count_++;
                if (y->tmp_pred_count_ == y->predecessors.size())
                    next_layer.push_back(y);
            }
        }
        current_layer = std::move(next_layer);
        layer_count++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
