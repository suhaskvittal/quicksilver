/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#ifndef COMPILER_MEMOPT_IMPL_h
#define COMPILER_MEMOPT_IMPL_h

#include "instruction.h"

#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace memopt
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class IMPL_BASE
{
public:
    using inst_ptr = INSTRUCTION*;
    using ws_type = std::unordered_set<qubit_type>;
    using inst_array = std::vector<inst_ptr>;
    using inst_window_type = std::deque<inst_ptr>;
    using inst_window_map = std::unordered_map<qubit_type, inst_window_type>;

    struct result_type
    {
        inst_array memory_instructions;
        ws_type working_set;
        size_t unused_bandwidth;
    };

    const size_t cmp_count;
    uint32_t num_qubits{};  // set by the owner -- don't worry about it :)
public:
    IMPL_BASE(size_t cmp_count);

    virtual result_type emit_memory_instructions(const ws_type& current_working_set, const inst_array& pending_inst, const inst_window_map& inst_windows) =0;
protected:
    // a lower score means the qubit should be evicted
    result_type transform_working_set_into(const ws_type& curr, const ws_type& target, const std::vector<double>& qubit_score);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memopt

#endif  // COMPILER_MEMORY_COMPILER_EMISSION_IMPL_h