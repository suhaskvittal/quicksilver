/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/pass/memory_scheduler/impl.h"

#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions + data structures
 * */

namespace
{

/*
 * CST = Compute Set Tree
 *
 * A `CSTNode` is used by HINT to represent a possible
 * scheduling decision. A `CSTNode` contains a set of qubits,
 * the amount of compute possible with that set as the
 * active set, and the number of memory instructions
 * required to load those qubits into the active set.
 * */
struct CSTNode
{
    using score_type = int32_t;

    active_set_type qubits;
    score_type      compute_count{0};
    score_type      memory_count{0};

    /*
     * CST nodes can only have one child since it is an
     * inverted tree (many parents, single child).
     * */
    CSTNode* child{nullptr};

    /*
     * `frozen` is set to handle dependencies, for example.
     * Encountering a frozen node while building the CST freezes
     * other nodes.
     * */
    bool frozen{false};
};

/*
 * `_cst_init` initializes the "entry_points" of the CST (one per qubit).
 * A qubit in the current active set has 0 memory cost, and any are not have
 * 1 memory cost.
 * */
std::vector<CSTNode*> _cst_init(const active_set_type&, size_t qubit_count);

/*
 * Updates the CST by creating a new node or updating the values in an existing node.
 * */
void _cst_update(std::vector<CSTNode*>&, inst_ptr, Config conf);

/*
 * Traverses the CST by using the `child` pointer in each node. Returns the
 * deepest node from the starting node (deepest node has null child)
 * */
CSTNode* _cst_traverse(CSTNode*);

/*
 * Returns the best active set that maximizes compute intensity.
 * See `_cst_score` for calculation.
 *
 * `_cst_find_best_active_set_simple` just picks the single best-scoring node.
 *
 * `_cst_find_best_active_set_complex` also tries combining disjoint
 * active sets to fill the active set capacity.
 * */
active_set_type _cst_find_best_active_set_simple(const std::vector<CSTNode*>&);
active_set_type _cst_find_best_active_set_complex(const std::vector<CSTNode*>&, size_t active_set_capacity);

double _cst_score(const CSTNode*);
double _cst_score(int32_t compute_count, int32_t memory_count);

/*
 * Deallocates all `CSTNode` pointers.
 * */
void _cst_free(std::vector<CSTNode*>&&);

/*
 * Generic DFS function for a CST.
 * The loop callback is called on the first visit to a node.
 * The exit callback is called on all visited nodes (after the DFS completes).
 * */
template <class LoopCallback, class ExitCallback>
void _cst_generic_dfs(const std::vector<CSTNode*>&, const LoopCallback&, const ExitCallback&);

/*
 * Returns the compute score for the instruction.
 * */
size_t _score_instruction(inst_ptr);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Result
hint(const active_set_type& active_set, const dag_ptr& dag, Config conf)
{
    // build the CST:
    auto entry_points = _cst_init(active_set, dag->qubit_count);
    dag->for_each_instruction_in_layer_order(
            0,
            conf.hint_lookahead_depth,
            [&entry_points, &conf] (inst_ptr inst, size_t) { _cst_update(entry_points, inst, conf); }
    );

    active_set_type best_active_set = conf.hint_use_complex_selection
                                        ? _cst_find_best_active_set_complex(entry_points, conf.active_set_capacity)
                                        : _cst_find_best_active_set_simple(entry_points);
    auto out = transform_active_set(active_set, best_active_set);

    _cst_free(std::move(entry_points));
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTION DEFINITIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<CSTNode*>
_cst_init(const active_set_type& active_set, size_t qubit_count)
{
    std::vector<CSTNode*> entry(qubit_count);
    for (qubit_type i = 0; i < qubit_count; i++)
    {
        CSTNode::score_type memory_score = active_set.count(i) ? 0 : 1;
        entry[i] = new CSTNode{active_set_type{i}, 0, memory_score};
    }
    return entry;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_cst_update(std::vector<CSTNode*>& entry_points, inst_ptr inst, Config conf)
{
    // identify the deepest nodes in the CST that contain the arguments of `inst`
    std::unordered_set<CSTNode*> deepest_nodes;
    deepest_nodes.reserve(get_inst_qubit_count(inst->type));
    bool any_frozen{false};
    for (auto it = inst->q_begin(); it != inst->q_end(); it++)
    {
        auto* x = _cst_traverse(entry_points[*it]);
        deepest_nodes.insert(x);
        any_frozen |= x->frozen;
    }

    if (deepest_nodes.empty())
        std::cerr << "_cst_update: found no nodes for inst: " << *inst << _die{};

    // if any nodes are frozen, then freeze all encountered nodes
    // and exit
    if (any_frozen)
    {
        for (auto* x : deepest_nodes)
            x->frozen = true;
        return;
    }

    if (deepest_nodes.size() == 1)
    {
        // simple case -- just update the sole node:
        CSTNode* x = *deepest_nodes.begin();
        x->compute_count += _score_instruction(inst);
    }
    else
    {
        // need to aggregate all `deepest_nodes` into one combined node:
        CSTNode* y = new CSTNode{};
        for (auto* x : deepest_nodes)
        {
            y->qubits.insert(x->qubits.begin(), x->qubits.end());
            y->compute_count += x->compute_count;
            y->memory_count += x->memory_count;
        }
        y->compute_count += _score_instruction(inst);

        // if `y->qubits` is too large (exceeds active set capacity), then
        // freeze all nodes and delete `y`
        if (y->qubits.size() > conf.active_set_capacity)
        {
            delete y;
            for (auto* x : deepest_nodes)
                x->frozen = true;
            return;
        }

        for (auto* x : deepest_nodes)
            x->child = y;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CSTNode*
_cst_traverse(CSTNode* x)
{
    while (x->child != nullptr)
        x = x->child;
    return x;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

active_set_type
_cst_find_best_active_set_simple(const std::vector<CSTNode*>& entry_points)
{
    CSTNode* best_cst_node{nullptr};
    double best_score;

    _cst_generic_dfs(entry_points,
                    [&best_cst_node, &best_score] (CSTNode* x)
                    {
                        double score = _cst_score(x);
                        bool update = best_cst_node == nullptr
                                        || score > best_score
                                        || (score == best_score && x->qubits.size() > best_cst_node->qubits.size());
                        if (update)
                        {
                            best_cst_node = x;
                            best_score = score;
                        }
                    },
                    [] (CSTNode*) {});
    return best_cst_node->qubits;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

active_set_type
_cst_find_best_active_set_complex(const std::vector<CSTNode*>& entry_points, size_t active_set_capacity)
{
    // 1. Collect all nodes via DFS and categorize by active set size.
    std::vector<std::vector<CSTNode*>> nodes_by_size(active_set_capacity);

    _cst_generic_dfs(entry_points,
                    [&nodes_by_size, active_set_capacity] (CSTNode* x)
                    {
                        size_t idx = x->qubits.size() - 1;
                        if (idx < active_set_capacity)
                            nodes_by_size[idx].push_back(x);
                    },
                    [] (CSTNode*) {});

    // 2. Search for the best active set.
    //    For an active set of size `k`, we also try combining with
    //    disjoint active sets of size `active_set_capacity - k` to
    //    fill the capacity.
    active_set_type best_active_set;
    double best_score{-1.0};

    for (size_t k = active_set_capacity; k > 0; k--)
    {
        for (const auto* x : nodes_by_size[k-1])
        {
            // Check if `x` by itself is good enough:
            double xs = _cst_score(x);
            if (xs > best_score)
            {
                best_score = xs;
                best_active_set = x->qubits;
            }

            // Skip combination search if already at full capacity:
            if (k == active_set_capacity)
                continue;

            // Try combining with disjoint active sets of complementary size:
            size_t complement_size = active_set_capacity - k;
            if (complement_size > active_set_capacity || complement_size == 0)
                continue;

            for (const auto* y : nodes_by_size[complement_size - 1])
            {
                // Make sure the active sets are disjoint:
                bool have_common_qubit = std::any_of(x->qubits.begin(), x->qubits.end(),
                                                     [y] (qubit_type q) { return y->qubits.count(q); });
                if (have_common_qubit)
                    continue;

                // Compute the score of the combined set:
                double combined_score = _cst_score(x->compute_count + y->compute_count,
                                                   x->memory_count + y->memory_count);
                if (combined_score > best_score)
                {
                    best_score = combined_score;
                    best_active_set = x->qubits;
                    best_active_set.insert(y->qubits.begin(), y->qubits.end());
                }
            }
        }
    }

    return best_active_set;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
_cst_score(const CSTNode* x)
{
    return _cst_score(x->compute_count, x->memory_count);
}

double
_cst_score(int32_t compute_count, int32_t memory_count)
{
    double C = static_cast<double>(compute_count);
    double M = static_cast<double>(memory_count);
    return C / (M+1.0); // need to add 1 in case M = 0
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_cst_free(std::vector<CSTNode*>&& entry_points)
{
    _cst_generic_dfs(entry_points,
                    [] (CSTNode*) {},
                    [] (CSTNode* x) { delete x; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class LoopCallback, class ExitCallback> void
_cst_generic_dfs(const std::vector<CSTNode*>& entry_points, const LoopCallback& loopf, const ExitCallback& exitf)
{
    std::unordered_set<CSTNode*> visited;
    std::vector<CSTNode*> dfss(entry_points);
    while (!dfss.empty())
    {
        auto* x = dfss.back();
        dfss.pop_back();
        if (visited.count(x))
            continue;
        visited.insert(x);
        loopf(x);
        if (x->child != nullptr)
            dfss.push_back(x->child);
    }

    for (auto* x : visited)
        exitf(x);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_score_instruction(inst_ptr inst)
{
    if (is_rotation_instruction(inst->type))
        return 20;
    else if (is_toffoli_like_instruction(inst->type))
        return 10;
    else if (is_cx_like_instruction(inst->type))
        return 2;
    else if (is_software_instruction(inst->type))
        return 0;
    else
        return 1;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace pass
}  // namespace compiler
