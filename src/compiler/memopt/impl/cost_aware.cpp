/*
    author: Suhas Vittal
    date:   30 September 2025
*/

#include "compiler/memopt/impl/cost_aware.h"
#include "cost_aware.h"
#include <memory>

namespace memopt
{
namespace impl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COST_AWARE::result_type
COST_AWARE::emit_memory_instructions(const ws_type& current_working_set, const inst_array& pending_inst, 
                                     const inst_window_map& inst_windows)
{
    std::vector<WORKING_SET_TREE_NODE*> entry_points(num_qubits, nullptr);
    for (qubit_type q = 0; q < num_qubits; q++)
    {
        WORKING_SET_TREE_NODE* node = new WORKING_SET_TREE_NODE;
        node->working_set.insert(q);
        node->memory_cost = current_working_set.count(q) ? 0 : 1;
        entry_points[q] = node;
    }

    // build DP tree:
    size_t num_inst_to_read = std::min(size_t{512*num_qubits}, pending_inst.size());
    for (size_t i = 0; i < num_inst_to_read; i++)
        update_dp_tree(entry_points, pending_inst[i]);

    // with the DP tree built, we can now compute the best working set
    ws_type new_working_set;
    double ws_score;
    std::vector<double> qubit_scores;
    std::tie(new_working_set, ws_score, qubit_scores) = compute_best_working_set(entry_points);

    INSTRUCTION::TYPE inst_type = INSTRUCTION::TYPE::MSWAP;
    if (num_scores > 12.0 && ws_score < 0.5*(tot_score/num_scores))
        inst_type = INSTRUCTION::TYPE::MSWAP_D;

    num_scores += 1.0;
    tot_score += ws_score;

    // deallocate the DP tree:
    working_set_tree_free(std::move(entry_points));

    return transform_working_set_into(current_working_set, new_working_set, qubit_scores, inst_type);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
COST_AWARE::update_dp_tree(std::vector<WORKING_SET_TREE_NODE*>& entry_points, inst_ptr inst)
{
    // get bottommost nodes for qubit
    std::vector<WORKING_SET_TREE_NODE*> latest_nodes;
    latest_nodes.reserve(inst->qubits.size());
    for (qubit_type q : inst->qubits)
    {
        auto* start_node = entry_points[q];
        auto* latest_node = working_set_tree_traverse(start_node);
        // check if `latest_node` is already in `latest_nodes`
        if (std::find(latest_nodes.begin(), latest_nodes.end(), latest_node) == latest_nodes.end())
            latest_nodes.push_back(latest_node);
    }

    bool any_frozen = std::any_of(latest_nodes.begin(), latest_nodes.end(), [](auto* node) { return node->is_frozen; });
    if (any_frozen)
    {
        // freeze all nodes
        for (auto* node : latest_nodes)
            node->is_frozen = true;
        return;
    }

    if (latest_nodes.empty())
        throw std::runtime_error("no latest nodes");

    // all nodes should be disjoint at this point:
    if (latest_nodes.size() == 1)
    {
        // we don't have to create a new node -- just update the existing one
        auto* node = latest_nodes[0];
        // only need to update compute value -- working set and memory cost are the same
        node->compute_value += get_compute_value_of_instruction(inst->type);
    }
    else
    {
        // we need to coalesce the nodes in `latest_nodes`
        // first compute the joint working set:
        ws_type joint_working_set;
        for (auto* node : latest_nodes)
            joint_working_set.insert(node->working_set.begin(), node->working_set.end());

        // ensure `joint_working_set` fits within the compute capacity:
        if (joint_working_set.size() > cmp_count)
        {
            // freeze all nodes and do not coalesce:
            for (auto* node : latest_nodes)
                node->is_frozen = true;
            return;
        }

        // create a new node for the joint working set
        auto* new_node = new WORKING_SET_TREE_NODE;
        new_node->working_set = joint_working_set;
        new_node->compute_value = std::transform_reduce(latest_nodes.begin(), latest_nodes.end(),
                                                            get_compute_value_of_instruction(inst->type),
                                                            std::plus<int64_t>{},
                                                            [](auto* node) { return node->compute_value; });
        new_node->memory_cost = std::transform_reduce(latest_nodes.begin(), latest_nodes.end(),
                                                            int64_t{0},
                                                            std::plus<int64_t>{},
                                                            [](auto* node) { return node->memory_cost; });
        
        // set the child of the `latest_nodes` to `new_node`
        for (auto* node : latest_nodes)
            node->child = new_node;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

COST_AWARE::working_set_search_result_type
COST_AWARE::compute_best_working_set(const std::vector<WORKING_SET_TREE_NODE*>& entry_points)
{
    // 1. categorize all nodes with working sets of size 2 to `cmp_count`
    std::vector<std::vector<WORKING_SET_TREE_NODE*>> nodes_by_ws_size(cmp_count);
    // do dfs to find all nodes:
    std::unordered_set<WORKING_SET_TREE_NODE*> visited;
    std::vector<WORKING_SET_TREE_NODE*> dfs_stack(entry_points);
    while (dfs_stack.size() > 0)
    {
        auto* node = dfs_stack.back();
        dfs_stack.pop_back();

        if (visited.count(node))
            continue;
        visited.insert(node);

        size_t idx = node->working_set.size() - 1;
        nodes_by_ws_size[idx].push_back(node);

        if (node->child != nullptr)
            dfs_stack.push_back(node->child);
    }
    
    // 2. for a working set of size `k`, we will try all combinations with working sets of size `cmp_count-k` if any exist
    ws_type best_working_set;
    int64_t best_compute_value{0};
    int64_t best_memory_cost{0};
    double best_score{-1.0};  // higher score is better
    for (size_t k = cmp_count; k >= cmp_count/2; k--)
    {
        for (const auto* x : nodes_by_ws_size[k-1])
        {
            // first, check if `x` by itself is good enough:
            double xs = score_working_set(x->compute_value, x->memory_cost);
            if (xs > best_score)
            {
                best_score = xs;
                best_working_set = x->working_set;
                best_compute_value = x->compute_value;
                best_memory_cost = x->memory_cost;
            }

            if (k == cmp_count)
                continue;

            for (const auto* y : nodes_by_ws_size[cmp_count-k-1])
            {
                // make sure the working sets are disjoint
                bool have_common_qubit = std::any_of(x->working_set.begin(), x->working_set.end(), 
                                                        [y](qubit_type q) { return y->working_set.count(q); });
                if (have_common_qubit)
                    continue;

                // compute the score of the combined set:
                double s = score_working_set(x->compute_value + y->compute_value, x->memory_cost + y->memory_cost);
                if (s > best_score)
                {
                    best_score = s;
                    best_compute_value = x->compute_value + y->compute_value;
                    best_memory_cost = x->memory_cost + y->memory_cost;

                    std::unordered_set<qubit_type> combined_working_set(x->working_set);
                    combined_working_set.insert(y->working_set.begin(), y->working_set.end());
                    best_working_set = combined_working_set;
                }
            }
        }
    }

    /*
    std::cout << "new working set:";
    for (qubit_type q : best_working_set)
        std::cout << q << " ";
    std::cout << ", score = " << best_score << ", compute value = " << best_compute_value << ", memory cost = " << best_memory_cost << "\n";
    */

    std::vector<double> qubit_scores(num_qubits, 0.0);
    return working_set_search_result_type{best_working_set, best_score, qubit_scores};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int64_t
get_compute_value_of_instruction(INSTRUCTION::TYPE type)
{
    switch (type)
    {
    case INSTRUCTION::TYPE::RZ:
    case INSTRUCTION::TYPE::RX:
        return 20;
    case INSTRUCTION::TYPE::CCX:
    case INSTRUCTION::TYPE::CCZ:
        return 10;
    case INSTRUCTION::TYPE::CX:
    case INSTRUCTION::TYPE::CZ:
        return 2;
    case INSTRUCTION::TYPE::X:
    case INSTRUCTION::TYPE::Y:
    case INSTRUCTION::TYPE::Z:
    case INSTRUCTION::TYPE::SWAP:
        return 0;
    default:
        return 1;
    }
    return 1;
}

WORKING_SET_TREE_NODE*
working_set_tree_traverse(WORKING_SET_TREE_NODE* start)
{
    while (start->child != nullptr)
        start = start->child;
    return start;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
working_set_tree_free(std::vector<WORKING_SET_TREE_NODE*>&& entry_points)
{
    std::unordered_set<WORKING_SET_TREE_NODE*> visited;
    std::vector<WORKING_SET_TREE_NODE*> dfs_stack(entry_points);
    while (dfs_stack.size() > 0)
    {
        auto* node = dfs_stack.back();
        dfs_stack.pop_back();

        if (visited.count(node))
            continue;
        visited.insert(node);

        if (node->child != nullptr)
            dfs_stack.push_back(node->child);
    }

    // algorithm ends when all nodes are visited:
    for (auto* node : visited)
        delete node;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
score_working_set(int64_t compute_value, int64_t memory_cost)
{
    double cv = static_cast<double>(compute_value);
    double mc = static_cast<double>(memory_cost);
    double s = cv / (mc+1.0);
    return s;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace impl
}   // namespace memopt
