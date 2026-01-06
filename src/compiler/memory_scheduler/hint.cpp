/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/memory_scheduler/impl.h"

#include <iostream>
#include <unordered_set>

namespace compile
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
 * A `CST_NODE` is used by HINT to represent a possible
 * scheduling decision. A `CST_NODE` contains a set of qubits,
 * the amount of compute possible with that set as the
 * active set, and the number of memory instructions
 * required to load those qubits into the active set.
 * */
struct CST_NODE
{
    using score_type = int16_t;

    active_set_type qubits;
    score_type      compute_count{0};
    score_type      memory_count{0};

    /*
     * CST nodes can only have one child since it is an
     * inverted tree (many parents, single child).
     * */
    CST_NODE* child{nullptr};

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
std::vector<CST_NODE*> _cst_init(const active_set_type&, size_t qubit_count);

/*
 * Updates the CST by creating a new node or updating the values in an existing node.
 * */
void _cst_update(std::vector<CST_NODE*>&, inst_ptr, config_type conf);

/*
 * Traverses the CST by using the `child` pointer in each node. Returns the
 * deepest node from the starting node (deepest node has null child)
 * */
CST_NODE* _cst_traverse(CST_NODE*);

/*
 * Returns the CST node that maximizes compute intensity
 * See `_cst_score` for calculation.
 * */
CST_NODE* _cst_find_best_node(const std::vector<CST_NODE*>&);
double    _cst_score(CST_NODE*);

/*
 * Deallocates all `CST_NODE` pointers.
 * */
void _cst_free(std::vector<CST_NODE*>&&);

/*
 * Generic DFS function for a CST.
 * The loop callback is called on the first visit to a node.
 * The exit callback is called on all visited nodes (after the DFS completes).
 * */
template <class LOOP_CALLBACK, class EXIT_CALLBACK>
void _cst_generic_dfs(const std::vector<CST_NODE*>&, const LOOP_CALLBACK&, const EXIT_CALLBACK&);

/*
 * Returns the compute score for the instruction.
 * */
size_t _score_instruction(inst_ptr);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

result_type
hint(const active_set_type& active_set, const dag_ptr& dag, config_type conf)
{
    // build the CST:
    auto entry_points = _cst_init(active_set, dag->qubit_count);
    dag->for_each_instruction_in_layer_order(
            [&entry_points, &conf] (inst_ptr inst) { _cst_update(entry_points, inst, conf); },
            conf.hint_lookahead_depth
    );
    CST_NODE* best = _cst_find_best_node(entry_points);
    auto out = transform_active_set(active_set, best->qubits);

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

std::vector<CST_NODE*>
_cst_init(const active_set_type& active_set, size_t qubit_count)
{
    std::vector<CST_NODE*> entry(qubit_count);
    for (qubit_type i = 0; i < qubit_count; i++)
    {
        CST_NODE::score_type memory_score = active_set.count(i) ? 0 : 1;
        entry[i] = new CST_NODE{active_set_type{i}, 0, memory_score};
    }
    return entry;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_cst_update(std::vector<CST_NODE*>& entry_points, inst_ptr inst, config_type conf)
{
    // identify the deepest nodes in the CST that contain the arguments of `inst`
    std::unordered_set<CST_NODE*> deepest_nodes;
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
        CST_NODE* x = *deepest_nodes.begin();
        x->compute_count += _score_instruction(inst);
    }
    else
    {
        // need to aggregate all `deepest_nodes` into one combined node:
        CST_NODE* y = new CST_NODE{};
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

CST_NODE*
_cst_traverse(CST_NODE* x)
{
    while (x->child != nullptr)
        x = x->child;
    return x;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

CST_NODE*
_cst_find_best_node(const std::vector<CST_NODE*>& entry_points)
{
    CST_NODE* best_cst_node{nullptr};
    double best_score;

    _cst_generic_dfs(entry_points,
                    [&best_cst_node, &best_score] (CST_NODE* x)
                    {
                        double score = _cst_score(x);
                        if (best_cst_node == nullptr || score > best_score)
                        {
                            best_cst_node = x;
                            best_score = score;
                        }
                    },
                    [] (CST_NODE*) {});
    return best_cst_node;
}

double
_cst_score(CST_NODE* x)
{
    double C = static_cast<double>(x->compute_count);
    double M = static_cast<double>(x->memory_count);
    return C / (M+1.0); // need to add in case M = 0
}

void
_cst_free(std::vector<CST_NODE*>&& entry_points)
{
    _cst_generic_dfs(entry_points,
                    [] (CST_NODE*) {},
                    [] (CST_NODE* x) { delete x; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class LOOP_CALLBACK, class EXIT_CALLBACK> void
_cst_generic_dfs(const std::vector<CST_NODE*>& entry_points, const LOOP_CALLBACK& loopf, const EXIT_CALLBACK& exitf)
{
    std::unordered_set<CST_NODE*> visited;
    std::vector<CST_NODE*> dfss(entry_points);
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
}  // namespace compile
