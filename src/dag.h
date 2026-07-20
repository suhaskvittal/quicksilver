/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef DAG_h
#define DAG_h

#include "instruction.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class GraphSearchType { BreadthFirst, DepthFirst, InvertedBreadthFirst, InvertedDepthFirst };

constexpr bool search_is_breadth_first(GraphSearchType);
constexpr bool search_is_inverted(GraphSearchType);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class DAG
{
public:
    using inst_ptr = Instruction*;

    struct node_type
    {
        inst_ptr                inst;
        std::vector<node_type*> dependent{};
        std::vector<node_type*> predecessors{};

        /*
         * These variables are for `for_each_instruction_in_layer_order` (see below)
         * */
        mutable uint8_t tmp_pred_count_{0};
        mutable size_t  last_generation_{0};
    };

    const size_t qubit_count;
private:
    /*
     * We implement `front_layer_` as a hashmap to speedup
     * deletion. So, we can lookup the corresponding node
     * for an instruction and just delete that.
     *
     * Using a std::vector or std::unordered_set would
     * require an O(n) lookup.
     * */
    std::unordered_map<inst_ptr, node_type*> front_layer_;

    /*
     * This is essentially a "bucket" of instructions.
     * Each entry corresponds to a different qubit and
     * corresponds to a node for the youngest instruction
     * to that qubit.
     * */
    std::vector<node_type*> back_instructions_;

    size_t inst_count_{0};

    /*
     * This is a table for memoization. This class does not allow for direct access
     * with `node_type*` for safety. However, if the user knows they will access
     * a particular instruction frequently (and this instruction is not in the front layer),
     * they can request it to be memoized, and operations can be done on this memoized
     * data.
     * */
    std::unordered_map<inst_ptr, node_type*> node_lookup_table_;

    /*
     * This is used during `for_each_instruction_in_layer_order`
     * */
    mutable size_t iteration_generation_{0};
public:
    DAG(size_t qubit_count);
    ~DAG();

    void add_instruction(inst_ptr, bool memoize=false);
    void remove_instruction_from_front_layer(inst_ptr);

    /*
     * Removes all instructions from the DAG, deleting the underlying
     * nodes and instructions and reseting the DAG to an empty state.
     *
     * If `dealloc_inst` is false, then instruction pointers are not
     * freed.
     * */
    void clear(bool dealloc_inst=true);

    /*
     * This returns a list of all instructions in the front layer.
     * These are the oldest instructions in the program.
     * */
    std::vector<inst_ptr> get_front_layer() const { return get_front_layer_if([] (const auto*) { return true; }); }

    /*
     * This is a modified version of the above function that
     * returns instructions in the front layer that meet the given
     * predicate.
     * */
    template <class Pred>
    std::vector<inst_ptr> get_front_layer_if(const Pred&) const;

    /*
     * Executes the given callback for `min_layer` to `max_layer`. The callback
     * is given the instruction (first argument) and the layer number (second argument).
     * */
    template <class Callback>
    void for_each_instruction_in_layer_order(size_t min_layer, size_t max_layer, const Callback&) const;

    /*
     * Executes a graph search where x is breadth or depth. Search
     * begins from provided instruction, which must either be in the front layer or
     * memoized explicitly.
     *
     * The user msut provide a callback that will be called for every visited node. If the callback
     * returns true, then the neighbors of the node are not traversed.
     * */
    template <GraphSearchType S, class Callback>
    void search(inst_ptr src, const Callback&) const;

    size_t inst_count() const { return inst_count_; }
private:

    /*
     * Templated functions that allow for a callback to a node on arrival. All nodes are traversed in layer
     * order.
     * */
    template <class Callback>
    void operate_on_nodes_in_layer_order(this auto&, size_t min_layer, size_t max_layer, const Callback&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "dag.tpp"

#endif   // DAG_h
