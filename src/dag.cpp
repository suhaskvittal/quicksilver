/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "dag.h"

#include <algorithm>
#include <unordered_set>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

DAG::DAG(size_t _qubit_count)
    :qubit_count(_qubit_count),
    front_layer_(),
    back_instructions_(_qubit_count, nullptr)
{
    front_layer_.reserve(qubit_count);
    node_lookup_table_.reserve(32);
}

DAG::~DAG()
{
    clear();
}

void
DAG::clear()
{
    // delete all nodes and instructions remaining in the DAG:
    // can do this efficiently via DFS:
    std::vector<node_type*> dfss;
    for (const auto& [__unused_inst, node] : front_layer_)
        dfss.push_back(node);

    while (!dfss.empty())
    {
        auto* x = dfss.back();
        dfss.pop_back();

        x->tmp_pred_count_++;
        if (x->tmp_pred_count_ == x->predecessors.size())
        {
            // traverse now -- since we will delete `x`
            for (auto* y : x->dependent)
                dfss.push_back(y);
            delete x->inst;
            delete x;
        }
    }

    // reset the DAG back to an empty state:
    front_layer_.clear();
    std::fill(back_instructions_.begin(), back_instructions_.end(), nullptr);
    node_lookup_table_.clear();
    inst_count_ = 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
DAG::add_instruction(inst_ptr inst, bool memoize)
{
    node_type* x = new node_type{inst};

    // identify dependent instructions in `back_instructions_` and also
    // assign the requisite indices of `back_instructions_`
    std::unordered_set<node_type*> visited;
    for (auto it = inst->q_begin(); it != inst->q_end(); it++)
    {
        auto q = *it;
        if (back_instructions_[q] != nullptr)
        {
            // avoid double counting the dependency
            if (!visited.count(back_instructions_[q]))
            {
                back_instructions_[q]->dependent.push_back(x);
                x->predecessors.push_back(back_instructions_[q]);
            }
            visited.insert(back_instructions_[q]);
        }
        back_instructions_[q] = x;
    }

    // handle the edge case where there are no predecessors 
    // in `back_instructions_` (so add to `front_layer_`)
    if (x->predecessors.empty())
        front_layer_[inst] = x;
    inst_count_++;

    // memoize if necessary
    if (memoize)
        node_lookup_table_[inst] = x;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
DAG::remove_instruction_from_front_layer(inst_ptr inst)
{
    auto node_it = front_layer_.find(inst);
    if (node_it == front_layer_.end())
    {
        std::cerr << "DAG::remove_instruction_from_layer: inst " << *inst 
                    << " was not found in the front layer"
                    << "\nfront layer contents:";
        for (const auto& [inst, node] : front_layer_)
        {
            std::cerr << "\n\tinst = " << *inst 
                        << "\n\t\tnode predecessors =";
            for (auto* x : node->predecessors)
                std::cerr << "\n\t\t\t" << *x->inst;
            std::cerr << "\n\t\tnode dependents =";
            for (auto* x : node->dependent)
                std::cerr << "\n\t\t\t" << *x->inst;
        }
        std::cerr << _die{};
    }
    node_type* head_node = node_it->second;
    front_layer_.erase(node_it);

    // update `inst` dependents:
    for (auto* dep : head_node->dependent)
    {
        auto p_it = std::find(dep->predecessors.begin(), dep->predecessors.end(), head_node);
        dep->predecessors.erase(p_it);
        if (dep->predecessors.empty())
            front_layer_.insert({dep->inst, dep});
    }

    // delete `head_node` from `node_lookup_table_` if it exists there
    auto lut_it = node_lookup_table_.find(inst);
    if (lut_it != node_lookup_table_.end())
        node_lookup_table_.erase(lut_it);

    // finally, if `inst` is also in `back_instructions_`, then we need to clear the entry
    std::for_each(inst->q_begin(), inst->q_end(),
            [this, &head_node] (qubit_type q)
            {
                if (back_instructions_[q] == head_node)
                    back_instructions_[q] = nullptr;
            });

    delete head_node;
    inst_count_--;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<DAG::inst_ptr>
DAG::get_front_layer() const
{
    return get_front_layer_if([] (const auto*) { return true; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
