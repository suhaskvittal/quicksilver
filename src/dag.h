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
        std::vector<node_type*> dependent;
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

    std::vector<inst_ptr> get_front_layer() const;

    size_t inst_count() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif   // DAG_h
