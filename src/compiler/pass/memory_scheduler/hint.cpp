/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/pass/memory_scheduler/impl.h"

#include <algorithm>
#include <iostream>
#include <memory>
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

using frozen_set_type = std::unordered_set<qubit_type>;

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
    CSTNode* youngest_descendant{nullptr};
};

// Keep the entry nodes and merged nodes for the lifetime of this thread.
// Their pointers remain stable even when the owning vectors grow.
static thread_local std::vector<CSTNode*> ST_ENTRY_POINTS;
static thread_local std::vector<std::unique_ptr<CSTNode>> ST_ENTRY_STORAGE;
static thread_local std::vector<std::unique_ptr<CSTNode>> ST_MERGED_STORAGE;
static thread_local size_t ST_MERGED_NODES_USED{0};

/*
 * `_cst_init` initializes the "entry_points" of the CST (one per qubit).
 * A qubit in the current active set has 0 memory cost, and any are not have
 * 1 memory cost.
 * */
void _cst_init(const active_set_type&, size_t qubit_count);
CSTNode* _cst_next_merged_node();

/*
 * Updates the CST by creating a new node or updating the values in an existing node.
 * */
void _cst_update(std::vector<CSTNode*>&, frozen_set_type&, inst_ptr, Config conf);

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
    _cst_init(active_set, dag->qubit_count);
    frozen_set_type frozen_qubits;
    dag->for_each_instruction_in_layer_order(
            0,
            conf.hint_lookahead_depth,
            [&] (inst_ptr inst, size_t) { _cst_update(ST_ENTRY_POINTS, frozen_qubits, inst, conf); }
    );

    // compute active set
    active_set_type best_active_set = conf.hint_use_complex_selection
                                        ? _cst_find_best_active_set_complex(ST_ENTRY_POINTS, conf.active_set_capacity)
                                        : _cst_find_best_active_set_simple(ST_ENTRY_POINTS);

    // compute eviction scores for each qubit using `best_active_set`
    std::vector<double> scores(dag->qubit_count, 0.0);
    if (conf.hint_use_nonarbitrary_victim_selection)
    {
        for (auto q : best_active_set)
        {
            auto* node = ST_ENTRY_POINTS[q];
            while (node != nullptr)
            {
                for (auto x : node->qubits)
                    scores[x] += 1.0;
                node = node->child;
            }
        }
    }

    auto out = transform_active_set(active_set, best_active_set, scores);

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTION DEFINITIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_cst_init(const active_set_type& active_set, size_t qubit_count)
{
    while (ST_ENTRY_STORAGE.size() < qubit_count)
    {
        qubit_type q = ST_ENTRY_STORAGE.size();
        auto node = std::make_unique<CSTNode>();
        node->qubits.insert(q);
        ST_ENTRY_STORAGE.push_back(std::move(node));
    }

    ST_ENTRY_POINTS.resize(qubit_count);
    ST_MERGED_NODES_USED = 0;
    for (qubit_type i = 0; i < qubit_count; i++)
    {
        CSTNode* node = ST_ENTRY_STORAGE[i].get();
        node->compute_count = 0;
        node->memory_count = active_set.count(i) ? 0 : 1;
        node->child = nullptr;
        node->youngest_descendant = node;
        ST_ENTRY_POINTS[i] = node;
    }
}

CSTNode*
_cst_next_merged_node()
{
    if (ST_MERGED_NODES_USED == ST_MERGED_STORAGE.size())
        ST_MERGED_STORAGE.push_back(std::make_unique<CSTNode>());
    CSTNode* node = ST_MERGED_STORAGE[ST_MERGED_NODES_USED++].get();
    node->qubits.clear();
    node->compute_count = 0;
    node->memory_count = 0;
    node->child = nullptr;
    node->youngest_descendant = node;
    return node;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_cst_update(std::vector<CSTNode*>& entry_points, frozen_set_type& frozen, inst_ptr inst, Config conf)
{
    bool any_frozen = std::any_of(inst->q_begin(), inst->q_end(), [&] (auto q) { return frozen.count(q); });
    if (any_frozen)
    {
        frozen.insert(inst->q_begin(), inst->q_end());
        return;
    }

    // identify the deepest nodes in the CST that contain the arguments of `inst`
    std::unordered_set<CSTNode*> deepest_nodes;
    deepest_nodes.reserve(get_inst_qubit_count(inst->type));
    for (auto it = inst->q_begin(); it != inst->q_end(); it++)
    {
        auto* x = _cst_traverse(entry_points[*it]);
        entry_points[*it]->youngest_descendant = x;
        deepest_nodes.insert(x);
    }

    if (deepest_nodes.empty())
        std::cerr << "_cst_update: found no nodes for inst: " << *inst << _die{};

    if (deepest_nodes.size() == 1)
    {
        // simple case -- just update the sole node:
        CSTNode* x = *deepest_nodes.begin();
        x->compute_count += _score_instruction(inst);
    }
    else
    {
        // need to aggregate all `deepest_nodes` into one combined node:
        CSTNode* y = _cst_next_merged_node();
        for (auto* x : deepest_nodes)
        {
            y->qubits.insert(x->qubits.begin(), x->qubits.end());
            y->compute_count += x->compute_count;
            y->memory_count += x->memory_count;
        }
        y->compute_count += _score_instruction(inst);

        // If the candidate exceeds capacity, freeze its qubits and reuse this
        // node for the next candidate; it has not been linked into the tree.
        if (y->qubits.size() > conf.active_set_capacity)
        {
            frozen.insert(y->qubits.begin(), y->qubits.end());
            ST_MERGED_NODES_USED--;
            return;
        }

        for (auto* x : deepest_nodes)
            x->child = y;
        std::for_each(inst->q_begin(), inst->q_end(), [&] (auto q) { entry_points[q]->youngest_descendant = y; });
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CSTNode*
_cst_traverse(CSTNode* x)
{
    x = x->youngest_descendant;
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
            /*
            // Check if `x` by itself is good enough:
            double xs = _cst_score(x);
            if (xs > best_score)
            {
                best_score = xs;
                best_active_set = x->qubits;
            }
            */

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

    if (best_score < 0)
        return _cst_find_best_active_set_simple(entry_points);
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
