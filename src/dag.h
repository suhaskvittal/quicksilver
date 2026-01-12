/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef DAG_h
#define DAG_h

#include "instruction.h"

#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class DAG
{
public:
    using inst_ptr = INSTRUCTION*;

    struct node_type
    {
        inst_ptr                inst;
        std::vector<node_type*> dependent{};
        size_t                  pred_count{0};
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
public:
    DAG(size_t qubit_count);
    ~DAG();

    void add_instruction(inst_ptr);
    void remove_instruction_from_front_layer(inst_ptr);

    /*
     * This returns a list of all instructions in the front layer.
     * These are the oldest instructions in the program.
     * */
    std::vector<inst_ptr> get_front_layer() const;

    /*
     * This is a modified version of the above function that
     * returns instructions in the front layer that meet the given
     * predicate.
     * */
    template <class PRED>
    std::vector<inst_ptr> get_front_layer_if(const PRED&) const;

    /*
     * Executes the given callback for up-to `max_layer`
     * */
    template <class CALLBACK>
    void for_each_instruction_in_layer_order(const CALLBACK&, size_t max_layer) const;

    size_t inst_count() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "dag.tpp"

#endif   // DAG_h
