/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#include "compiler/memopt/impl/viszlai.h"

namespace memopt
{
namespace impl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VISZLAI::result_type
VISZLAI::emit_memory_instructions(const ws_type& current_working_set, const inst_array& pending_inst, const inst_window_map& inst_windows)
{
    inst_array memory_instructions;
    ws_type new_working_set;

    std::vector<bool> visited(num_qubits, false);
    std::vector<inst_ptr> priority_instructions;
    std::vector<inst_ptr> head_instructions;
    priority_instructions.reserve(current_working_set.size());
    head_instructions.reserve(num_qubits);
    for (const auto& [q, win] : inst_windows)
    {
        if (visited[q])
            continue;

        if (win.empty())
            continue;

        inst_ptr inst = win.front();
        bool all_ready = std::all_of(inst->qubits.begin(), inst->qubits.end(),
                                        [&inst_windows, inst] (qubit_type q) { return inst_windows.at(q).front() == inst; });
        if (all_ready)
        {
            bool is_priority{false};
            for (qubit_type q : inst->qubits)
            {
                visited[q] = true;
                is_priority |= current_working_set.count(q);
            }

            if (is_priority)
                priority_instructions.push_back(inst);
            else
                head_instructions.push_back(inst);
        }
    }

    // first pass: check if any qubits in `current_working_set` have a ready instruction at the head of the window:
    for (inst_ptr inst : priority_instructions)
    {
        instruction_selection_iteration(inst, new_working_set);
        if (new_working_set.size() >= cmp_count)
            break;
    }

    // second pass: check if any qubits in `pending_inst` have a ready instruction at the head of the window:
    if (new_working_set.size() < cmp_count)
    {
        for (inst_ptr inst : head_instructions)
        {
            instruction_selection_iteration(inst, new_working_set);
            if (new_working_set.size() >= cmp_count)
                break;
        }
    }

    // generate memory instructions:
    std::vector<double> qubit_scores(num_qubits, 0.0);

    return transform_working_set_into(current_working_set, new_working_set, qubit_scores);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
VISZLAI::instruction_selection_iteration(inst_ptr inst, ws_type& ws)
{
    if (inst->qubits.size() > cmp_count - ws.size())
        return;

    ws.insert(inst->qubits.begin(), inst->qubits.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace impl
}  // namespace memopt