/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

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
DAG::for_each_instruction_in_layer_order(const CALLBACK& callback, size_t max_layer) const
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
            callback(x->inst);

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
